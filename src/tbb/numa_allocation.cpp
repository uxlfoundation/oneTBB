/*
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

#include "governor.h"
#include "dynamic_link.h"

#define TBB_PREVIEW_NUMA_ALLOCATION 1
#include "oneapi/tbb/numa_allocation.h"

#include <atomic>
#include <memory> // for std::unique_ptr

#if __linux__

#include <sys/mman.h>

// must support build without numaif.h
extern "C" long move_pages(int pid, unsigned long count,
                           void **pages, const int *nodes, int *status, int flags);

#elif _WIN32 || _WIN64

template<typename T>
static inline T alignUp(T arg, uintptr_t alignment) {
    return T(((uintptr_t)arg+(alignment-1)) & ~(alignment-1));
}
#else

#include <stdlib.h> // for malloc and free

#endif

namespace tbb {
namespace detail {
namespace r1 {

#if __TBB_WEAK_SYMBOLS_PRESENT
#pragma weak move_pages
#endif /* __TBB_WEAK_SYMBOLS_PRESENT */

static std::atomic<do_once_state> interleaved_initialization_state;

#if __linux__
static long (*move_pages_ptr)(int pid, unsigned long count,
             void **pages, const int *nodes, int *status, int flags);

static const dynamic_link_descriptor LibnumaLinkTable[] = {
    DLD(move_pages, move_pages_ptr)
};
#elif _WIN32 || _WIN64
static PVOID (*VirtualAlloc2_ptr)(HANDLE Process,
  PVOID                  BaseAddress,
  SIZE_T                 Size,
  ULONG                  AllocationType,
  ULONG                  PageProtection,
  MEM_EXTENDED_PARAMETER *ExtendedParameters,
  ULONG                  ParameterCount
);

static const dynamic_link_descriptor LibnumaLinkTable[] = {
    DLD(VirtualAlloc2, VirtualAlloc2_ptr)
};
#endif /* __linux__ */

void interleaved_initialization_impl() {
#if __linux__
    dynamic_link("libnuma.so", LibnumaLinkTable,
                 sizeof(LibnumaLinkTable) / sizeof(dynamic_link_descriptor), nullptr,
                 DYNAMIC_LINK_GLOBAL | DYNAMIC_LINK_LOAD | DYNAMIC_LINK_WEAK);
#elif _WIN32 || _WIN64
    dynamic_link("kernelbase.dll", LibnumaLinkTable,
                 sizeof(LibnumaLinkTable) / sizeof(dynamic_link_descriptor));
#endif
}

bool verify_args(size_t bytes, const tbb::detail::d1::numa_node_id *nodes_ids, size_t nodes_count,
                 size_t bytes_per_chunk) {
    if (bytes == 0) // to be consistent with mmap
        return false;
    if (bytes_per_chunk % governor::default_page_size() != 0)
        return false;
    if ((nodes_ids == nullptr && nodes_count != 0) || (nodes_ids != nullptr && nodes_count == 0))
        return false;
    return true;
}

// interleaved memory allocation is only supported for those platforms
#if __linux__ || _WIN32 || _WIN64

