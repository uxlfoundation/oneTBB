/*
    Copyright (c) 2025 Intel Corporation

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

#include <cstdint>
#include <vector>

constexpr std::size_t N = 10000;

std::vector<std::size_t> global_vector(N, 0);

void foo(std::size_t begin, std::size_t end) {
    for (std::size_t i = begin; i < end; ++i) {
        global_vector[i] = 42;
    }
}

/*begin_task_group_extensions_bypassing_example*/
#define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
#include "oneapi/tbb/task_group.h"

void foo(std::size_t begin, std::size_t end);

struct for_task {
    static constexpr std::size_t serial_threshold = 16;
    tbb::task_handle operator()() const {
        tbb::task_handle next_task;
        std::size_t size = end - begin;
        if (size < serial_threshold) {
            // Execute the work serially
            foo(begin, end);
        } else {
            // Enough work to split the range
            std::size_t middle = begin + size / 2;

            // Submit the right subtask for execution
            tg.run(for_task{middle, end, tg});

            // Bypass the left part
            next_task = tg.defer(for_task{begin, middle, tg});
        }
        return next_task;
    }

    std::size_t begin;
    std::size_t end;
    tbb::task_group& tg;
}; // struct for_task

void calculate_parallel_for(std::vector<std::size_t>& vec) {
    tbb::task_group tg;
    // Run the root task
    tg.run_and_wait(for_task{0, vec.size(), tg});
}
/*end_task_group_extensions_bypassing_example*/

#include <iostream>

int main() {
    calculate_parallel_for(global_vector);

    for (std::size_t i = 0; i < global_vector.size(); ++i) {
        if (global_vector[i] != 42) {
            std::cerr << "Error in " << i << " index" << std::endl;
            return 1;
        }
    }
}
