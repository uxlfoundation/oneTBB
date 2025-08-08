/*
    Copyright (c) 2005-2020 Intel Corporation

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

#include <cstdio>
#include <cstdlib>

#include "task_emulation_layer.h"

extern long CutOff;

long SerialFib( const long n ) {
    if( n < 2 )
        return n;
    else
        return SerialFib(n-1) + SerialFib(n-2);
}

struct fib_continuation : task_emulation::base_task {
    fib_continuation(long& s) : sum(s) {}

    task_emulation::base_task* execute() override {
        sum = x + y;
        return nullptr;
    }

    long x{ 0 }, y{ 0 };
    long& sum;
};

struct fib_computation : task_emulation::base_task {
    fib_computation(long n, long* x) : n(n), x(x) {}

    task_emulation::base_task* execute() override {
        task_emulation::base_task* bypass = nullptr;
        if (n < cutoff) {
            *x = SerialFib(n);
        }
        else {
            // Continuation passing
            auto& c = *this->allocate_continuation<fib_continuation>(/* children_counter = */ 2, *x);
            task_emulation::run_task(c.create_child<fib_computation>(n - 1, &c.x));

            // Recycling
            this->recycle_as_child_of(c);
            n = n - 2;
            x = &c.y;
            bypass = this;
        }
        return bypass;
    }

    long n;
    long* x;
};

long ParallelFib( const long n ) {
    long sum = 0;
    tbb::task_group tg;
    tg.run_and_wait(
        task_emulation::create_root_task<fib_computation>(/* for root task = */ tg, n, &sum));
    return sum;
}

