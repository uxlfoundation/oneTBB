/*
    Copyright (c) 2005-2022 Intel Corporation
    Copyright (c) 2026 UXL Foundation Contributors

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

#include "oneapi/tbb/detail/_config.h"
#include "oneapi/tbb/detail/_assert.h"
#include "../tbb/assert_impl.h"

#if !__TBB_WIN8UI_SUPPORT && defined(_WIN32)

#ifndef _CRT_SECURE_NO_DEPRECATE
#define _CRT_SECURE_NO_DEPRECATE 1
#endif

// no standard-conforming implementation of snprintf prior to VS 2015
#if !defined(_MSC_VER) || _MSC_VER>=1900
#define LOG_PRINT(s, n, format, ...) snprintf(s, n, format, __VA_ARGS__)
#else
#define LOG_PRINT(s, n, format, ...) _snprintf_s(s, n, _TRUNCATE, format, __VA_ARGS__)
#endif

#include <windows.h>
#include <new>
#include <stdio.h>
#include <string.h>

#include "function_replacement.h"

// The information about a standard memory allocation function for the replacement log
struct FunctionInfo {
    const char* funcName;
    const char* dllName;
};

// Namespace that processes and manages the output of records to the Log journal
// that will be provided to user by TBB_malloc_replacement_log()
namespace Log {
    // Value of RECORDS_COUNT is set due to the fact that we maximally
    // scan 8 modules, and in every module we can swap 6 opcodes. (rounded to 8)
    static const unsigned RECORDS_COUNT = 8 * 8;
    static const unsigned RECORD_LENGTH = MAX_PATH;

    // Need to add 1 to count of records, because last record must be always nullptr
    static char *records[RECORDS_COUNT + 1];
    static bool replacement_status = true;

    // Internal counter that contains number of next string for record
    static unsigned record_number = 0;

    // Function that writes info about (not)found opcodes to the Log journal
    // functionInfo - information about a standard memory allocation function for the replacement log
    // opcodeString - string, that contain byte code of this function
    // status - information about function replacement status
    static void record(FunctionInfo functionInfo, const char * opcodeString, bool status) {
        __TBB_ASSERT(functionInfo.dllName, "Empty DLL name value");
        __TBB_ASSERT(functionInfo.funcName, "Empty function name value");
        __TBB_ASSERT(opcodeString, "Empty opcode");
        __TBB_ASSERT(record_number <= RECORDS_COUNT, "Incorrect record number");

        //If some replacement failed -> set status to false
        replacement_status &= status;

        // If we reach the end of the log, write this message to the last line
        if (record_number == RECORDS_COUNT) {
            // %s - workaround to fix empty variable argument parsing behavior in GCC
            LOG_PRINT(records[RECORDS_COUNT - 1], RECORD_LENGTH, "%s", "Log was truncated.");
            return;
        }

        char* entry = (char*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, RECORD_LENGTH);
        __TBB_ASSERT(entry, "Invalid memory was returned");

        LOG_PRINT(entry, RECORD_LENGTH, "%s: %s (%s), byte pattern: <%s>",
            status ? "Success" : "Fail", functionInfo.funcName, functionInfo.dllName, opcodeString);

        records[record_number++] = entry;
    }
};

inline UINT_PTR Ptr2Addrint(LPVOID ptr)
{
    Int2Ptr i2p;
    i2p.lpv = ptr;
    return i2p.uip;
}

inline LPVOID Addrint2Ptr(UINT_PTR ptr)
{
    Int2Ptr i2p;
    i2p.uip = ptr;
    return i2p.lpv;
}

// Is the distance between addr1 and addr2 smaller than dist
inline bool IsInDistance(UINT_PTR addr1, UINT_PTR addr2, __int64 dist)
{
    __int64 diff = addr1>addr2 ? addr1-addr2 : addr2-addr1;
    return diff<dist;
}

/*
 * When inserting a probe in 64 bits process the distance between the insertion
 * point and the target may be bigger than 2^32. In this case we are using
 * indirect jump through memory where the offset to this memory location
 * is smaller than 2^32 and it contains the absolute address (8 bytes).
 *
 * This class is used to hold the pages used for the above trampolines.
 * Since this utility will be used to replace malloc functions this implementation
 * doesn't allocate memory dynamically.
 *
 * The struct MemoryBuffer holds the data about a page in the memory used for
 * replacing functions in 64-bit code where the target is too far to be replaced
 * with a short jump. All the calculations of m_base and m_next are in a multiple
 * of SIZE_OF_ADDRESS (which is 8 in Win64).
 */
class MemoryProvider {
private:
    struct MemoryBuffer {
        UINT_PTR m_base;    // base address of the buffer
        UINT_PTR m_next;    // next free location in the buffer
        DWORD    m_size;    // size of buffer

        // Default constructor
        MemoryBuffer() : m_base(0), m_next(0), m_size(0) {}

