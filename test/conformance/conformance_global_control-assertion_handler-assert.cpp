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

//! \file conformance_global_control-assertion_handler-assert.cpp
//! \brief Test for [sched.global_control] specification

#include "common/test.h"

constexpr char test_name[] =
    "terminate_on_exception: assert from user code, then throw from assertion handler";

// The test does not work correctly when the runtime is statically linked.
// TODO: investigate a failure in debug with MSVC
#if TBB_USE_EXCEPTIONS && (!_MSC_VER || (defined(_DLL) && !defined(_DEBUG))) && !EMSCRIPTEN

#include "throw_in_assertion_handler.h"

//! Test terminate_on_exception: user code triggers an assert, then the assertion handler throws an exception
//! that does not inherit from std::exception. This throw from the assertion handler results in std::terminate,
//! which the test intercepts with longjmp.
//! \brief \ref interface \ref requirement
TEST_CASE(test_name) {
    throw_in_assertion_handler(TestCase::ASSERT_FROM_USER_CODE);
}

#else
//! Test terminate_on_exception: skip in this configuration
//! \brief \ref interface \ref requirement
TEST_CASE(test_name * doctest::skip()) {}
#endif
