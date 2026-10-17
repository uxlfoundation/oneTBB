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
#if !defined(TEST_CONFORMANCE_TERMINATE_ON_EXCEPTION_H)
#define TEST_CONFORMANCE_TERMINATE_ON_EXCEPTION_H

#include "oneapi/tbb/global_control.h"
#include "oneapi/tbb/parallel_for.h"

#include <csetjmp>

enum class TestCase {
    SET_TERMINATE,
    CUSTOM_ASSERTION_HANDLER
};

// Overall, the test case is not safe because the dtors might not be called during long jump.
// Therefore, it makes sense to run the test case after all other test cases.
//! Test terminate_on_exception behavior
//! \brief \ref interface \ref requirement
void global_control_terminate_on_exception(TestCase test_case) {
    oneapi::tbb::global_control c(oneapi::tbb::global_control::terminate_on_exception, 1);
    static bool terminate_handler_called;
    static bool invalid_argument_exception;
    static bool std_exception;
    terminate_handler_called = false;
    invalid_argument_exception = false;
    std_exception = false;

#if TBB_USE_EXCEPTIONS
    try {
#endif
        static std::jmp_buf buffer;
        volatile std::terminate_handler prev_terminate_handler = nullptr;
        tbb::ext::assertion_handler_type prev_assertion_handler = nullptr;

        if (test_case == TestCase::SET_TERMINATE) {
            prev_terminate_handler = std::set_terminate([] {
                CHECK(!terminate_handler_called);
                terminate_handler_called = true;
                std::longjmp(buffer, 1);
            });
        } else if (test_case == TestCase::CUSTOM_ASSERTION_HANDLER) {
            prev_assertion_handler =
                tbb::ext::set_assertion_handler([](const char* location, int line,
                                                   const char* expression, const char* comment) {
                CHECK(!terminate_handler_called);
                terminate_handler_called = true;
                CHECK(!location);
                CHECK(!line);
                CHECK(!expression);
                CHECK(comment);
                if (strstr(comment, "std::invalid_argument"))
                    invalid_argument_exception = true;
                else if (strstr(comment, "std::exception"))
                    std_exception = true;
                std::longjmp(buffer, 1);
            });
        }
#if _MSC_VER
#pragma warning(push)
#pragma warning(disable:4611) // interaction between '_setjmp' and C++ object destruction is non - portable
#endif
        SUBCASE("internal exception") {
            if (setjmp(buffer) == 0) {
                oneapi::tbb::parallel_for(0, 1, -1, [](int) {});
                FAIL("Unreachable code");
            }
            if (test_case == TestCase::CUSTOM_ASSERTION_HANDLER) {
                CHECK(invalid_argument_exception);
                CHECK(!std_exception);
            }
        }
#if TBB_USE_EXCEPTIONS
        SUBCASE("user exception") {
            if (setjmp(buffer) == 0) {
                oneapi::tbb::parallel_for(0, 1, [](int) {
                    volatile bool suppress_unreachable_code_warning = true;
                    if (suppress_unreachable_code_warning) {
                        throw std::exception();
                    }
                });
                FAIL("Unreachable code");
            }
            if (test_case == TestCase::CUSTOM_ASSERTION_HANDLER) {
                CHECK(!invalid_argument_exception);
                CHECK(std_exception);
            }
        }
#endif
#if _MSC_VER
#pragma warning(pop)
#endif
        if (test_case == TestCase::SET_TERMINATE) {
            std::set_terminate(prev_terminate_handler);
        } else if (test_case == TestCase::CUSTOM_ASSERTION_HANDLER) {
            tbb::ext::set_assertion_handler(prev_assertion_handler);
        }
#if TBB_USE_EXCEPTIONS
    } catch (...) {
        FAIL("The exception is not expected");
    }
#endif
    CHECK(terminate_handler_called);
}

#endif // TEST_CONFORMANCE_TERMINATE_ON_EXCEPTION_H