        // Constructor
        MemoryBuffer(void *base, DWORD size)
        {
            m_base = Ptr2Addrint(base);
            m_next = m_base;
            m_size = size;
        }
    };

MemoryBuffer *CreateBuffer(UINT_PTR addr)
    {
        // No more room in the pages database
        if (m_lastBuffer - m_pages == MAX_NUM_BUFFERS)
            return 0;

        void *newAddr = Addrint2Ptr(addr);
        // Get information for the region which the given address belongs to
        MEMORY_BASIC_INFORMATION memInfo;
        if (VirtualQuery(newAddr, &memInfo, sizeof(memInfo)) != sizeof(memInfo))
            return 0;

        for(;;) {
            // The new address to check is beyond the current region and aligned to allocation size
            newAddr = Addrint2Ptr( (Ptr2Addrint(memInfo.BaseAddress) + memInfo.RegionSize + m_allocSize) & ~(UINT_PTR)(m_allocSize-1) );

            // Check that the address is in the right distance.
            // VirtualAlloc can only round the address down; so it will remain in the right distance
            if (!IsInDistance(addr, Ptr2Addrint(newAddr), MAX_DISTANCE))
                break;

            if (VirtualQuery(newAddr, &memInfo, sizeof(memInfo)) != sizeof(memInfo))
                break;

            if (memInfo.State == MEM_FREE && memInfo.RegionSize >= m_allocSize)
            {
                // Found a free region, try to allocate a page in this region
                void *newPage = VirtualAlloc(newAddr, m_allocSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
                if (!newPage)
                    break;

                // Add the new page to the pages database
                MemoryBuffer *pBuff = new (m_lastBuffer) MemoryBuffer(newPage, m_allocSize);
                ++m_lastBuffer;
                return pBuff;
            }
        }

        // Nothing free above within the distance: search below the allocation that contains addr.
        if (VirtualQuery(Addrint2Ptr(addr), &memInfo, sizeof(memInfo)) != sizeof(memInfo))
            return 0;
        UINT_PTR regionStart = Ptr2Addrint(memInfo.AllocationBase ? memInfo.AllocationBase : memInfo.BaseAddress);
        for(;;) {
            if (regionStart < m_allocSize)
                break;
            // The highest allocation-aligned block that ends at or below the start of the current region
            const UINT_PTR candidate = (regionStart - m_allocSize) & ~(UINT_PTR)(m_allocSize-1);

            // The whole block is below addr, so its farthest byte from addr is the candidate itself
            if (!IsInDistance(addr, candidate, MAX_DISTANCE))
                break;

            if (VirtualQuery(Addrint2Ptr(candidate), &memInfo, sizeof(memInfo)) != sizeof(memInfo))
                break;

            // For a free region, RegionSize counts from the candidate up to the next allocation
            if (memInfo.State == MEM_FREE && memInfo.RegionSize >= m_allocSize)
            {
                void *newPage = VirtualAlloc(Addrint2Ptr(candidate), m_allocSize, MEM_COMMIT|MEM_RESERVE, PAGE_READWRITE);
                if (!newPage)
                    break;

                MemoryBuffer *pBuff = new (m_lastBuffer) MemoryBuffer(newPage, m_allocSize);
                ++m_lastBuffer;
                return pBuff;
            }

            // Continue below this region: below the candidate if it was a too-small free gap,
            // otherwise below the start of the allocation (e.g. a whole DLL image) that occupies it.
            regionStart = memInfo.State == MEM_FREE
                ? candidate
                : Ptr2Addrint(memInfo.AllocationBase ? memInfo.AllocationBase : memInfo.BaseAddress);
        }

        // Failed to find a buffer in the distance
        return 0;
    }

public:
    MemoryProvider()
    {
        SYSTEM_INFO sysInfo;
        GetSystemInfo(&sysInfo);
        m_allocSize = sysInfo.dwAllocationGranularity;
        m_lastBuffer = &m_pages[0];
    }

    // We can't free the pages in the destructor because the trampolines
    // are using these memory locations and a replaced function might be called
    // after the destructor was called.
    ~MemoryProvider()
    {
    }

    // Return a memory location in distance less than 2^31 from input address
    UINT_PTR GetLocation(UINT_PTR addr)
    {
        MemoryBuffer *pBuff = m_pages;
        for (; pBuff<m_lastBuffer && IsInDistance(pBuff->m_next, addr, MAX_DISTANCE); ++pBuff)
        {
            if (pBuff->m_next < pBuff->m_base + pBuff->m_size)
            {
                UINT_PTR loc = pBuff->m_next;
                pBuff->m_next += MAX_PROBE_SIZE;
                return loc;
            }
        }

        pBuff = CreateBuffer(addr);
        if(!pBuff)
            return 0;

        UINT_PTR loc = pBuff->m_next;
        pBuff->m_next += MAX_PROBE_SIZE;
        return loc;
    }

private:
    MemoryBuffer m_pages[MAX_NUM_BUFFERS];
    MemoryBuffer *m_lastBuffer;
    DWORD m_allocSize;
};

static MemoryProvider memProvider;

#if !defined(_M_ARM64)

// Compare opcodes from dictionary (str1) and opcodes from code (str2)
// str1 might contain '*' to mask addresses
// RETURN: 0 if opcodes did not match, 1 on success
size_t compareStrings( const char *str1, const char *str2 )
{
   for (size_t i=0; str1[i]!=0; i++){
       if( str1[i]!='*' && str1[i]!='#' && str1[i]!=str2[i] ) return 0;
   }
   return 1;
}

// Check function prologue with known prologues from the dictionary
// opcodes - dictionary
// inpAddr - pointer to function prologue
// Dictionary contains opcodes for several full asm instructions
// + one opcode byte for the next asm instruction for safe address processing
// RETURN: 1 + the index of the matched pattern, or 0 if no match found.
static UINT CheckOpcodes( const char ** opcodes, void *inpAddr, bool abortOnError, const FunctionInfo* functionInfo = nullptr)
{
    static size_t opcodesStringsCount = 0;
    static size_t maxOpcodesLength = 0;
    static size_t opcodes_pointer = (size_t)opcodes;
    char opcodeString[2*MAX_PATTERN_SIZE+1];
    size_t i;
    size_t result = 0;

    // Get the values for static variables
    // max length and number of patterns
    if( !opcodesStringsCount || opcodes_pointer != (size_t)opcodes ){
        while( *(opcodes + opcodesStringsCount)!= nullptr ){
            if( (i=strlen(*(opcodes + opcodesStringsCount))) > maxOpcodesLength )
                maxOpcodesLength = i;
            opcodesStringsCount++;
        }
        opcodes_pointer = (size_t)opcodes;
        __TBB_ASSERT( maxOpcodesLength/2 <= MAX_PATTERN_SIZE, "Pattern exceeded the limit of 28 opcodes/56 symbols" );
    }

    // Translate prologue opcodes to string format to compare
    for( i=0; i<maxOpcodesLength/2 && i<MAX_PATTERN_SIZE; ++i ){
        sprintf( opcodeString + 2*i, "%.2X", *((unsigned char*)inpAddr+i) );
    }
    opcodeString[2*i] = 0;

    // Compare translated opcodes with patterns
    for( UINT idx=0; idx<opcodesStringsCount; ++idx ){
        result = compareStrings( opcodes[idx],opcodeString );
        if( result ) {
            if (functionInfo) {
                Log::record(*functionInfo, opcodeString, /*status*/ true);
            }
            return idx + 1; // avoid 0 which indicates a failure
        }
    }
    if (functionInfo) {
        Log::record(*functionInfo, opcodeString, /*status*/ false);
    }
    if (abortOnError) {
        // Impossibility to find opcodes in the dictionary is a serious issue,
        // as if we unable to call original function, leak or crash is expected result.
        __TBB_ASSERT_RELEASE( false, "CheckOpcodes failed" );
    }
    return 0;
}

// Modify offsets in original code after moving it to a trampoline.
// We do not have more than one offset to correct in existing opcode patterns.
static void CorrectOffset( UINT_PTR address, const char* pattern, UINT distance )
{
    const char* pos = strstr(pattern, "#*******");
    if( pos ) {
        address += (pos - pattern)/2; // compute the offset position
        UINT value;
        // UINT assignment is not used to avoid potential alignment issues
        memcpy(&value, Addrint2Ptr(address), sizeof(value));
        value += distance;
        memcpy(Addrint2Ptr(address), &value, sizeof(value));
    }
}

#if defined(_M_X64)
/*
 * A trampoline runs the first bytes of the original function, which can be prologue
 * instructions that move RSP or push registers. A stack walk (by a debugger or a profiler)
 * that pauses a thread there needs unwind data for the trampoline; otherwise the trampoline
 * is taken for a leaf function and the return address is read from the wrong place.
 *
 * The trampoline gets a copy of the unwind data of the original function, registered with
 * RtlAddFunctionTable as the documentation requires for generated code. No unwind codes need to
 * be selected: in a prolog, the unwinder only undoes the codes whose offset is not past RIP, which
 * are those of the moved instructions. The copy declares the whole trampoline as prolog, as past
 * the prolog the unwinder checks whether the code at RIP is an epilog, and the final jmp looks like one.
 *
 * Only version 1 unwind data without chained info is copied. The functions replaced with a
 * trampoline in ucrtbase.dll (_aligned_free, _aligned_msize, _free_base; free and _o_free have no
 * unwind data, _msize is a jmp) use it, with at most 4 unwind codes (ucrtbase.dll 10.0.28000).
 * For other unwind data nothing is registered, and stack walks behave as without this support.
 *
 * References, in the Microsoft documentation:
 * - "x64 exception handling", https://learn.microsoft.com/cpp/build/exception-handling-x64:
 *   "struct UNWIND_INFO" for the layout, and "Unwind procedure" for how the unwinder uses it.
 * - RtlAddFunctionTable, https://learn.microsoft.com/windows/win32/api/winnt/nf-winnt-rtladdfunctiontable
 */
namespace X64Unwind {
    // UNWIND_INFO; it is documented, but not defined in the Windows SDK headers
    struct Info {
        UCHAR versionAndFlags;
        UCHAR sizeOfProlog;
        UCHAR countOfCodes;
        UCHAR frame;
        USHORT codes[1];
    };
    const unsigned MAX_CODES = (MAX_PROBE_SIZE - sizeof(RUNTIME_FUNCTION) - offsetof(Info, codes)) / sizeof(USHORT);

