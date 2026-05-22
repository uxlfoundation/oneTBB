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

#include <algorithm> // for std::any_of
#include <sys/mman.h>

// TBB build must be done without numaif.h, but we need signatures of those functions
// for dynamic loading, so declare it here.
extern "C" {
long move_pages(int pid, unsigned long count,
                void **pages, const int *nodes, int *status, int flags);
struct bitmask *numa_bitmask_alloc(unsigned int);
void numa_bitmask_free(struct bitmask *);
int numa_bitmask_isbitset(const struct bitmask *, unsigned int);
struct bitmask *numa_bitmask_setbit(struct bitmask *, unsigned int);
void numa_interleave_memory(void *mem, size_t size, struct bitmask *mask);

} // extern "C"

#elif !(_WIN32 || _WIN64)

#include <stdlib.h> // for malloc and free

#endif

namespace tbb {
namespace detail {
namespace r1 {

#if __linux__

#if __TBB_WEAK_SYMBOLS_PRESENT
#pragma weak move_pages
#pragma weak numa_bitmask_alloc
#pragma weak numa_bitmask_free
#pragma weak numa_bitmask_isbitset
#pragma weak numa_bitmask_setbit
#pragma weak numa_interleave_memory
#endif

static long (*move_pages_ptr)(int pid, unsigned long count,
             void **pages, const int *nodes, int *status, int flags);
static struct bitmask *(*numa_bitmask_alloc_ptr)(unsigned int);
static void (*numa_bitmask_free_ptr)(struct bitmask *);
static int (*numa_bitmask_isbitset_ptr)(const struct bitmask *, unsigned int);
static struct bitmask *(*numa_bitmask_setbit_ptr)(struct bitmask *, unsigned int);
static void (*numa_interleave_memory_ptr)(void *mem, size_t size, struct bitmask *mask);

static const dynamic_link_descriptor LibnumaLinkTable[] = {
    DLD(move_pages, move_pages_ptr),
    DLD(numa_bitmask_alloc, numa_bitmask_alloc_ptr),
    DLD(numa_bitmask_free, numa_bitmask_free_ptr),
    DLD(numa_bitmask_isbitset, numa_bitmask_isbitset_ptr),
    DLD(numa_bitmask_setbit, numa_bitmask_setbit_ptr),
    DLD(numa_interleave_memory, numa_interleave_memory_ptr)
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

bool is_args_valid(size_t bytes, const tbb::detail::d1::numa_node_id *nodes_ids, size_t nodes_count,
                 size_t bytes_per_chunk) {
    if (bytes == 0) // to be consistent with mmap
        return false;
    if (bytes_per_chunk % governor::default_page_size() != 0)
        return false;
    // nodes_ids and nodes_count must be consistent with each other,
    // i.e. either nodes_ids is nullptr and nodes_count is 0 or vice versa
    return (nodes_ids == nullptr) ^ (nodes_count != 0);
}

// interleaved memory allocation is only supported for those platforms
#if __linux__ || _WIN32 || _WIN64

static std::atomic<do_once_state> interleaved_initialization_state;

void interleaved_initialization_impl() {
#if __linux__
    const char* numa_lib_name = "libnuma.so.1";
#elif _WIN32 || _WIN64
    const char* numa_lib_name = "kernelbase.dll";
#endif
    dynamic_link(numa_lib_name, LibnumaLinkTable,
                 sizeof(LibnumaLinkTable) / sizeof(dynamic_link_descriptor), /*handle*/nullptr,
                 DYNAMIC_LINK_GLOBAL | DYNAMIC_LINK_LOAD | DYNAMIC_LINK_WEAK);
}

static const int *common_init(size_t bytes, const tbb::detail::d1::numa_node_id *nodes_ids,
                              size_t &nodes_count, size_t &bytes_per_chunk) {
    atomic_do_once(interleaved_initialization_impl, interleaved_initialization_state);

    if (!is_args_valid(bytes, nodes_ids, nodes_count, bytes_per_chunk))
        return nullptr;

    if (!bytes_per_chunk)
        bytes_per_chunk = governor::default_page_size();
    const int *nodes = nodes_count ? nodes_ids : get_numa_nodes_indexes();
    if (!nodes_count)
        nodes_count = numa_node_count();

    return nodes;
}

static bool is_numa_funcs_found() {
#if __linux__
    return move_pages_ptr && numa_bitmask_alloc_ptr && numa_bitmask_free_ptr &&
           numa_bitmask_isbitset_ptr && numa_bitmask_setbit_ptr && numa_interleave_memory_ptr;
#elif _WIN32 || _WIN64
    return VirtualAlloc2_ptr;
 #endif
}

#if __linux__
void *__TBB_EXPORTED_FUNC allocate_interleaved(size_t bytes,
                        const tbb::detail::d1::numa_node_id *nodes_ids, size_t nodes_count,
                        size_t bytes_per_chunk) {
    const int *nodes = common_init(bytes, nodes_ids, nodes_count, bytes_per_chunk);
    if (!nodes)
        return nullptr;

    char *base_addr = reinterpret_cast<char*>(
        mmap(/*addr=*/nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, /*fd=*/-1, /*offset=*/0));
    if (base_addr == MAP_FAILED)
        return nullptr;

    // no NUMA nodes or move_pages() not available, just return the memory as is
    if (numa_node_count() == 1 || !is_numa_funcs_found())
        return base_addr;

    auto unmap = [bytes](void *ptr) {
        munmap(ptr, bytes);
    };
    std::unique_ptr<void, decltype(unmap)> data_holder(base_addr, unmap);

    // for chunk size equal to page size and all nodes being different we can place memory right to
    // the appropriate NUMA nodes with a single system call, otherwise we need to touch each page
    // from current thread and only then move them
    if (bytes_per_chunk == governor::default_page_size() && nodes_count <= numa_node_count()) {
        auto bitmask_free = [](struct bitmask *m) {
            numa_bitmask_free_ptr(m);
        };
        std::unique_ptr<struct bitmask, decltype(bitmask_free)>
            bitmask_holder(numa_bitmask_alloc_ptr(numa_node_count()), bitmask_free);

        size_t i = 0;
        for (; i < nodes_count; ++i) {
            // numa_interleave_memory doesn't support repeated nodes, fallback to move_pages()
            if (numa_bitmask_isbitset_ptr(bitmask_holder.get(), nodes[i]))
                break;
            numa_bitmask_setbit_ptr(bitmask_holder.get(), nodes[i]);
        }
        if (i == nodes_count) {
            numa_interleave_memory_ptr(base_addr, bytes, bitmask_holder.get());

            return data_holder.release();
        }
    }
    // process non-standard chunk size or repeated nodes

    // touch each page, otherwise move_pages() will fail with EFAULT
    for (size_t i = 0; i < bytes; i += governor::default_page_size())
        base_addr[i] = 0;

    size_t count_pages = (bytes + governor::default_page_size() - 1) / governor::default_page_size();
    std::unique_ptr<void *[]> pages(new void *[count_pages]);
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
    long ret = move_pages_ptr(/*pid=*/0, count_pages, pages.get(), nodes_per_page.get(), status.get(),
                              /*flags=*/0);
    if (ret < 0)
        return nullptr;

    return std::any_of(status.get(), status.get() + count_pages, [](int s) { return s < 0; }) ?
        nullptr : data_holder.release();
}

#elif _WIN32 || _WIN64

void *__TBB_EXPORTED_FUNC allocate_interleaved(size_t bytes,
                        const tbb::detail::d1::numa_node_id *nodes_ids, size_t nodes_count,
                        size_t bytes_per_chunk) {
    const int *nodes = common_init(bytes, nodes_ids, nodes_count, bytes_per_chunk);
    if (!nodes)
        return nullptr;

    // no NUMA nodes or no VirtualAlloc2, just return the memory as is
    if (numa_node_count() == 1 || !is_numa_funcs_found())
        // do not use VirtualAlloc(), because it compiled incorrectly by MSVC 2017 with
        // -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDebug -DTBB_WINDOWS_DRIVER=ON
        return VirtualAllocEx(GetCurrentProcess(), /*BaseAddress=*/nullptr, bytes, MEM_RESERVE | MEM_COMMIT,
                              PAGE_READWRITE);

    // for VirtualAlloc2 it must be a multiple of the page size
    bytes = align_to_greater_or_equal(bytes, governor::default_page_size());
    char* base_addr =
        reinterpret_cast<char*>(VirtualAlloc2_ptr(/*Process=*/nullptr, /*BaseAddress=*/nullptr, bytes,
                                                  MEM_RESERVE | MEM_RESERVE_PLACEHOLDER, PAGE_NOACCESS,
                                                  /*ExtendedParameters=*/nullptr, /*ParameterCount=*/0));
    if (!base_addr)
        return nullptr;

     auto unmap = [](char* base_addr) {
        VirtualFree(base_addr, /*dwSize=*/0, MEM_RELEASE);
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
                                         &param, /*ParameterCount=*/1);

        if (!result)
            return nullptr;
    }
    return data_holder.release();
}

#endif // _WIN32 || _WIN64

void __TBB_EXPORTED_FUNC deallocate_interleaved(void *ptr, size_t bytes) {
    atomic_do_once(interleaved_initialization_impl, interleaved_initialization_state);

    // TODO: process return value of munmap()/VirtualFree()
#if __linux__
    munmap(ptr, bytes);
#elif _WIN32 || _WIN64
    suppress_unused_warning(bytes);
    VirtualFree(ptr, /*dwSize=*/0, MEM_RELEASE);
#endif
}

#else /* __linux__ || _WIN32 || _WIN64 */

// fallback implementation with malloc/free
void *__TBB_EXPORTED_FUNC allocate_interleaved(size_t bytes,
                        const tbb::detail::d1::numa_node_id *nodes_ids, size_t nodes_count,
                        size_t bytes_per_chunk) {
    return is_args_valid(bytes, nodes_ids, nodes_count, bytes_per_chunk) ?
        calloc(bytes, 1) : nullptr;
}

void __TBB_EXPORTED_FUNC deallocate_interleaved(void *ptr, size_t /*bytes*/) {
    free(ptr);
}

#endif /* __linux__ || _WIN32 || _WIN64 */

} // namespace r1
} // namespace detail
} // namespace tbb
