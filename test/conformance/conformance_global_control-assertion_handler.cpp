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

//! \file conformance_global_control-assertion_handler.cpp
//! \brief Test for [sched.global_control] specification

#include "common/test.h"

constexpr char test_name[] =
    "terminate_on_exception: tbb::set_assertion_handler";

// The test does not work correctly when the runtime is statically linked.
// TODO: investigate a failure in debug with MSVC
#if (!_MSC_VER || (defined(_DLL) && !defined(_DEBUG))) && !EMSCRIPTEN

#include "terminate_on_exception.h"

//! Test terminate_on_exception by using tbb::set_assertion_handler and throwing an exception from
//! the library and a std::exception from user code.
//! \brief \ref interface \ref requirement
TEST_CASE(test_name) {
    global_control_terminate_on_exception(TestCase::CUSTOM_ASSERTION_HANDLER);
}
#else // (!_MSC_VER || (defined(_DLL) && !defined(_DEBUG))) && !EMSCRIPTEN
TEST_CASE(test_name * doctest::skip()) {}
#endif