    // Register unwind data for the trampoline that runs `moved` bytes of the function at src
    inline void RegisterTrampoline(UINT_PTR src, UINT_PTR tramp, size_t moved) {
        DWORD64 imageBase = 0;
        const RUNTIME_FUNCTION* func = RtlLookupFunctionEntry(src, &imageBase, nullptr);
        // Without unwind data, the function is a leaf that doesn't change RSP: nothing to describe.
        // UNWIND_INFO is DWORD-aligned, so an odd UnwindData is not its address.
        if (!func || imageBase + func->BeginAddress != src || (func->UnwindData & 1))
            return;
        const Info* orig = (const Info*)Addrint2Ptr(imageBase + func->UnwindData);
        if ((orig->versionAndFlags & 0x7) != 1 || ((orig->versionAndFlags >> 3) & UNW_FLAG_CHAININFO)
            || orig->countOfCodes > MAX_CODES)
            return;

        const UINT_PTR slot = memProvider.GetLocation(src);
        if (!slot)
            return;
        RUNTIME_FUNCTION* entry = (RUNTIME_FUNCTION*)Addrint2Ptr(slot);
        Info* info = (Info*)Addrint2Ptr(slot + sizeof(RUNTIME_FUNCTION));
        memcpy(info, orig, offsetof(Info, codes) + orig->countOfCodes * sizeof(USHORT));
        info->versionAndFlags = 1; // no handlers: no exception can be raised in the trampoline
        info->sizeOfProlog = (UCHAR)(moved + SIZE_OF_RELJUMP);

        // Both the trampoline and its unwind data are within 2 GB of src
        const UINT_PTR base = slot < tramp ? slot : tramp;
        entry->BeginAddress = (DWORD)(tramp - base);
        entry->EndAddress = (DWORD)(tramp + moved + SIZE_OF_RELJUMP - base);
        entry->UnwindData = (DWORD)(Ptr2Addrint(info) - base);
        RtlAddFunctionTable(entry, 1, base);
    }
} // namespace X64Unwind
#endif // _M_X64

// Insert jump relative instruction to the input address
// RETURN: the size of the trampoline or 0 on failure
static DWORD InsertTrampoline32(void *inpAddr, void *targetAddr, const char* pattern, void** storedAddr)
{
    size_t bytesToMove = SIZE_OF_RELJUMP;
    UINT_PTR srcAddr = Ptr2Addrint(inpAddr);
    UINT_PTR tgtAddr = Ptr2Addrint(targetAddr);
    // Check that the target fits in 32 bits
    if (!IsInDistance(srcAddr, tgtAddr, MAX_DISTANCE))
        return 0;

    UINT_PTR offset;
    UINT offset32;
    UCHAR *codePtr = (UCHAR *)inpAddr;

    if ( storedAddr ){ // If requested, store original function code
        bytesToMove = strlen(pattern)/2-1; // The last byte matching the pattern must not be copied
        __TBB_ASSERT_RELEASE( bytesToMove >= SIZE_OF_RELJUMP, "Incorrect bytecode pattern?" );
        UINT_PTR trampAddr = memProvider.GetLocation(srcAddr);
        if (!trampAddr)
            return 0;
        *storedAddr = Addrint2Ptr(trampAddr);
        // Set 'executable' flag for original instructions in the new place
        DWORD pageFlags = PAGE_EXECUTE_READWRITE;
        if (!VirtualProtect(*storedAddr, MAX_PROBE_SIZE, pageFlags, &pageFlags)) return 0;
        // Copy original instructions to the new place
        memcpy(*storedAddr, codePtr, bytesToMove);
        offset = srcAddr - trampAddr;
        offset32 = (UINT)(offset & 0xFFFFFFFF);
        CorrectOffset( trampAddr, pattern, offset32 );
        // Set jump to the code after replacement
        offset32 -= SIZE_OF_RELJUMP;
        *(UCHAR*)(trampAddr+bytesToMove) = 0xE9;
        memcpy((UCHAR*)(trampAddr+bytesToMove+1), &offset32, sizeof(offset32));
#if defined(_M_X64)
        X64Unwind::RegisterTrampoline(srcAddr, trampAddr, bytesToMove);
#endif
    }

    // The following will work correctly even if srcAddr>tgtAddr, as long as
    // address difference is less than 2^31, which is guaranteed by IsInDistance.
    offset = tgtAddr - srcAddr - SIZE_OF_RELJUMP;
    offset32 = (UINT)(offset & 0xFFFFFFFF);
    // Insert the jump to the new code
    *codePtr = 0xE9;
    memcpy(codePtr+1, &offset32, sizeof(offset32));

    // Fill the rest with NOPs to correctly see disassembler of old code in debugger.
    for( unsigned i=SIZE_OF_RELJUMP; i<bytesToMove; i++ ){
        *(codePtr+i) = 0x90;
    }

    return SIZE_OF_RELJUMP;
}

// This function is called when the offset doesn't fit in 32 bits
// 1  Find and allocate a page in the small distance (<2^31) from input address
// 2  Put jump RIP relative indirect through the address in the close page
// 3  Put the absolute address of the target in the allocated location
// RETURN: the size of the trampoline or 0 on failure
static DWORD InsertTrampoline64(void *inpAddr, void *targetAddr, const char* pattern, void** storedAddr)
{
    size_t bytesToMove = SIZE_OF_INDJUMP;

    UINT_PTR srcAddr = Ptr2Addrint(inpAddr);
    UINT_PTR tgtAddr = Ptr2Addrint(targetAddr);

    // Get a location close to the source address
    UINT_PTR location = memProvider.GetLocation(srcAddr);
    if (!location)
        return 0;

    UINT_PTR offset;
    UINT offset32;
    UCHAR *codePtr = (UCHAR *)inpAddr;

    // Fill the location
    UINT_PTR *locPtr = (UINT_PTR *)Addrint2Ptr(location);
    *locPtr = tgtAddr;

    if ( storedAddr ){ // If requested, store original function code
        bytesToMove = strlen(pattern)/2-1; // The last byte matching the pattern must not be copied
        __TBB_ASSERT_RELEASE( bytesToMove >= SIZE_OF_INDJUMP, "Incorrect bytecode pattern?" );
        UINT_PTR trampAddr = memProvider.GetLocation(srcAddr);
        if (!trampAddr)
            return 0;
        *storedAddr = Addrint2Ptr(trampAddr);
        // Set 'executable' flag for original instructions in the new place
        DWORD pageFlags = PAGE_EXECUTE_READWRITE;
        if (!VirtualProtect(*storedAddr, MAX_PROBE_SIZE, pageFlags, &pageFlags)) return 0;
        // Copy original instructions to the new place
        memcpy(*storedAddr, codePtr, bytesToMove);
        offset = srcAddr - trampAddr;
        offset32 = (UINT)(offset & 0xFFFFFFFF);
        CorrectOffset( trampAddr, pattern, offset32 );
        // Set jump to the code after replacement. It is within the distance of relative jump!
        offset32 -= SIZE_OF_RELJUMP;
        *(UCHAR*)(trampAddr+bytesToMove) = 0xE9;
        memcpy((UCHAR*)(trampAddr+bytesToMove+1), &offset32, sizeof(offset32));
#if defined(_M_X64)
        X64Unwind::RegisterTrampoline(srcAddr, trampAddr, bytesToMove);
#endif
    }

    // Fill the buffer
    offset = location - srcAddr - SIZE_OF_INDJUMP;
    offset32 = (UINT)(offset & 0xFFFFFFFF);
    *(codePtr) = 0xFF;
    *(codePtr+1) = 0x25;
    memcpy(codePtr+2, &offset32, sizeof(offset32));

    // Fill the rest with NOPs to correctly see disassembler of old code in debugger.
    for( unsigned i=SIZE_OF_INDJUMP; i<bytesToMove; i++ ){
        *(codePtr+i) = 0x90;
    }

    return SIZE_OF_INDJUMP;
}

#else // _M_ARM64

/*
 * ARM64: instructions are 4 bytes long and 4-byte aligned, so a function is redirected by
 * replacing only its first instruction with a B to a probe placed within +-128 MB. The probe
 * loads the absolute target address into x16 (IP0, free to use at a call boundary) and
 * branches to it. Because a single instruction is displaced, no instruction-length decoding
 * or prologue dictionary is needed: the trampoline that calls the original function executes
 * the displaced instruction (rewritten if it is PC-relative) and branches back to entry + 4.
 * Instructions that can't be moved make IsPrologueKnown() fail, so replacement is skipped,
 * as on x86/x64 for unknown prologues.
 */
namespace Arm64 {
    const UINT32 LDR_X16_PLUS8 = 0x58000050; // ldr x16, #8
    const UINT32 BR_X16        = 0xD61F0200; // br  x16

