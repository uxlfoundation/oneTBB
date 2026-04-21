
#include "common/test.h"
#include "common/utils_dynamic_libs.h"

#include "tbb/numa_memory.h"

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

TEST_CASE("test basics") {
    size_t page_size = DefaultSystemPageSize();
#if __linux__
    utils::LIBRARY_HANDLE lib = utils::OpenLibrary("libnuma.so");
    WARN_MESSAGE(lib, "Can't load libnuma.so, skipping NUMA ownership checks");
    if (lib)
        utils::GetAddress(lib, "move_pages", move_pages_ptr);
#else
    bool lib = true;
#endif

    for (size_t obj_size = page_size; obj_size <= 1024 * 1024LLU; obj_size *= 2)
    {
        std::vector<tbb::numa_node_id> numa_nodes = tbb::info::numa_nodes();
        // we treat no-NUMA as single-NUMA with node index 0, but numa_nodes() return -1 in this case
        if (numa_nodes.size() == 1)
            numa_nodes[0] = 0;
        {
            char *ptr = (char *)tbb::alloc_interleaved(obj_size);
            REQUIRE(ptr != nullptr);
            memset(ptr, 0, obj_size);
            if (lib)
                for (size_t i = 0; i < obj_size; i += page_size)
                    REQUIRE_EQ(find_numa_node(ptr + i), numa_nodes[i / page_size % numa_nodes.size()]);
            tbb::free_interleaved(ptr, obj_size);
        }

        {
            char *ptr = (char *)tbb::alloc_interleaved(obj_size, 2 * page_size);
            REQUIRE(ptr != nullptr);
            memset(ptr, 0, obj_size);
            if (lib)
                for (size_t i = 0; i < obj_size; i += 2 * page_size)
                    REQUIRE_EQ(find_numa_node(ptr + i), numa_nodes[i / (2 * page_size) % numa_nodes.size()]);
            tbb::free_interleaved(ptr, obj_size);
        }

        // revet numa_nodes and check that interleaving works as expected
        std::reverse(numa_nodes.begin(), numa_nodes.end());
        {
            char *ptr = (char *)tbb::alloc_interleaved(obj_size, numa_nodes);
            REQUIRE(ptr != nullptr);
            memset(ptr, 0, obj_size);
            if (lib)
                for (size_t i = 0; i < obj_size; i += page_size)
                    REQUIRE(find_numa_node(ptr + i) == numa_nodes[i / page_size % numa_nodes.size()]);
            tbb::free_interleaved(ptr, obj_size);
        }

        // remove half of the nodes and check that interleaving works as expected
        numa_nodes.erase(numa_nodes.begin(), numa_nodes.begin() + numa_nodes.size() / 2);
        {
            char *ptr = (char *)tbb::alloc_interleaved(obj_size, numa_nodes);
            REQUIRE(ptr != nullptr);
            memset(ptr, 0, obj_size);
            if (lib)
                for (size_t i = 0; i < obj_size; i += page_size)
                    REQUIRE(find_numa_node(ptr + i) == numa_nodes[i / page_size % numa_nodes.size()]);
            tbb::free_interleaved(ptr, obj_size);
        }
    }

#if __linux__
    utils::CloseLibrary(lib);
#endif
}
