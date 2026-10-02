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

// The test cannot work correctly with statically linked runtime.
// TODO: investigate a failure in debug with MSVC
#if (!_MSC_VER || (defined(_DLL) && !defined(_DEBUG))) && !EMSCRIPTEN

#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/parallel_for.h"

#include <csetjmp>

//! Test terminate_on_exception: tbb::ext::set_assertion_handler with an exception
//! not inherited from std::exception
//! \brief \ref interface \ref requirement
TEST_CASE("terminate_on_exception: tbb::ext::set_assertion_handler") {
    oneapi::tbb::global_control c(oneapi::tbb::global_control::terminate_on_exception, 1);
    static bool terminate_handler_called;

#if TBB_USE_EXCEPTIONS
    try {
#endif
        static std::jmp_buf buffer;
        tbb::ext::assertion_handler_type prev_assertion_handler = nullptr;

            prev_assertion_handler =
                tbb::ext::set_assertion_handler([](const char* location, int line,
                                                   const char* expression, const char* comment) {
                CHECK(!terminate_handler_called);
                terminate_handler_called = true;
                CHECK(!location);
                CHECK(!line);
                CHECK(!expression);
                CHECK(comment);
                CHECK(strcmp(comment, "Unknown exception") == 0);
                std::longjmp(buffer, 1);
            });
#if TBB_USE_EXCEPTIONS
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
#endif
#if _MSC_VER
#pragma warning(pop)
#endif
            tbb::ext::set_assertion_handler(prev_assertion_handler);
#if TBB_USE_EXCEPTIONS
    } catch (...) {
        FAIL("The exception is not expected");
    }
#endif
    CHECK(terminate_handler_called);
}
#else // (!_MSC_VER || (defined(_DLL) && !defined(_DEBUG))) && !EMSCRIPTEN
TEST_CASE("terminate_on_exception: enabled" * doctest::skip()) {}
#endif
