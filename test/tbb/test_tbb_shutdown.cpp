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

#include <atomic>
#include <chrono>
#include <thread>

static struct slow_global_object {
    ~slow_global_object() {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        // Crashes if the library was unmapped in the meantime
        alive = false;
    }
    std::atomic<bool> alive{true};
} slow_global_object_instance;

#else

#include "common/test.h"

#include "../../src/tbb/dynamic_link.h"

// Load the test library at TBB startup
#define __TBB_LOAD_EARLY_LIBRARIES() \
    tbb::detail::r1::dynamic_link(TEST_TBB_SHUTDOWN_LIBRARY, nullptr, 0, nullptr, \
        tbb::detail::r1::DYNAMIC_LINK_DEFAULT & ~tbb::detail::r1::DYNAMIC_LINK_BUILD_ABSOLUTE_PATH)

#include "../../src/tbb/main.cpp"

#include "common/spin_barrier.h"

#include <chrono>
#include <cstdlib>
#include <thread>

constexpr unsigned n_threads = 4;
utils::SpinBarrier barrier(n_threads + 1);

//! \brief \ref error_guessing
TEST_CASE("Library unloading at process exit does not race with global objects destructors") {
    using tbb::detail::r1::__TBB_InitOnce;

    // Since the exit handler is registered after global objects are created,
    // it will be invoked before their destruction
    REQUIRE(std::atexit([] { barrier.wait(); }) == 0);

    for (unsigned i = 0; i < n_threads; ++i) {
        std::thread([] {
            __TBB_InitOnce::add_ref();
            // Signal the main thread that the detached thread has started
            barrier.wait();
            // Wait for the exit handler to be invoked
            barrier.wait();
            // Let __TBB_InitOnce destructor run, but release the references while the plugin's
            // global object is still being destroyed
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            __TBB_InitOnce::remove_ref();
        }).detach();
    }

    // Wait for detached threads to start
    barrier.wait();
}

#endif
