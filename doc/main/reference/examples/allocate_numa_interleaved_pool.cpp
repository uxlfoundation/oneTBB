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

/*begin_allocate_numa_interleaved_pool_example*/
#define TBB_PREVIEW_MEMORY_POOL 1
#define TBB_PREVIEW_NUMA_ALLOCATION 1

#include <oneapi/tbb/numa_allocation.h>
#include <oneapi/tbb/memory_pool.h>

#include <cassert>
#include <array>

class numa_interleaved_allocator {
public:
    // Guarantee that each allocation is a multiple of the system page size,
    // so allocate_numa_interleaved() requirements are satisfied.
    typedef std::array<char, 4*1024> value_type;
    numa_interleaved_allocator() {}
    void *allocate(size_t size) {
        return tbb::allocate_numa_interleaved(size*sizeof(value_type));
    }
    void deallocate(void *ptr, size_t size) {
        tbb::deallocate_numa_interleaved(ptr, size*sizeof(value_type));
    }
};

int main() {
    // Memory pool requests memory in big chunks, slices them internally and uses caching,
    // so may improve performance for many small allocations and reuse scenarios.
    tbb::memory_pool<numa_interleaved_allocator> pool;
    for (int i = 0; i < 10*1000; ++i) {
        void* ptr = pool.malloc(1024);
        assert(ptr);
        pool.free(ptr);
    }
}

/*end_allocate_numa_interleaved_pool_example*/
