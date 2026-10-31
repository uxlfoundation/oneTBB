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

#if !defined(TEST_CONFORMANCE_THROW_IN_ASSERTION_HANDLER_H)
#define TEST_CONFORMANCE_THROW_IN_ASSERTION_HANDLER_H

#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/parallel_for.h"

#include <csetjmp>

enum class TestCase {
    THROW_FROM_USER_CODE,
    THROW_FROM_LIBRARY
};

void throw_in_assertion_handler(TestCase test_case) {
    oneapi::tbb::global_control c(oneapi::tbb::global_control::terminate_on_exception, 1);
    static bool terminate_handler_called;
    static bool assertion_handler_called;
    static TestCase my_test_case;

    terminate_handler_called = false;
    assertion_handler_called = false;
    my_test_case = test_case;

    try {
        static std::jmp_buf buffer;

        std::terminate_handler prev_terminate_handler = std::set_terminate([] {
            CHECK(assertion_handler_called);
            CHECK(!terminate_handler_called);
            terminate_handler_called = true;
            std::longjmp(buffer, 1);
        });

        tbb::assertion_handler_type prev_assertion_handler =
            tbb::set_assertion_handler([](const char* location, int line,
                                                   const char* expression, const char* comment) {
                CHECK(!assertion_handler_called);
                assertion_handler_called = true;
                CHECK(location);
                CHECK(location[0] == 0);
                CHECK(!line);
                CHECK(expression);
                CHECK(expression[0] == 0);
                CHECK(comment);
                const char *expected_comment = my_test_case == TestCase::THROW_FROM_LIBRARY ?
                    "std::invalid_argument" : "Unknown exception";
                CHECK_MESSAGE(strstr(comment, expected_comment),
                              "expected \"" << std::string(expected_comment) << "\", got: "
                               << std::string(comment));
                throw std::runtime_error("Assertion handler triggered");
            });
#if _MSC_VER
#pragma warning(push)
#pragma warning(disable:4611) // interaction between '_setjmp' and C++ object destruction is non-portable
#endif
        if (setjmp(buffer) == 0) {
            if (test_case == TestCase::THROW_FROM_LIBRARY) {
                oneapi::tbb::parallel_for(0, 1, -1, [](int) {});
            } else {
                oneapi::tbb::parallel_for(0, 1, [](int) {
                    volatile bool suppress_unreachable_code_warning = true;
                    if (suppress_unreachable_code_warning) {
                        throw int{1};
                    }
                });
            }
            FAIL("Unreachable code");
        }
        CHECK(terminate_handler_called);
#if _MSC_VER
#pragma warning(pop)
#endif
        tbb::set_assertion_handler(prev_assertion_handler);
        std::set_terminate(prev_terminate_handler);
    } catch (...) {
        FAIL("The exception is not expected in terminate_on_exception mode");
    }
    CHECK(terminate_handler_called);
}

#endif // TEST_CONFORMANCE_THROW_IN_ASSERTION_HANDLER_H