void *__TBB_EXPORTED_FUNC allocate_interleaved(size_t bytes,
                        const tbb::detail::d1::numa_node_id *nodes_ids, size_t nodes_count,
                        size_t bytes_per_chunk) {
    atomic_do_once(interleaved_initialization_impl, interleaved_initialization_state);

    if (!verify_args(bytes, nodes_ids, nodes_count, bytes_per_chunk))
        return nullptr;

    if (!bytes_per_chunk)
        bytes_per_chunk = governor::default_page_size();
    const int *nodes = nodes_count? nodes_ids : get_numa_nodes_indexes();
    if (!nodes_count)
        nodes_count = numa_node_count();

#if __linux__
    char *base_addr = reinterpret_cast<char*>(
        mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
    if (base_addr == MAP_FAILED)
        return nullptr;

    auto unmap = [bytes](void* ptr) {
        munmap(ptr, bytes);
    };
    std::unique_ptr<void, decltype(unmap)> data_holder(base_addr, unmap);

    // touch each page, otherwise move_pages() will fail with EFAULT
    for (size_t i = 0; i < bytes; i += governor::default_page_size())
        base_addr[i] = 0;

    // no NUMA nodes or move_pages() not available, just return the memory as is
    if (numa_node_count() == 1 || !move_pages_ptr)
        return data_holder.release();

    size_t count_pages = (bytes + governor::default_page_size() - 1) / governor::default_page_size();
    std::unique_ptr<void*[]> pages(new void*[count_pages]);
    std::unique_ptr<int[]> nodes_per_page(new int[count_pages]);
    std::unique_ptr<int[]> status(new int[count_pages]);

    char *end_ptr = base_addr + bytes;
    // move_pages() has no length parameter, so moving must be done per page
    for (char *ptr = base_addr; ptr < end_ptr; ptr += governor::default_page_size()) {
        size_t page_idx = (ptr - base_addr) / governor::default_page_size();
        size_t stride_idx = (ptr - base_addr) / bytes_per_chunk;
        pages[page_idx] = ptr;
        nodes_per_page[page_idx] = nodes[stride_idx % nodes_count];
    }
    long ret = move_pages_ptr(0, count_pages, pages.get(), nodes_per_page.get(), status.get(), 0);
    if (ret < 0)
        return nullptr;

    for (size_t i = 0; i < count_pages; ++i)
        if (status[i] < 0)
            return nullptr;

    return data_holder.release();
#elif _WIN32 || _WIN64
    // no NUMA nodes or no VirtualAlloc2, just return the memory as is
    if (numa_node_count() == 1 || !VirtualAlloc2_ptr)
        // do not use VirtualAlloc(), because it compiled incorrectly by MSVC 2017 with
        // -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug -DTBB_WINDOWS_DRIVER=ON
        return VirtualAllocEx(GetCurrentProcess(), nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

    // for VirtualAlloc2 it must be a multiple of the page size
    bytes = alignUp(bytes, governor::default_page_size());
    char* base_addr =
        static_cast<char*>(VirtualAlloc2_ptr(nullptr, nullptr, bytes, MEM_RESERVE | MEM_RESERVE_PLACEHOLDER,
                                             PAGE_NOACCESS, nullptr, 0));
    if (!base_addr)
        return nullptr;

     auto unmap = [](char* base_addr) {
        VirtualFree(base_addr, 0, MEM_RELEASE);
    };
    std::unique_ptr<char, decltype(unmap)> data_holder(base_addr, unmap);

    // commit pages round-robin across nodes
    for (size_t node_index = 0, curr_size = 0; curr_size < bytes;
         curr_size += bytes_per_chunk, ++node_index) {
        // must release every but last page
        if (curr_size + bytes_per_chunk < bytes) {
            BOOL ok = VirtualFree(base_addr + curr_size, bytes_per_chunk,
                                  MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER);
            if (!ok)
                return nullptr;
        }

        MEM_EXTENDED_PARAMETER param = { {0, 0}, {0} };

        param.Type = MemExtendedParameterNumaNode;
        // preferred node
        param.ULong = nodes[node_index % nodes_count];

        // commit the pages to the preferred node
        size_t chunk_size = min(bytes_per_chunk, bytes - curr_size);
        __TBB_ASSERT(chunk_size % governor::default_page_size() == 0,
                     "chunk_size is a multiple of the page size, because bytes is aligned to page size "
                     "and bytes_per_chunk is a multiple of page size");
        PVOID result = VirtualAlloc2_ptr(nullptr, base_addr + curr_size, chunk_size,
                                         MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER, PAGE_READWRITE,
                                         &param, 1);

        if (!result)
            return nullptr;
    }

    return data_holder.release();
#endif
}
#else /* __linux__ || _WIN32 || _WIN64 */

// fallback implementation with malloc/free
void *__TBB_EXPORTED_FUNC allocate_interleaved(size_t bytes,
                        const tbb::detail::d1::numa_node_id *nodes_ids, size_t nodes_count,
                        size_t bytes_per_chunk) {
    return verify_args(bytes, nodes_ids, nodes_count, bytes_per_chunk) ?
        calloc(bytes, 1) : nullptr;
}

#endif /* __linux__ || _WIN32 || _WIN64 */


void __TBB_EXPORTED_FUNC deallocate_interleaved(void *ptr, size_t bytes) {
    atomic_do_once(interleaved_initialization_impl, interleaved_initialization_state);

    // TODO: process return value of munmap()/VirtualFree()
#if __linux__
    munmap(ptr, bytes);
#elif _WIN32 || _WIN64
    (void)bytes;
    VirtualFree(ptr, 0, MEM_RELEASE);
#else
    (void)bytes;
    free(ptr);
#endif
}

} // namespace r1
} // namespace detail
} // namespace tbb
