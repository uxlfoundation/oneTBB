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

#include <sys/mman.h>

static bool mock_madvise_called = false;
static bool mock_move_pages_called = false;
static bool madvise_should_fail = true;

static bool mock_mmap_called = false;
static bool mmap_should_fail = false;

// Defined before the macro below so it can dispatch to the real mmap when not failing.
static void* mock_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset) {
    mock_mmap_called = true;
    if (mmap_should_fail)
        return MAP_FAILED;
    return mmap(addr, length, prot, flags, fd, offset);
}

#define madvise(addr, length, advice) mock_madvise(addr, length, advice)
#define mmap(addr, length, prot, flags, fd, offset) mock_mmap(addr, length, prot, flags, fd, offset)

extern "C" {
static int mock_madvise(void*, size_t, int) noexcept (true) {
    mock_madvise_called = true;
    return madvise_should_fail ? -1 : 0;
}
}
#elif _WIN32 || _WIN64

#include <map>
#include <memory>

static struct {
    std::unique_ptr<char[]> storage;
    // allocate_interleaved() split allocated space into multiple regions by VirtualFree() calls,
    // need to track them for verification
    // key: address of the region, value: size of the region
    std::map<char*, size_t> regions;
    bool make_reservation_fail = false;
    size_t fail_split = 0;
    size_t fail_replacement = 0;
    size_t split_calls = 0;
    size_t replacement_calls = 0;
    size_t release_calls = 0;
} mock_state;

static bool mock_VirtualAllocEx_called = false;
static bool VirtualAllocEx_should_fail = false;

BOOL mock_VirtualFree(LPVOID lpAddress, SIZE_T dwSize, DWORD dwFreeType) {
    char* address = static_cast<char*>(lpAddress);
    auto region = mock_state.regions.find(address);
    REQUIRE(region != mock_state.regions.end());
    if (dwFreeType == (MEM_RELEASE | MEM_PRESERVE_PLACEHOLDER)) {
        REQUIRE(dwSize > 0);
        REQUIRE(dwSize < region->second);
        if (++mock_state.split_calls == mock_state.fail_split)
            return FALSE;

        // Splitting creates a new region starting at address + dwSize.
        // Ensure we do not silently overwrite an existing region entry.
        auto insert_result = mock_state.regions.emplace(address + dwSize, region->second - dwSize);
        REQUIRE(insert_result.second);

        region->second = dwSize;
    } else {
        REQUIRE(dwFreeType == MEM_RELEASE);
        REQUIRE(dwSize == 0);
        mock_state.regions.erase(region);
        ++mock_state.release_calls;
    }
    return TRUE;
}

LPVOID mock_VirtualAllocEx(HANDLE hProcess, LPVOID lpAddress, SIZE_T dwSize,
                           DWORD flAllocationType, DWORD flProtect) {
    mock_VirtualAllocEx_called = true;
    if (VirtualAllocEx_should_fail)
        return nullptr;
    return VirtualAllocEx(hProcess, lpAddress, dwSize, flAllocationType, flProtect);
}

#define VirtualFree mock_VirtualFree
#define VirtualAllocEx mock_VirtualAllocEx

#endif

#include "../../src/tbb/numa_allocation.cpp"

#undef madvise
#undef mmap
#undef VirtualFree
#undef VirtualAllocEx

