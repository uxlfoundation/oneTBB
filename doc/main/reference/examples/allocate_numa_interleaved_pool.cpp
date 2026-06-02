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
#include <oneapi/tbb/parallel_for.h>

#include <array>
#include <vector>

class numa_interleaved_provider {
public:
    // Guarantee that each allocation is a multiple of the system page size,
    // so allocate_numa_interleaved() requirements are satisfied.
    typedef std::array<char, 4*1024> value_type;
    numa_interleaved_provider() {}
    // Like std::allocator<T>::allocate, these functions expect the number of
    // objects of the same size as sizeof(value_type).
    void *allocate(size_t num_of_objects) {
        return
            oneapi::tbb::allocate_numa_interleaved(num_of_objects * sizeof(value_type));
    }
    void deallocate(void *ptr, size_t num_of_objects) {
        oneapi::tbb::deallocate_numa_interleaved(ptr, num_of_objects * sizeof(value_type));
    }
};

int main() {
    // Memory pool requests memory in big chunks, slices them internally and uses
    // memory caching, so may improve performance for many small allocations and
    // scenarios with the objects reuse.
    oneapi::tbb::memory_pool<numa_interleaved_provider> pool;

    oneapi::tbb::parallel_for(0, 1024*1024, [&pool](std::size_t i) {
        // Temporary arrays allocated from the pool will reside in different
        // NUMA domains for better overall memory throughput.
        // As the pool caches the memory, on average it is faster than
        // allocate_numa_interleaved()/deallocate_numa_interleaved()
        double* ptr = (double*)pool.malloc(10*1000*sizeof(double));
        // ...
        pool.free(ptr);
    });

    // std::vector uses interleaved NUMA memory
    using pool_allocator_t = oneapi::tbb::memory_pool_allocator<double>;
    std::vector<double, pool_allocator_t> values(pool_allocator_t{pool});
    values.push_back(3.14);
}
/*end_allocate_numa_interleaved_pool_example*/
