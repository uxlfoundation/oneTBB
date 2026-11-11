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

//! \file test_tbb_shutdown.cpp
//! \brief Test for [internal] functionality

#define __TBB_NO_IMPLICIT_LINKAGE 1

#if _USRDLL
#include "common/dummy_body.h"

#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/info.h"
#include "oneapi/tbb/task_arena.h"
#include "oneapi/tbb/parallel_for.h"
#include "oneapi/tbb/tbb_allocator.h"

#include <algorithm>
#include <cstddef>
#include <thread>

#if _WIN32||_WIN64
    #define TEST_EXPORT __declspec(dllexport)
#else
    #define TEST_EXPORT __TBB_EXPORT
#endif

extern "C" TEST_EXPORT bool run_tbb_shutdown_test() {
    constexpr std::size_t n = 1000;

    tbb::global_control gc(tbb::global_control::max_allowed_parallelism,
                           std::max(2u, std::thread::hardware_concurrency()));

    // Loads tbbmalloc
    tbb::tbb_allocator<std::size_t> alloc;
    std::size_t* data = alloc.allocate(n);
    std::fill(data, data + n, 0);

    // Loads tbbbind (if available)
    auto numa_nodes = tbb::info::numa_nodes();
    tbb::task_arena arena(tbb::task_arena::constraints{}.set_numa_id(numa_nodes.front()));
    arena.execute([data] {
        tbb::parallel_for(std::size_t(0), n, [data](std::size_t i) {
            utils::doDummyWork(1000);
            data[i] = i + 1;
        });
    });

    bool correct = true;
    for (std::size_t i = 0; i < n; ++i) {
        correct = correct && data[i] == i + 1;
    }
    alloc.deallocate(data, n);
    return correct;
}

#else

#include "common/test.h"
#include "common/utils_dynamic_libs.h"

//! \brief \ref error_guessing
TEST_CASE("Process exit after runtime loading TBB") {
    utils::LIBRARY_HANDLE library = utils::OpenLibrary(TEST_LIBRARY_NAME("_test_tbb_shutdown"));
    REQUIRE_MESSAGE(library, "Cannot find " TEST_LIBRARY_NAME("_test_tbb_shutdown"));

    bool (*run)() = nullptr;
    utils::GetAddress(library, "run_tbb_shutdown_test", run);
    REQUIRE_MESSAGE(run(), "parallel algorithm produced incorrect results");

    // The library is intentionally not closed as the test checks TBB's shutdown at process exit
}

#endif