namespace tbb {
namespace detail {
namespace r1 {

#if __linux__

static long mock_move_pages(int, unsigned long, void**, const int*, int*, int) {
    mock_move_pages_called = true;
    return -1;
}

static struct bitmask* mock_numa_bitmask_alloc(unsigned int) {
    return nullptr;
}

static void mock_numa_bitmask_free(struct bitmask*) {}

static int mock_numa_bitmask_isbitset(const struct bitmask*, unsigned int) {
    return 1;
}

static struct bitmask* mock_numa_bitmask_setbit(struct bitmask* m, unsigned int) {
    return m;
}

static void mock_numa_interleave_memory(void*, size_t, struct bitmask*) {}

#elif _WIN32 || _WIN64

static void* mock_VirtualAlloc2_ptr(HANDLE, PVOID BaseAddress, SIZE_T Size,
                                     ULONG AllocationType, ULONG,
                                     MEM_EXTENDED_PARAMETER *, ULONG) {
    if (AllocationType == (MEM_RESERVE | MEM_RESERVE_PLACEHOLDER)) {
        REQUIRE(BaseAddress == nullptr);
        if (mock_state.make_reservation_fail)
            return nullptr;
        mock_state.storage.reset(new char[Size]);
        mock_state.regions[mock_state.storage.get()] = Size;
        return mock_state.storage.get();
    }
    REQUIRE(AllocationType == (MEM_RESERVE | MEM_COMMIT | MEM_REPLACE_PLACEHOLDER));
    auto region = mock_state.regions.find(static_cast<char*>(BaseAddress));
    REQUIRE(region != mock_state.regions.end());
    REQUIRE(region->second == Size);
    if (++mock_state.replacement_calls == mock_state.fail_replacement)
        return nullptr;
    return BaseAddress;
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
    move_pages_ptr = mock_move_pages;
    numa_bitmask_alloc_ptr = mock_numa_bitmask_alloc;
    numa_bitmask_free_ptr = mock_numa_bitmask_free;
    numa_bitmask_isbitset_ptr = mock_numa_bitmask_isbitset;
    numa_bitmask_setbit_ptr = mock_numa_bitmask_setbit;
    numa_interleave_memory_ptr = mock_numa_interleave_memory;
#elif _WIN32 || _WIN64
    VirtualAlloc2_ptr = mock_VirtualAlloc2_ptr;
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
//! Testing correct behavior of allocate_interleaved if syscall fails
//! \brief \ref error_guessing
TEST_CASE("allocate_interleaved with failed syscall") {
    // use non-default value to not call numa_interleave_memory under Linux
    size_t per_chunk = 2*4*1024;
    tbb::detail::d1::numa_node_id nodes_ids_array[] = {0, 0};
    tbb::detail::d1::numa_node_id *nodes_ids = nodes_ids_array;

#if __linux__
    size_t size = 1024;
    // make madvise failed
    madvise_should_fail = true;
    mock_madvise_called = false;
    void *ptr = tbb::detail::r1::allocate_interleaved(size, nodes_ids, 2, per_chunk);
    REQUIRE(ptr == nullptr);
    REQUIRE_MESSAGE(mock_madvise_called, "Failed madvise syscall was not called");

    // madvise is expected to not fail, move_pages_ptr is failing
    madvise_should_fail = false;
    mock_move_pages_called = false;
    ptr = tbb::detail::r1::allocate_interleaved(size, nodes_ids, 2, per_chunk);
    REQUIRE(ptr == nullptr);
    REQUIRE_MESSAGE(mock_move_pages_called, "Failed move_pages syscall was not called");

    // mmap is expected to fail
    mock_mmap_called = false;
    mmap_should_fail = true;
    ptr = tbb::detail::r1::allocate_interleaved(size, nodes_ids, 2, per_chunk);
    REQUIRE(ptr == nullptr);
    REQUIRE_MESSAGE(mock_mmap_called, "Failed mmap syscall was not called");
    mmap_should_fail = false;
#elif _WIN32 || _WIN64
    const struct {
        size_t bytes;
        bool fail_reservation;
        size_t fail_split;
        size_t fail_replacement;
        size_t expected_releases;
    } cases[] = {
        {per_chunk, true, 0, 0, 0},
        {per_chunk / 2, false, 0, 1, 1},
        {4*per_chunk, false, 1, 0, 1},
        {4*per_chunk, false, 3, 0, 3},
        {4*per_chunk, false, 0, 1, 2},
        {4*per_chunk, false, 0, 3, 4},
        {4*per_chunk, false, 0, 4, 4},
        {3*per_chunk + per_chunk / 2, false, 0, 4, 4},
        {3*per_chunk + per_chunk / 2, false, 0, 0, 0}
    };

    try {
        for (const auto &test_case : cases) {
            CAPTURE(test_case.bytes);
            CAPTURE(test_case.fail_split);
            CAPTURE(test_case.fail_replacement);
            mock_state = {};
            mock_state.make_reservation_fail = test_case.fail_reservation;
            mock_state.fail_split = test_case.fail_split;
            mock_state.fail_replacement = test_case.fail_replacement;

            void *ptr = tbb::detail::r1::allocate_interleaved(test_case.bytes, nodes_ids, 2, per_chunk);
            if (test_case.fail_reservation || test_case.fail_split || test_case.fail_replacement)
            {
                REQUIRE(ptr == nullptr);
                REQUIRE(mock_state.regions.empty());
            }
            else
            {
                REQUIRE(ptr == mock_state.storage.get());
                REQUIRE(mock_state.regions.size() == 4);
            }
            REQUIRE(mock_state.release_calls == test_case.expected_releases);
            if (test_case.fail_split)
                REQUIRE(mock_state.split_calls == test_case.fail_split);
            if (test_case.fail_replacement)
                REQUIRE(mock_state.replacement_calls == test_case.fail_replacement);
        }
        mock_state = {};

        VirtualAllocEx_should_fail = true;
        mock_VirtualAllocEx_called = false;
        // make it null to use VirtualAllocEx
        tbb::detail::r1::VirtualAlloc2_ptr = nullptr;
        void *ptr = tbb::detail::r1::allocate_interleaved(per_chunk / 2, nodes_ids, 1, per_chunk);
        REQUIRE(ptr == nullptr);
        REQUIRE_MESSAGE(mock_VirtualAllocEx_called, "Failed VirtualAllocEx syscall was not called");
    } catch (doctest::detail::TestFailureException) { /* catch it to restore the state */ }
    VirtualAllocEx_should_fail = false;
    tbb::detail::r1::VirtualAlloc2_ptr = tbb::detail::r1::mock_VirtualAlloc2_ptr;
#endif // _WIN32 || _WIN64
}
#endif //  __linux__ || _WIN32 || _WIN64
