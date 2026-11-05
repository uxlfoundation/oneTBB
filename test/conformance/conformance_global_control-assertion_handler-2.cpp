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

//! \file conformance_global_control-assertion_handler-2.cpp
//! \brief Test for [sched.global_control] specification

#include "common/test.h"

constexpr char test_name[] =
    "terminate_on_exception: uses tbb::set_assertion_handler, "
    "throws not inherited from std::exception";

// The test cannot work correctly with statically linked runtime.
// TODO: investigate a failure in debug with MSVC
#if TBB_USE_EXCEPTIONS && (!_MSC_VER || (defined(_DLL) && !defined(_DEBUG))) && !EMSCRIPTEN

#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/parallel_for.h"

#include <csetjmp>

//! Test terminate_on_exception: use tbb::set_assertion_handler, throw from user code an exception
//! not inherited from std::exception
//! \brief \ref interface \ref requirement
TEST_CASE(test_name) {
    oneapi::tbb::global_control c(oneapi::tbb::global_control::terminate_on_exception, 1);
    static bool terminate_handler_called;

    try {
        static std::jmp_buf buffer;
        tbb::assertion_handler_type prev_assertion_handler = nullptr;

        prev_assertion_handler =
            tbb::set_assertion_handler([](const char* location, int line,
                                          const char* expression, const char* comment) {
                CHECK(!terminate_handler_called);
                terminate_handler_called = true;
                CHECK(location);
                CHECK(location[0] == 0);
                CHECK(!line);
                CHECK(expression);
                CHECK(expression[0] == 0);
                CHECK(comment);
                const char *expected_comment = "Unknown exception";
                CHECK_MESSAGE(strcmp(comment, expected_comment) == 0, "expected \"" << std::string(expected_comment) << "\", got: "
                              << std::string(comment));
                std::longjmp(buffer, 1);
            });
#if _MSC_VER
#pragma warning(push)
#pragma warning(disable:4611) // interaction between '_setjmp' and C++ object destruction is non-portable
#endif
        if (setjmp(buffer) == 0) {
            oneapi::tbb::parallel_for(0, 1, [](int) {
                volatile bool suppress_unreachable_code_warning = true;
                if (suppress_unreachable_code_warning) {
                    throw int{1};
                }
            });
            FAIL("Unreachable code");
        }
        CHECK(terminate_handler_called);
#if _MSC_VER
#pragma warning(pop)
#endif
        tbb::set_assertion_handler(prev_assertion_handler);
    } catch (...) {
        FAIL("The exception is not expected");
    }
    CHECK(terminate_handler_called);
}
#else
TEST_CASE(test_name * doctest::skip()) {}
#endif