    enum Kind {
        PLAIN,       // PC-independent: copied as is
        BRANCH,      // B: its target is the original function (hot-patch stubs in ucrtbase.dll)
        COND_BRANCH, // CBZ/CBNZ/TBZ/TBNZ/B.cond: re-emitted with a local target
        ADDRESS,     // ADR/ADRP: replaced by a load of the computed address
        UNSUPPORTED
    };

    inline INT64 SignExtend(UINT64 value, unsigned bits) {
        const UINT64 sign = (UINT64)1 << (bits - 1);
        value &= ((UINT64)1 << bits) - 1;
        return (INT64)((value ^ sign) - sign);
    }

    inline bool IsB(UINT32 i)          { return (i & 0xFC000000) == 0x14000000; }
    inline bool IsCbz(UINT32 i)        { return (i & 0x7E000000) == 0x34000000; } // CBZ/CBNZ, imm19
    inline bool IsTbz(UINT32 i)        { return (i & 0x7E000000) == 0x36000000; } // TBZ/TBNZ, imm14
    inline bool IsBCond(UINT32 i)      { return (i & 0xFF000000) == 0x54000000; } // B.cond/BC.cond, imm19
    inline bool IsHint(UINT32 i)       { return (i & 0xFFFFF01F) == 0xD503201F; } // NOP, PACIBSP, BTI, ...
    inline bool IsBranchSystem(UINT32 i) { return (i & 0x1C000000) == 0x14000000; } // branches, system
    inline bool IsAdr(UINT32 i)        { return (i & 0x1F000000) == 0x10000000; } // ADR/ADRP
    inline bool IsLiteralLoad(UINT32 i) { return (i & 0x3B000000) == 0x18000000; } // LDR/LDRSW/PRFM (literal)

