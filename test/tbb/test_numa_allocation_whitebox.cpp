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

//! \file test_numa_allocation_whitebox.cpp
//! \brief Test for [internal] functionality

#define __TBB_NO_IMPLICIT_LINKAGE 1
#define TBB_PREVIEW_NUMA_ALLOCATION 1

#include "common/test.h"

#if __linux__

static bool overrided_madvise_called = false;
static bool overrided_move_pages_called = false;
static bool madvise_should_fail = true;

#define madvise(addr, length, advice) failed_madvise(addr, length, advice)

extern "C" {
static int failed_madvise(void*, size_t, int) noexcept (true) {
    overrided_madvise_called = true;
    return madvise_should_fail ? -1 : 0;
}
}
#elif _WIN32 || _WIN64

static bool overrided_VirtualAlloc2_ptr_failed = false;
static bool overrided_VirtualFree_failed = false;

BOOL failed_VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD  dwFreeType) {
    static int cnt;

    if (++cnt % 2) {
        return VirtualFree(lpAddress, dwSize, dwFreeType);
    } else {
        overrided_VirtualFree_failed = true;
        return FALSE;
    }
}

#define VirtualFree failed_VirtualFree

#endif

#include "../../src/tbb/numa_allocation.cpp"

#undef madvise
#undef VirtualFree

namespace tbb {
namespace detail {
namespace r1 {

#if __linux__

static long dummy_move_pages(int, unsigned long, void**, const int*, int*, int) {
    overrided_move_pages_called = true;
    return -1;
}

static struct bitmask* dummy_numa_bitmask_alloc(unsigned int) {
    return nullptr;
}

static void dummy_numa_bitmask_free(struct bitmask*) {}

static int dummy_numa_bitmask_isbitset(const struct bitmask*, unsigned int) {
    return 1;
}

static struct bitmask* dummy_numa_bitmask_setbit(struct bitmask* m, unsigned int) {
    return m;
}

static void dummy_numa_interleave_memory(void*, size_t, struct bitmask*) {}

#elif _WIN32 || _WIN64

static void* dummy_VirtualAlloc2_ptr(HANDLE, PVOID, SIZE_T Size,
                                     ULONG, ULONG,
                                     MEM_EXTENDED_PARAMETER *, ULONG) {
    static int cnt;

    if (++cnt % 2) {
        return VirtualAllocEx(GetCurrentProcess(), /*BaseAddress=*/nullptr, Size,
                              MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    } else {
        overrided_VirtualAlloc2_ptr_failed = true;
        return nullptr;
    }
}

#endif

void assertion_failure(const char*, int, const char*, const char*) {
    REQUIRE_MESSAGE(false, "Triggering TBB asserts during this test run is unexpected.");
}

bool dynamic_link( const char* ,
                   const tbb::detail::r1::dynamic_link_descriptor[],
                   std::size_t ,
                   tbb::detail::r1::dynamic_link_handle*,
                   int) {
#if __linux__
    move_pages_ptr = dummy_move_pages;
    numa_bitmask_alloc_ptr = dummy_numa_bitmask_alloc;
    numa_bitmask_free_ptr = dummy_numa_bitmask_free;
    numa_bitmask_isbitset_ptr = dummy_numa_bitmask_isbitset;
    numa_bitmask_setbit_ptr = dummy_numa_bitmask_setbit;
    numa_interleave_memory_ptr = dummy_numa_interleave_memory;
#elif _WIN32 || _WIN64
    VirtualAlloc2_ptr = dummy_VirtualAlloc2_ptr;
#endif
    return true;
}

const int* get_numa_nodes_indexes() {
    return nullptr;
}

unsigned numa_node_count() {
    return 2;
}

size_t DefaultSystemPageSize() {
    return 4*1024;
}

} // namespace r1
} // namespace detail
} // namespace tbb

#if __linux__ || _WIN32 || _WIN64
//! Testing correct behavior if syscall fails
//! \brief \ref error_guessing
TEST_CASE("test failed syscall") {
    // use non-default value to not call numa_interleave_memory under Linux
    size_t per_chunk = 2*4*1024;
    tbb::detail::d1::numa_node_id nodes_ids_array[] = {0, 0};
    tbb::detail::d1::numa_node_id *nodes_ids = nodes_ids_array;

#if __linux__
    size_t size = 1024;
    // make madvise failed
    madvise_should_fail = true;
    void *ptr = tbb::detail::r1::allocate_interleaved(size, nodes_ids, 2, per_chunk);
    REQUIRE(ptr == nullptr);
    REQUIRE_MESSAGE(overrided_madvise_called, "Failed madvise syscall was not called");

    // madvise is expected to not fail, move_pages_ptr is failing
    madvise_should_fail = false;
    overrided_move_pages_called = false;
    ptr = tbb::detail::r1::allocate_interleaved(size, nodes_ids, 2, per_chunk);
    REQUIRE(ptr == nullptr);
    REQUIRE_MESSAGE(overrided_move_pages_called, "Failed move_pages syscall was not called");
#elif _WIN32 || _WIN64
    // VirtualAlloc2_ptr is expected to fail, must use less than chunk size to start with call of
    // VirtualAlloc2_ptr in the committing loop
    void *ptr = tbb::detail::r1::allocate_interleaved(tbb::detail::r1::DefaultSystemPageSize() / 2,
                                                      nodes_ids, 2, per_chunk);
    REQUIRE(ptr == nullptr);
    REQUIRE_MESSAGE(overrided_VirtualAlloc2_ptr_failed, "Failed VirtualAlloc2 syscall was not called");

    // VirtualAlloc2_ptr is expected not to fail, VirtualFree in the committing loop is failing
    overrided_VirtualFree_failed = false;
    // must allocate more than chunk size to use VirtualFree in the committing loop
    ptr = tbb::detail::r1::allocate_interleaved(2*per_chunk, nodes_ids, 2, per_chunk);
    REQUIRE(ptr == nullptr);
    REQUIRE_MESSAGE(overrided_VirtualFree_failed, "Failed VirtualFree syscall was not called");
#endif
}
#endif
