/*
    Copyright (c) 2019-2025 Intel Corporation
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

//! \file test_tbbbind.cpp
//! \brief Test for TBBbind library, covers [configuration.debug_features]

#define TRY_BAD_EXPR_ENABLED 1
#if _WIN32 || _WIN64
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/info.h"
#include "common/test.h"
#include "common/utils_assert.h"

using namespace tbb::detail::r1;

// no need to do it in the test
#define __TBB_SKIP_DEPENDENCY_SIGNATURE_VERIFICATION 1

#include "../../src/tbb/dynamic_link.cpp"

class binding_handler;
    // dummy implementation, not used
class governor {
    public:
        static unsigned default_num_threads () { return 0; }
};

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../../src/tbb/load_tbbbind.cpp"
#pragma GCC diagnostic pop

#if TBB_USE_ASSERT
bool canTestTbbBind() {
#if _WIN32 && !_WIN64
    if (std::thread::hardware_concurrency() > 32) {
        MESSAGE("There is no TBBbind for 32-bit Windows and > 32 logical CPUs");
        return false;
    }
#endif

    core_type_count(); // to initialize internals of governor

    return system_topology::load_tbbbind_shared_object() != nullptr;
}
#else
// All assertions in TBBbind are available only in TBB_USE_ASSERT mode.
bool canTestTbbBind() { return false; }
#endif


// The test relies on an assumption that system_topology::load_tbbbind_shared_object() find
// same instance of TBBbind as TBB uses internally.
TEST_CASE("Using custom assertion handler inside TBBbind" * doctest::skip(! canTestTbbBind())) {
    tbb::set_assertion_handler(utils::AssertionFailureHandler);
    TRY_BAD_EXPR(deallocate_binding_handler_ptr(nullptr), "Trying to deallocate nullptr pointer.");
}
