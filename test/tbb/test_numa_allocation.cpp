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

#define TBB_PREVIEW_NUMA_ALLOCATION 1

#include "common/test.h"
#include "common/utils.h"
#include "common/utils_dynamic_libs.h"

#include "tbb/numa_allocation.h"

#if _WIN32 || _WIN64
#include <psapi.h>
#endif

#if __linux__
static long (*move_pages_ptr)(int pid, unsigned long count,
                void **pages, const int *nodes, int *status, int flags) = nullptr;
#endif

size_t DefaultSystemPageSize() {
#if _WIN32 || _WIN64
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return si.dwPageSize;
#else
    return sysconf(_SC_PAGESIZE);
#endif
}

int find_numa_node(void* addr) {
#if __linux__
    int numa_node = -1;
    int status[1];
    void* pages[1] = { addr };

    // Query which node owns this page
    if (move_pages_ptr(0, 1, pages, NULL, status, 0) == 0)
        numa_node = status[0];

    return numa_node;
#elif _WIN32 || _WIN64
    PSAPI_WORKING_SET_EX_INFORMATION pv = { nullptr, {0} };
    pv.VirtualAddress = addr;

    // Query the working set extended information
    if (!QueryWorkingSetEx(GetCurrentProcess(), &pv, sizeof(pv))) {
        std::cerr << "QueryWorkingSetEx failed." << std::endl;
        // Function failed (e.g., invalid handle or pointer)
        return -1;
    }

    // If Valid == 0, the page is either swapped out or not yet faulted in.
    if (!pv.VirtualAttributes.Valid) {
        std::cerr << "VirtualAttributes invalid" << std::endl;
        return -1;
    }

    // Extract the NUMA Node from the bitfield (0-63)
    return (int)pv.VirtualAttributes.Node;
#else
    (void)addr;
    return 0;
#endif
}

#if __linux__
    #define NUMA_EQ CHECK_EQ
#else
    // we set only desired NUMA node, but Windows may put page on any node
    #define NUMA_EQ WARN_EQ
#endif

TEST_CASE("invalid parameters") {
    REQUIRE_MESSAGE(tbb::allocate_numa_interleaved(1, 7) == nullptr,
                    "bytes_per_chunk must be multiple of the memory page size");
}

void VerifySizeAndNodes(bool use_find_node, size_t bytes, const std::vector<tbb::numa_node_id>& nodes,
     size_t bytes_per_chunk) {
    char* ptr = (char*)tbb::allocate_numa_interleaved(bytes, nodes, bytes_per_chunk);
    REQUIRE(ptr != nullptr);
    REQUIRE_EQ(utils::NonZero(ptr, bytes), 0);
    if (use_find_node)
        for (size_t i = 0; i < bytes; i += bytes_per_chunk) {
            NUMA_EQ(find_numa_node(ptr + i), nodes[i / bytes_per_chunk % nodes.size()]);
        }
    tbb::deallocate_numa_interleaved(ptr, bytes);
}

TEST_CASE("test basics") {
    size_t page_size = DefaultSystemPageSize();
#if __linux__
#if __TBB_DYNAMIC_LOAD_ENABLED
    utils::LIBRARY_HANDLE lib = utils::OpenLibrary("libnuma.so");
    WARN_MESSAGE(lib, "Can't load libnuma.so, skipping NUMA ownership checks");
    if (lib)
        utils::GetAddress(lib, "move_pages", move_pages_ptr);
#else
    bool lib = false;
#endif
#else // on Windows we use VirtualAllocEx and QueryWorkingSetEx, so no need to load any library
    bool lib = true;
#endif

    for (size_t obj_size = 8; obj_size <= 1024 * 1024LLU; obj_size *= 2)
    {
        std::vector<tbb::numa_node_id> numa_nodes = tbb::info::numa_nodes();
        // we treat no-NUMA as single-NUMA with node index 0, but numa_nodes() return -1 in this case
        if (numa_nodes.size() == 1)
            numa_nodes[0] = 0;

        {
            char *ptr = (char *)tbb::allocate_numa_interleaved(obj_size);
            REQUIRE(ptr != nullptr);
            REQUIRE_EQ(utils::NonZero(ptr, obj_size), 0);
            if (lib)
                for (size_t i = 0; i < obj_size; i += page_size) {
                    NUMA_EQ(find_numa_node(ptr + i), numa_nodes[i / page_size % numa_nodes.size()]);
                }
            tbb::deallocate_numa_interleaved(ptr, obj_size);
        }

        {
            size_t bytes_per_chunk = 3 * page_size;
            char *ptr = (char *)tbb::allocate_numa_interleaved(obj_size, bytes_per_chunk);
            REQUIRE(ptr != nullptr);
            REQUIRE_EQ(utils::NonZero(ptr, obj_size), 0);
            if (lib)
                for (size_t i = 0; i < obj_size; i += bytes_per_chunk) {
                    NUMA_EQ(find_numa_node(ptr + i), numa_nodes[i / bytes_per_chunk % numa_nodes.size()]);
                }
            tbb::deallocate_numa_interleaved(ptr, obj_size);
        }

        // explicit nodes and bytes_per_chunk
        VerifySizeAndNodes(lib, obj_size, numa_nodes, 3 * page_size);

        // revet numa_nodes and check that interleaving works as expected
        std::reverse(numa_nodes.begin(), numa_nodes.end());
        VerifySizeAndNodes(lib, obj_size, numa_nodes, 3 * page_size);

        // remove half of the nodes and check that interleaving works as expected
        numa_nodes.erase(numa_nodes.begin(), numa_nodes.begin() + numa_nodes.size() / 2);
        VerifySizeAndNodes(lib, obj_size, numa_nodes, 7 * page_size);

        // duplcated nodes are supported
        std::vector<tbb::numa_node_id> numa_nodes_1 = tbb::info::numa_nodes();
        // we treat no-NUMA as single-NUMA with node index 0, but numa_nodes() return -1 in this case
        if (numa_nodes_1.size() == 1)
            numa_nodes_1[0] = 0;
        numa_nodes.insert(numa_nodes.end(), numa_nodes_1.begin(), numa_nodes_1.end());
        VerifySizeAndNodes(lib, obj_size, numa_nodes, 3 * page_size);
    }

#if __linux__ && __TBB_DYNAMIC_LOAD_ENABLED
    if (lib)
        utils::CloseLibrary(lib);
#endif
}
