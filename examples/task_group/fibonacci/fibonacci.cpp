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

#define TBB_PREVIEW_TASK_GROUP_EXTENSIONS 1
#include "oneapi/tbb/tick_count.h"
#include "oneapi/tbb/task_group.h"
#include "oneapi/tbb/concurrent_unordered_map.h"
#include "oneapi/tbb/global_control.h"

#include "common/utility/utility.hpp"
#include "common/utility/measurements.hpp"
#include "common/utility/get_default_num_threads.hpp"

#include <cstdint>
#include <cstdio>

struct fibonacci_cache_entry {
    fibonacci_cache_entry(const oneapi::tbb::task_handle& handle)
        : m_calculation_task(handle)
        , m_result(0)
    {}

    oneapi::tbb::task_completion_handle m_calculation_task;
    unsigned long                       m_result;
};

using fibonacci_cache_type = oneapi::tbb::concurrent_unordered_map<std::size_t, fibonacci_cache_entry>;

void calculate_fibonacci_impl(oneapi::tbb::task_group& tg, fibonacci_cache_type& fibonacci_cache,
                              std::size_t num, unsigned long& result)
{
    if (num == 0 || num == 1) {
        result = num;
        return;
    }

    // Pointers to the result where num-1th and num-2th Fibonacci numbers are stored or will be stored
    unsigned long* pred1_result_placeholder = nullptr;
    unsigned long* pred2_result_placeholder = nullptr;

    // Completion handles of the task that compute num-1th and num-2th Fibonacci numbers
    oneapi::tbb::task_completion_handle pred1_comp_handle;
    oneapi::tbb::task_completion_handle pred2_comp_handle;

    auto read_cache_or_run_computation = 
        [&tg, &fibonacci_cache](std::size_t num,
                                oneapi::tbb::task_completion_handle& comp_handle,
                                unsigned long*& result_placeholder)
    {
        // Trying to find already requested Fibonacci computations for num
        auto it = fibonacci_cache.find(num);
        if (it != fibonacci_cache.end()) {
            // Found a valid cache entry, but the related task may still be unexecuted
            // Save the task completion handle and the pointer to the place where
            // the result will be stored
            comp_handle = it->second.m_calculation_task;
            result_placeholder = &it->second.m_result;
        } else {
            // No entry found, create a task to calculate num-th Fibonacci number
            oneapi::tbb::task_handle t = tg.defer([&tg, &fibonacci_cache, num] {
                // Finding the place where the result should be stored
                // Entry should be present at this point
                auto entry = fibonacci_cache.find(num);
                unsigned long& result_placeholder = entry->second.m_result;
                calculate_fibonacci_impl(tg, fibonacci_cache, num, result_placeholder);
            });

            // Trying to add cache entry for num-th Fibonacci number
            // Another thread may simultaneously request calculation of the same number
            // Only one thread will win the insertion
            auto res = fibonacci_cache.emplace(num, t);
            
            // Save the task completion handle and the result placeholder from the cache
            // May point to the task and the placeholder created by another thread
            comp_handle = res.first->second.m_calculation_task;
            result_placeholder = &res.first->second.m_result;

            if (res.second) {
                // If the current thread inserted the cache entry, run the task to start computations
                tg.run(std::move(t));
            }
        }
    };

    // Finding the task and the result placeholder for num-1th and num-2th Fibonacci numbers
    read_cache_or_run_computation(num - 2, pred1_comp_handle, pred1_result_placeholder);
    read_cache_or_run_computation(num - 1, pred2_comp_handle, pred2_result_placeholder);

    // Create a task that computes num-th Fibonacci number as a sum of num-1th and num-2th numbers
    oneapi::tbb::task_handle sum = tg.defer([pred1_result_placeholder, pred2_result_placeholder, &result] {
        result = *pred1_result_placeholder + *pred2_result_placeholder;
    });

    // Set dependencies to ensure num-1th and num-2th numbers are calculated before the sum
    oneapi::tbb::task_group::set_task_order(pred1_comp_handle, sum);
    oneapi::tbb::task_group::set_task_order(pred2_comp_handle, sum);

    // Transfer successors of the current task to preserve the structure of the entire task tree
    oneapi::tbb::task_group::transfer_this_task_completion_to(sum);

    tg.run(std::move(sum));
}

unsigned long calculate_fibonacci_number(std::size_t num_fib) {
    unsigned long result = 0;
    oneapi::tbb::task_group tg;
    fibonacci_cache_type fibonacci_cache;

    tg.run_and_wait([&] {
        calculate_fibonacci_impl(tg, fibonacci_cache, num_fib, result);
    });
    return result;
}

int main(int argc, char* argv[]) {
    std::size_t num_threads = oneapi::tbb::this_task_arena::max_concurrency();
    bool silent = false;
    int repeats = 1;
    std::size_t num_fib = 10;

    utility::parse_cli_arguments(argc, argv,
        utility::cli_argument_pack()
            .positional_arg(num_threads, "--num-threads", "Num threads to use")
            .positional_arg(num_fib, "--fib-number", "Fibonacci number to compute")
            .positional_arg(repeats, "--num-repeats", "Repeat computation this number of times, must be positive integer")
            .arg(silent, "--silent", "No output except elapsed time")
    );

    if (repeats < 1) {
        fprintf(stderr, "Incorrect num-repeats=%d, exit.\n", repeats);
        return -1;
    }

    if (!silent) {
        fprintf(stdout, "Calculating %luth Fibonacci Number\n", num_fib);
    }

    oneapi::tbb::global_control ctx(oneapi::tbb::global_control::max_allowed_parallelism, num_threads);

    oneapi::tbb::tick_count start_time = oneapi::tbb::tick_count::now();
    
    unsigned long number = calculate_fibonacci_number(num_fib);

    for (std::size_t i = 0; i < repeats - 1; ++i) {
        unsigned long it_number = calculate_fibonacci_number(num_fib);
        if (number != it_number) {
            fprintf(stderr, "Run-to-run Fibonacci numbers are not equal");
            return -1;
        }        
    }

    oneapi::tbb::tick_count finish_time = oneapi::tbb::tick_count::now();

    if (!silent) {
        fprintf(stdout, "%luth Fibonacci Number is %lu\n", num_fib, number);
    }
    
    utility::report_elapsed_time((finish_time - start_time).seconds());
}
