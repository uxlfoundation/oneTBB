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

constexpr std::size_t N = 10000;

/*begin_task_group_extensions_reduction_example*/

#define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
#include "oneapi/tbb/task_group.h"

struct reduce_task {
    static constexpr std::size_t serial_threshold = 16;

    tbb::task_handle operator()() {
        tbb::task_handle next_task;

        std::size_t size = end - begin;
        if (size < serial_threshold) {
            // Perform serial reduction
            for (std::size_t i = begin; i < end; ++i) {
                *result += i;
            }
        } else {
            // The range is too large to process directly
            // Divide it into smaller segments for parallel execution
            std::size_t middle = begin + size / 2;

            std::shared_ptr<std::size_t> left_result = std::make_shared<std::size_t>(0);
            tbb::task_handle left_leaf = tg.defer(reduce_task{begin, middle, left_result, tg});

            std::shared_ptr<std::size_t> right_result = std::make_shared<std::size_t>(0);
            tbb::task_handle right_leaf = tg.defer(reduce_task{middle, end, right_result, tg});

            tbb::task_handle join_task = tg.defer([=]() {
                *result = *left_result + *right_result;
            });

            tbb::task_group::set_task_order(left_leaf, join_task);
            tbb::task_group::set_task_order(right_leaf, join_task);

            tbb::task_group::transfer_this_task_completion_to(join_task);

            // Save the left leaf for further bypassing
            next_task = std::move(left_leaf);

            tg.run(std::move(right_leaf));
            tg.run(join_task);
        }

        return next_task;
    }

    std::size_t begin;
    std::size_t end;
    std::shared_ptr<std::size_t> result;
    tbb::task_group& tg;
};

std::size_t calculate_parallel_sum(std::size_t begin, std::size_t end) {
    tbb::task_group tg;

    std::shared_ptr<std::size_t> reduce_result = std::make_shared<std::size_t>(0);
    reduce_task root_reduce_task(begin, end, reduce_result, tg);
    tg.run_and_wait(root_reduce_task);

    return *reduce_result;
}

/*end_task_group_extensions_reduction_example*/

#include <iostream>

int main() {
    std::size_t serial_sum = N * (N + 1) / 2;
    std::size_t parallel_sum = calculate_parallel_sum(0, N);

    if (serial_sum != parallel_sum) std::cerr << "Incorrect reduction result" << std::endl;
}