    /*
     * If the displaced instruction changes SP or LR, a stack walk (by a debugger or a profiler) that
     * pauses a thread in the trampoline needs unwind data for it; otherwise the trampoline is taken
     * for a leaf function, and SP or LR are not restored. The trampoline is registered with
     * RtlAddFunctionTable as a 2-instruction function whose prolog is the displaced instruction.
     *
     * The unwind data can't be copied from the original function as on x64: on ARM64 the function
     * length is part of the unwind data, so the copy would claim the trampoline is as long as the
     * original function and cover the neighboring trampolines. So the displaced instruction is
     * translated to its unwind code, which is supported for the instructions that start prologs:
     * - pacibsp, which signs LR in builds with pointer authentication: _aligned_msize in
     *   ucrtbase.dll 10.0.28000 starts with it. The other functions replaced with a trampoline
     *   there start with B, CBZ or MOV, which need no unwind data.
     * - stp x29, x30, [sp, #-n]!, which saves the frame pointer and LR in other builds.
     * - sub sp, sp, #n (up to 32 KB), which allocates the stack frame.
     * Other instructions get no unwind data, as before this support: only stack walks paused in
     * the trampoline are affected, while refusing the instruction would skip the replacement.
     *
     * References:
     * - "ARM64 exception handling", https://learn.microsoft.com/cpp/build/arm64-exception-handling:
     *   ".xdata records" for the header (IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY_XDATA in winnt.h) and
     *   "Unwind codes" for the codes, which are not defined in the Windows SDK headers.
     * - Arm A64 Instruction Set Architecture (DDI 0602), https://developer.arm.com/documentation/ddi0602:
     *   the encodings of STP, SUB (immediate) and PACIBSP.
     */

