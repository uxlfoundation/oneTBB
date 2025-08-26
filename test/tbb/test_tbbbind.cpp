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
//! \brief Test for TBBbind library

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
    // dummy implementaion, not used
class governor {
    public:
        static unsigned default_num_threads () { return 0; }
};

#include "../../src/tbb/load_tbbbind.cpp"

// The test relies on an assumption that system_topology::load_tbbbind_shared_object() find same
// instance of TBBbind as TBB uses internally.
TEST_CASE("Using custom assertion handler inside TBBbind") {
#if _WIN32 && !_WIN64
    if (std::thread::hardware_concurrency() > 32) {
        MESSAGE("There is no TBBbind for 32-bit Windows and > 32 logical CPUs");
        return;
    }
#endif

    // to initialize internals of governor
    core_type_count();

    tbb::detail::r1::assertion_handler_type custom_handler =
        [](const char* location, int line, const char* expression, const char*) {
            throw utils::AssertionFailure(location, line, expression, "overloaded assertion");
    };

    const char* tbbbind_version = system_topology::load_tbbbind_shared_object();
    REQUIRE_MESSAGE(tbbbind_version, "TBBbind must be found");

    set_assertion_handler(custom_handler);

#if TBB_USE_ASSERT
    // Trigger an assertion failure to test the custom handler
    TRY_BAD_EXPR(deallocate_binding_handler_ptr(nullptr), "overloaded assertion");
#endif
}
