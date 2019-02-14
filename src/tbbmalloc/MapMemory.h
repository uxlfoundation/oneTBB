/*
    Copyright (c) 2005-2018 Intel Corporation

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

#ifndef _itt_shared_malloc_MapMemory_H
#define _itt_shared_malloc_MapMemory_H

#include <stdlib.h>

void *ErrnoPreservingMalloc(size_t bytes)
{
    int prevErrno = errno;
    void *ret = malloc( bytes );
    if (!ret)
        errno = prevErrno;
    return ret;
}

#if __linux__ || __APPLE__ || __sun || __FreeBSD__

#if __sun && !defined(_XPG4_2)
 // To have void* as mmap's 1st argument
 #define _XPG4_2 1
 #define XPG4_WAS_DEFINED 1
#endif

#include <sys/mman.h>
#if __linux__
/* __TBB_MAP_HUGETLB is MAP_HUGETLB from system header linux/mman.h.
   The header is not included here, as on some Linux flavors inclusion of
   linux/mman.h leads to compilation error,
   while changing of MAP_HUGETLB is highly unexpected.
*/
#define __TBB_MAP_HUGETLB 0x40000
#else
#define __TBB_MAP_HUGETLB 0
#endif

#if XPG4_WAS_DEFINED
 #undef _XPG4_2
 #undef XPG4_WAS_DEFINED
#endif

#define MEMORY_MAPPING_USES_MALLOC 0
void* MapMemory (size_t bytes, bool hugePages)
{
    void* result = 0;
    int prevErrno = errno;
#ifndef MAP_ANONYMOUS
// macOS* defines MAP_ANON, which is deprecated in Linux*.
#define MAP_ANONYMOUS MAP_ANON
#endif /* MAP_ANONYMOUS */
    int addFlags = hugePages? __TBB_MAP_HUGETLB : 0;
    result = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|addFlags, -1, 0);
    if (result==MAP_FAILED)
        errno = prevErrno;
    return result==MAP_FAILED? 0: result;
}

int UnmapMemory(void *area, size_t bytes)
{
    int prevErrno = errno;
    int ret = munmap(area, bytes);
    if (-1 == ret)
        errno = prevErrno;
    return ret;
}

#elif (_WIN32 || _WIN64) && !__TBB_WIN8UI_SUPPORT
#include <windows.h>

bool useLargePages = false;

// #pragma comment(lib, "Advapi32.lib")
bool TryEnableLargePageSupport()
{
    // Large pages require memory locking privilege
    bool privilegeSuccess = false;
    TOKEN_PRIVILEGES priv;
    priv.PrivilegeCount = 1;

    DWORD error;
    if (!LookupPrivilegeValue(nullptr, SE_LOCK_MEMORY_NAME, &priv.Privileges[0].Luid))
    {
        error = GetLastError();
        //printf("LookupPrivilegeValue failed when enabling SE_LOCK_MEMORY_NAME privilege, 0x%x\n", error);
    }
    else
    {
        priv.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        HANDLE hToken;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY | TOKEN_ADJUST_PRIVILEGES, &hToken))
        {
            error = GetLastError();
            //WrtLog(IQCSLog::WarningLevel,
            //       str(format("[LargePageSupport] OpenProcessToken failed when enabling SE_LOCK_MEMORY_NAME privilege with error %1%") % error));
        }
        else
        {
            if (!AdjustTokenPrivileges(hToken, FALSE, &priv, 0, nullptr, nullptr))
            {
                error = GetLastError();
                //WrtLog(
                //    IQCSLog::WarningLevel,
                //    str(format("[LargePageSupport] AdjustTokenPrivileges failed when enabling SE_LOCK_MEMORY_NAME privilege with error %1%") % error));
            }
            // Adjusting privilege can fail even if AdjustTokenPrivileges returns true
            else if (GetLastError() != 0)
            {
                error = GetLastError();
                //WrtLog(
                //    IQCSLog::WarningLevel,
                //    str(format("[LargePageSupport] AdjustTokenPrivileges failed when enabling SE_LOCK_MEMORY_NAME privilege with error %1%") % error));
            }
            else
            {
                privilegeSuccess = true;
            }
            CloseHandle(hToken);
        }
    }

    return privilegeSuccess;
}

void initMapMemory()
{
    useLargePages = TryEnableLargePageSupport();
}

#define MEMORY_MAPPING_USES_MALLOC 0
void* MapMemory (size_t bytes, bool)
{
    // printf("VirtualAlloc %u bytes, use large pages = %u\n", bytes, useLargePages);
    /* Is VirtualAlloc thread safe? */
    DWORD allocationType = useLargePages ? MEM_LARGE_PAGES | MEM_RESERVE | MEM_COMMIT : MEM_RESERVE | MEM_COMMIT;


    return VirtualAlloc(NULL, bytes, allocationType, PAGE_READWRITE);
}

int UnmapMemory(void *area, size_t /*bytes*/)
{
    BOOL result = VirtualFree(area, 0, MEM_RELEASE);
    return !result;
}

#else

#define MEMORY_MAPPING_USES_MALLOC 1
void* MapMemory (size_t bytes, bool)
{
    return ErrnoPreservingMalloc( bytes );
}

int UnmapMemory(void *area, size_t /*bytes*/)
{
    free( area );
    return 0;
}

#endif /* OS dependent */

#if MALLOC_CHECK_RECURSION && MEMORY_MAPPING_USES_MALLOC
#error Impossible to protect against malloc recursion when memory mapping uses malloc.
#endif

#endif /* _itt_shared_malloc_MapMemory_H */