    const UINT32 PACIBSP = 0xD503237F;

    // Unwind codes, named as in the "Unwind codes" table of "ARM64 exception handling"
    const UCHAR UWC_ALLOC_S     = 0x00; // 000xxxxx: sub sp, sp, #x*16
    const UCHAR UWC_SAVE_FPLR_X = 0x80; // 10zzzzzz: stp x29, lr, [sp, #-(z+1)*8]!
    const UCHAR UWC_ALLOC_M     = 0xC0; // 11000xxx'xxxxxxxx: sub sp, sp, #x*16
    const UCHAR UWC_END         = 0xE4;
    const UCHAR UWC_PAC_SIGN_LR = 0xFC; // pacibsp

    // Write the unwind code for an instruction that changes SP or LR.
    // RETURN: the size of the code, or 0 if the instruction has no unwind code here
    inline unsigned UnwindCode(UINT32 i, UCHAR code[2]) {
        if (i == PACIBSP) {
            code[0] = UWC_PAC_SIGN_LR;
            return 1;
        }
        if ((i & 0xFFC07FFF) == 0xA9807BFD) {                  // stp x29, x30, [sp, #-n]!
            const INT64 size = -SignExtend(i >> 15, 7) * 8;
            if (size <= 0)
                return 0;
            code[0] = (UCHAR)(UWC_SAVE_FPLR_X | (size / 8 - 1));
            return 1;
        }
        if ((i & 0xFF8003FF) == 0xD10003FF) {                  // sub sp, sp, #n
            const UINT32 size = ((i >> 10) & 0xFFF) << ((i & 0x00400000) ? 12 : 0);
            const UINT32 x = size / 16;
            if (size % 16 || x >= 0x800)
                return 0;
            if (x < 0x20) {
                code[0] = (UCHAR)(UWC_ALLOC_S | x);
                return 1;
            }
            code[0] = (UCHAR)(UWC_ALLOC_M | (x >> 8));
            code[1] = (UCHAR)x;
            return 2;
        }
        return 0;
    }


    inline Kind Classify(UINT32 i) {
        if (IsB(i))
            return BRANCH;
        if (IsCbz(i) || IsTbz(i) || IsBCond(i))
            return COND_BRANCH;
        if (IsHint(i))
            return PLAIN;
        if (IsBranchSystem(i)) // BL, BR, BLR, RET, exception and system instructions
            return UNSUPPORTED;
        if (IsAdr(i))
            return ADDRESS;
        if (IsLiteralLoad(i))
            return UNSUPPORTED;

        return PLAIN;
    }

    inline UINT_PTR BranchTarget(UINT_PTR pc, UINT32 i) {
        return pc + (UINT_PTR)(SignExtend(i, 26) * 4);
    }
    inline UINT_PTR CondBranchTarget(UINT_PTR pc, UINT32 i) {
        return pc + (UINT_PTR)(SignExtend(i >> 5, IsTbz(i) ? 14 : 19) * 4);
    }
    // The same conditional branch with its offset set to `words` instructions
    inline UINT32 RetargetCondBranch(UINT32 i, UINT32 words) {
        const UINT32 mask = IsTbz(i) ? 0x3FFFu : 0x7FFFFu;
        return (i & ~(mask << 5)) | ((words & mask) << 5);
    }
    inline UINT_PTR AdrValue(UINT_PTR pc, UINT32 i) {
        const UINT64 imm = ((i >> 29) & 0x3) | ((UINT64)((i >> 5) & 0x7FFFF) << 2);
        if (i & 0x80000000) // ADRP
            return (pc & ~(UINT_PTR)0xFFF) + (UINT_PTR)(SignExtend(imm, 21) * 4096);
        return pc + (UINT_PTR)SignExtend(imm, 21);
    }
    inline UINT32 Branch(UINT_PTR from, UINT_PTR to) {
        return 0x14000000 | (UINT32)(((INT64)(to - from) >> 2) & 0x3FFFFFF);
    }
    inline UINT32 LdrLiteral64(UINT32 rt, UINT32 words) {
        return 0x58000000 | ((words & 0x7FFFF) << 5) | (rt & 0x1F);
    }

    // Get a location within +-128 MB of `near_addr` that can hold code
    inline UINT32* GetCodeLocation(UINT_PTR near_addr) {
        const UINT_PTR loc = memProvider.GetLocation(near_addr);
        if (!loc)
            return nullptr;
        DWORD prot = 0;
        if (!VirtualProtect(Addrint2Ptr(loc), MAX_PROBE_SIZE, PAGE_EXECUTE_READWRITE, &prot))
            return nullptr;
        return (UINT32*)Addrint2Ptr(loc);
    }

    // Unwind data for a trampoline of two instructions (the displaced one and a branch back),
    // placed after them in the same slot
    struct TrampolineUnwind {
        RUNTIME_FUNCTION entry;
        UINT32 header;
        UCHAR codes[4]; // the unwind code of the displaced instruction, then UWC_END
    };

