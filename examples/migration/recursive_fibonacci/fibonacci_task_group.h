/*
    Copyright (c) 2025 Intel Corporation
    Copyright (c) 2025 UXL Foundation Contributors

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

#ifndef TASK_GROUP_HEADER
#define TASK_GROUP_HEADER

#include <tbb/task_group.h>
#include <memory>
#include <utility>

extern int cutoff;

// struct fib_sum_task {
//     void operator()() const {
//         *result = *left + *right;
//         delete left;
//         delete right;
//     }

//     int* left;
//     int* right;
//     int* result;
// };

struct fib_sum_task {
    void operator()() const {
        result.get() = left + right;
    }

    int left;
    int right;
    std::reference_wrapper<int> result;
};

struct fib_compute_task {
    static int serial_fib(int n) {
        return n < 2 ? n : serial_fib(n - 1) + serial_fib(n - 2);
    }

    tbb::task_handle operator()() const {
        tbb::task_handle bypass;

        if (n < cutoff) {
            // *x = fib_compute_task::serial_fib(n);
            x.get() = fib_compute_task::serial_fib(n);
        } else {
            // int* prev_fib = new int{};
            // int* prev_prev_fib = new int{};
            // tbb::task_handle compute_prev = tg.defer(fib_compute_task{n - 1, prev_fib, tg});
            // tbb::task_handle compute_prev_prev = tg.defer(fib_compute_task{n - 2, prev_prev_fib, tg});
            // tbb::task_handle sum = tg.defer(fib_sum_task{prev_fib, prev_prev_fib, x});

            tbb::task_handle sum = tg.defer(fib_sum_task{0, 0, x});
            auto& actual_body = sum.get_function<fib_sum_task>();

            tbb::task_handle compute_prev = tg.defer(fib_compute_task{n - 1, std::ref(actual_body.left), tg});
            tbb::task_handle compute_prev_prev = tg.defer(fib_compute_task{n - 2, std::ref(actual_body.right), tg});

            tbb::task_group::set_task_order(compute_prev, sum);
            tbb::task_group::set_task_order(compute_prev_prev, sum);
            tbb::task_group::transfer_this_task_completion_to(sum);

            tg.run(std::move(compute_prev_prev));
            tg.run(std::move(sum));

            bypass = std::move(compute_prev);
        }
        return bypass;
    }

    int n;
    // int* x;
    std::reference_wrapper<int> x;
    tbb::task_group& tg;
};

int fibonacci_task_group(int n) {
    int sum{};

    tbb::task_group tg;
    // tg.run_and_wait(fib_compute_task{n, &sum, tg});
    tg.run_and_wait(fib_compute_task{n, std::ref(sum), tg});

    return sum;
}

#endif
