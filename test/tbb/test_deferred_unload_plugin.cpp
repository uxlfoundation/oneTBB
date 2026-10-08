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

//! \file test_deferred_unload_plugin.cpp
//! \brief Shared library that links oneTBB and performs parallel work. It mimics a native
//! extension module loaded by a host that does not link oneTBB itself.

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>

#include <cmath>
#include <cstddef>
#include <new>
#include <vector>

// The oneTBB build hides symbols by default, so the entry point has to be exported explicitly.
extern "C" __attribute__((visibility("default"))) void run_work() {
    constexpr int n = 1000;
    std::vector<double> values(n, 0.0);

    oneapi::tbb::parallel_for(oneapi::tbb::blocked_range<int>(0, n),
        [&](const oneapi::tbb::blocked_range<int>& range) {
            for (int i = range.begin(); i != range.end(); ++i) {
                values[i] = std::sqrt(static_cast<double>(i) + 0.5);
            }
        });

    oneapi::tbb::parallel_for(oneapi::tbb::blocked_range<int>(0, n),
        [&](const oneapi::tbb::blocked_range<int>& range) {
            for (int i = range.begin(); i != range.end(); ++i) {
                values[i] += values[n - i - 1];
            }
        });
}

// Joins the oneTBB worker threads. Unloading oneTBB while its workers still execute its code
// is unsafe and is a separate concern from the exit-time unload ordering this test targets.
extern "C" __attribute__((visibility("default"))) void stop_work() {
    oneapi::tbb::task_scheduler_handle handle{oneapi::tbb::attach{}};
    oneapi::tbb::finalize(handle, std::nothrow);
}
