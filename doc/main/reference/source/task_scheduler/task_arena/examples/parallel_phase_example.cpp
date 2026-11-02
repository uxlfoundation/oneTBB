/*
    Copyright (c) 2025 Intel Corporation
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

/*begin_parallel_phase_example*/
#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/parallel_sort.h"

#include <chrono>
#include <cstddef>
#include <thread>
#include <vector>

int main() {
    oneapi::tbb::global_control gc(
        oneapi::tbb::global_control::leave_policy,
        oneapi::tbb::task_arena::leave_policy::fast
    );

    oneapi::tbb::task_arena ta;

    std::vector<std::size_t> data(1000);

    // Parallel work separated by a long gap, so fast leave is justified.
    ta.execute([&data]() {
        oneapi::tbb::parallel_for(std::size_t(0), data.size(), [&data](std::size_t i) {
            data[i] = i;
        });
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    {
        // A sequence of parallel computations with short serial parts in between.
        // It may be useful to hint the scheduler to keep worker threads in the arena for the whole phase.
        oneapi::tbb::task_arena::parallel_phase phase{ta};
        ta.execute([&data]() {
            oneapi::tbb::parallel_for(std::size_t(0), data.size(), [&data](std::size_t i) {
                data[i] = i*i;
            });
        });

        for (std::size_t i = 1; i < data.size(); ++i) {
            data[i] += data[i-1];
        }

        ta.execute([&data]() {
            oneapi::tbb::parallel_sort(data.begin(), data.end());
        });
    } // the phase ends
}
/*end_parallel_phase_example*/