    inline void RegisterTrampoline(UINT32* code, UINT32 insn) {
        TrampolineUnwind* data = (TrampolineUnwind*)(code + 2);
        memset(data->codes, UWC_END, sizeof(data->codes));
        if (UnwindCode(insn, data->codes) == 0)
            return; // no stack or LR change, or one without an unwind code here
        const UINT_PTR base = Ptr2Addrint(code);
        IMAGE_ARM64_RUNTIME_FUNCTION_ENTRY_XDATA header = {};
        header.FunctionLength = 2; // instructions
        header.CodeWords = 1;
        data->header = header.HeaderData;
        data->entry.BeginAddress = 0;
        data->entry.UnwindData = (DWORD)(Ptr2Addrint(&data->header) - base);
        RtlAddFunctionTable(&data->entry, 1, base);
    }

    // Build a trampoline that runs the instruction displaced from `src` and continues at src + 4
    inline void* MakeTrampoline(UINT_PTR src, UINT32 insn) {
        UINT32* code = GetCodeLocation(src);
        if (!code)
            return nullptr;
        const UINT_PTR t = Ptr2Addrint(code);
        UINT_PTR value = 0;
        switch (Classify(insn)) {
        case PLAIN:
            code[0] = insn;
            code[1] = Branch(t + 4, src + SIZE_OF_ARM64_INSN);
            RegisterTrampoline(code, insn);
            break;
        case COND_BRANCH:
            // Taken: jump to the absolute branch at +8. Not taken: continue the function.
            value = CondBranchTarget(src, insn);
            code[0] = RetargetCondBranch(insn, 2);
            code[1] = Branch(t + 4, src + SIZE_OF_ARM64_INSN);
            code[2] = LDR_X16_PLUS8;
            code[3] = BR_X16;
            memcpy(&code[4], &value, sizeof(value));
            break;
        case ADDRESS:
            value = AdrValue(src, insn);
            code[0] = LdrLiteral64(insn & 0x1F, 2);
            code[1] = Branch(t + 4, src + SIZE_OF_ARM64_INSN);
            memcpy(&code[2], &value, sizeof(value));
            break;
        default:
            return nullptr;
        }
        FlushInstructionCache(GetCurrentProcess(), code, MAX_PROBE_SIZE);
        return code;
    }
} // namespace Arm64

// Replace the first instruction at inpAddr with a branch to targetAddr.
// RETURN: false on failure, true on success
static bool InsertTrampolineArm64(void *inpAddr, void *targetAddr, void** storedAddr)
{
    const UINT_PTR srcAddr = Ptr2Addrint(inpAddr);
    UINT_PTR destAddr = Ptr2Addrint(targetAddr);
    UINT32 insn;
    memcpy(&insn, inpAddr, sizeof(insn));

    if ( storedAddr ){ // If requested, keep a way to call the original function
        if ( Arm64::Classify(insn) == Arm64::BRANCH ) {
            // For the special case when a system function consists of a single branch,
            // instead of moving it somewhere we use the target of the branch as the original function.
            *storedAddr = Addrint2Ptr(Arm64::BranchTarget(srcAddr, insn));
        } else {
            *storedAddr = Arm64::MakeTrampoline(srcAddr, insn);
            if (!*storedAddr)
                return false;
        }
    }

    if (!IsInDistance(srcAddr, destAddr, MAX_DISTANCE)) {
        // Too far for B: go through a probe that does an absolute jump
        UINT32* probe = Arm64::GetCodeLocation(srcAddr);
        if (!probe)
            return false;
        probe[0] = Arm64::LDR_X16_PLUS8;
        probe[1] = Arm64::BR_X16;
        memcpy(&probe[2], &destAddr, sizeof(destAddr));
        FlushInstructionCache(GetCurrentProcess(), probe, MAX_PROBE_SIZE);
        destAddr = Ptr2Addrint(probe);
    }

    // A single aligned 4-byte store: other threads see either the old or the new instruction.
    InterlockedExchange((volatile LONG*)inpAddr, (LONG)Arm64::Branch(srcAddr, destAddr));
    return true;
}

#endif // _M_ARM64

// Insert a jump instruction in the inpAddr to the targetAddr
// 1. Get the memory protection of the page containing the input address
// 2. Change the memory protection to writable
// 3. Call InsertTrampoline32 or InsertTrampoline64
// 4. Restore memory protection
// RETURN: FALSE on failure, TRUE on success
static bool InsertTrampoline(void *inpAddr, void *targetAddr, const char ** opcodes, void** origFunc)
{
    DWORD probeSize;
    // Change page protection to EXECUTE+WRITE
    DWORD origProt = 0;
    if (!VirtualProtect(inpAddr, MAX_PROBE_SIZE, PAGE_EXECUTE_WRITECOPY, &origProt))
        return FALSE;

#if defined(_M_ARM64)
    (void)opcodes; // not used: the displaced instruction is relocated instead
    const bool replaced = InsertTrampolineArm64(inpAddr, targetAddr, origFunc);
    probeSize = SIZE_OF_ARM64_INSN;
#else
    const char* pattern = nullptr;
    if ( origFunc ){ // Need to store original function code
        UCHAR * const codePtr = (UCHAR *)inpAddr;
        if ( *codePtr == 0xE9 ){ // JMP relative instruction
            // For the special case when a system function consists of a single near jump,
            // instead of moving it somewhere we use the target of the jump as the original function.
            // The offset is signed: the jump can go backwards.
            int offsetInJmp = *(int*)(codePtr + 1);
            *origFunc = (void*)(Ptr2Addrint(inpAddr) + (INT_PTR)offsetInJmp + SIZE_OF_RELJUMP);
            origFunc = nullptr; // now it must be ignored by InsertTrampoline32/64
        } else {
            // find the right opcode pattern
            UINT opcodeIdx = CheckOpcodes( opcodes, inpAddr, /*abortOnError=*/true );
            __TBB_ASSERT( opcodeIdx > 0, "abortOnError ignored in CheckOpcodes?" );
            pattern = opcodes[opcodeIdx-1];  // -1 compensates for +1 in CheckOpcodes
        }
    }

    probeSize = InsertTrampoline32(inpAddr, targetAddr, pattern, origFunc);
    if (!probeSize)
        probeSize = InsertTrampoline64(inpAddr, targetAddr, pattern, origFunc);
    const bool replaced = probeSize != 0;
#endif

    // Restore original protection
    VirtualProtect(inpAddr, MAX_PROBE_SIZE, origProt, &origProt);

    if (!replaced)
        return FALSE;

    FlushInstructionCache(GetCurrentProcess(), inpAddr, probeSize);
#if !defined(_M_ARM64) // the ARM64 trampolines are flushed when they are made
    FlushInstructionCache(GetCurrentProcess(), origFunc, probeSize);
#endif

    return TRUE;
}

// Routine to replace the functions
// TODO: replace opcodesNumber with opcodes and opcodes number to check if we replace right code.
FRR_TYPE ReplaceFunctionA(const char *dllName, const char *funcName, FUNCPTR newFunc, const char ** opcodes, FUNCPTR* origFunc)
{
    // Cache the results of the last search for the module
    // Assume that there was no DLL unload between
    static char cachedName[MAX_PATH+1];
    static HMODULE cachedHM = 0;

    if (!dllName || !*dllName)
        return FRR_NODLL;

    if (!cachedHM || strncmp(dllName, cachedName, MAX_PATH) != 0)
    {
        // Find the module handle for the input dll
        HMODULE hModule = GetModuleHandleA(dllName);
        if (hModule == 0)
        {
            // Couldn't find the module with the input name
            cachedHM = 0;
            return FRR_NODLL;
        }

        cachedHM = hModule;
        strncpy(cachedName, dllName, MAX_PATH);
    }

    FARPROC inpFunc = GetProcAddress(cachedHM, funcName);
    if (inpFunc == 0)
    {
        // Function was not found
        return FRR_NOFUNC;
    }

    if (!InsertTrampoline((void*)inpFunc, (void*)newFunc, opcodes, (void**)origFunc)){
        // Failed to insert the trampoline to the target address
        return FRR_FAILED;
    }

    return FRR_OK;
}

FRR_TYPE ReplaceFunctionW(const wchar_t *dllName, const char *funcName, FUNCPTR newFunc, const char ** opcodes, FUNCPTR* origFunc)
{
    // Cache the results of the last search for the module
    // Assume that there was no DLL unload between
    static wchar_t cachedName[MAX_PATH+1];
    static HMODULE cachedHM = 0;

    if (!dllName || !*dllName)
        return FRR_NODLL;

    if (!cachedHM || wcsncmp(dllName, cachedName, MAX_PATH) != 0)
    {
        // Find the module handle for the input dll
        HMODULE hModule = GetModuleHandleW(dllName);
        if (hModule == 0)
        {
            // Couldn't find the module with the input name
            cachedHM = 0;
            return FRR_NODLL;
        }

        cachedHM = hModule;
        wcsncpy(cachedName, dllName, MAX_PATH);
    }

    FARPROC inpFunc = GetProcAddress(cachedHM, funcName);
    if (inpFunc == 0)
    {
        // Function was not found
        return FRR_NOFUNC;
    }

    if (!InsertTrampoline((void*)inpFunc, (void*)newFunc, opcodes, (void**)origFunc)){
        // Failed to insert the trampoline to the target address
        return FRR_FAILED;
    }

    return FRR_OK;
}

bool IsPrologueKnown(const char* dllName, const char *funcName, const char **opcodes, HMODULE module)
{
    FARPROC inpFunc = GetProcAddress(module, funcName);
    FunctionInfo functionInfo = { funcName, dllName };

    if (!inpFunc) {
        Log::record(functionInfo, "unknown", /*status*/ false);
        return false;
    }

#if defined(_M_ARM64)
    (void)opcodes;
    UINT32 insn;
    memcpy(&insn, (void*)inpFunc, sizeof(insn));
    char opcodeString[2*sizeof(insn)+1];
    LOG_PRINT(opcodeString, sizeof(opcodeString), "%08X", (unsigned)insn);
    const bool known = Arm64::Classify(insn) != Arm64::UNSUPPORTED;
    Log::record(functionInfo, opcodeString, known);
    return known;
#else
    return CheckOpcodes( opcodes, (void*)inpFunc, /*abortOnError=*/false, &functionInfo) != 0;
#endif
}

// Public Windows API
extern "C" __declspec(dllexport) int TBB_malloc_replacement_log(char *** function_replacement_log_ptr)
{
    if (function_replacement_log_ptr != nullptr) {
        *function_replacement_log_ptr = Log::records;
    }

    // If we have no logs -> return false status
    return Log::replacement_status && Log::records[0] != nullptr ? 0 : -1;
}

#endif /* !__TBB_WIN8UI_SUPPORT && defined(_WIN32) */
