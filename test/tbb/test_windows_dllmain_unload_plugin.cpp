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

//! \file test_windows_dllmain_unload_plugin.cpp
//! \brief Plugin used by test_windows_dllmain_unload.cpp.
//!
//! The plugin links oneTBB, so the host can load and unload the runtime dynamically. The scheduler
//! handle allows the host to join all workers before it releases its DLL references.

#define __TBB_NO_IMPLICIT_LINKAGE 1

#include <oneapi/tbb/global_control.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/blocked_range.h>

#include <atomic>
#include <new>

namespace {
oneapi::tbb::task_scheduler_handle scheduler_handle;
} // namespace

extern "C" __declspec(dllexport) void attach_scheduler() {
    scheduler_handle = oneapi::tbb::task_scheduler_handle{oneapi::tbb::attach{}};
}

extern "C" __declspec(dllexport) int run_work() {
    std::atomic<int> sum{0};
    tbb::parallel_for(tbb::blocked_range<int>(0, 1000), [&](const tbb::blocked_range<int>& range) {
        int local = 0;
        for (int i = range.begin(); i != range.end(); ++i) {
            local += i % 7;
        }
        sum.fetch_add(local, std::memory_order_relaxed);
    });
    return sum.load() == 2997 ? 0 : 1;
}

extern "C" __declspec(dllexport) int finalize_work() {
    return oneapi::tbb::finalize(scheduler_handle, std::nothrow) ? 0 : 1;
}
