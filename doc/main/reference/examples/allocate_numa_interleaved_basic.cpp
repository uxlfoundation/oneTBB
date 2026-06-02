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

/*begin_allocate_numa_interleaved_example*/
#define TBB_PREVIEW_NUMA_ALLOCATION 1

#include <oneapi/tbb/numa_allocation.h>
#include <oneapi/tbb/parallel_for.h>

int main() {
    std::size_t array_size = 10LLU * 1024 * 1024;
    double* ptr = (double*)oneapi::tbb::allocate_numa_interleaved(array_size * sizeof(double));
    if (!ptr)
        return -1;
    oneapi::tbb::parallel_for(std::size_t(0), array_size, [=](std::size_t i) {
        ptr[i] = i;
    });

    oneapi::tbb::deallocate_numa_interleaved(ptr, array_size * sizeof(double));
}
/*end_allocate_numa_interleaved_example*/
