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

enum Workload : int {
    INTERNAL_EXCEPTION = 1,
    USER_EXCEPTION = 2
};

enum class ExpectedException {
    INVALID_ARGUMENT,
    STD_EXCEPTION
};

// Overall, the test case is not safe because the dtors might not be called during long jump.
// Therefore, it makes sense to run the test case after all other test cases.
//! Test terminate_on_exception behavior
//! \brief \ref interface \ref requirement
void global_control_terminate_on_exception(TestCase test_case,
        int workload = Workload::INTERNAL_EXCEPTION|Workload::USER_EXCEPTION) {
    oneapi::tbb::global_control c(oneapi::tbb::global_control::terminate_on_exception, 1);
    static bool terminate_handler_called;
    terminate_handler_called = false;
    static std::atomic<ExpectedException> expected_exception;

#if TBB_USE_EXCEPTIONS
    try {
#endif
        static std::jmp_buf buffer;
        volatile std::terminate_handler prev_terminate_handler = nullptr;
        tbb::assertion_handler_type prev_assertion_handler = nullptr;

        if (test_case == TestCase::SET_TERMINATE) {
            prev_terminate_handler = std::set_terminate([] {
                CHECK(!terminate_handler_called);
                terminate_handler_called = true;
                std::longjmp(buffer, 1);
            });
        } else if (test_case == TestCase::CUSTOM_ASSERTION_HANDLER) {
            prev_assertion_handler =
                tbb::set_assertion_handler([](const char* location, int line,
                                                   const char* expression, const char* comment) {
                REQUIRE(!terminate_handler_called);
                terminate_handler_called = true;
                REQUIRE(location);
                REQUIRE(location[0] == 0);
                REQUIRE(!line);
                REQUIRE(expression);
                REQUIRE(expression[0] == 0);
                REQUIRE(comment);
                if (strstr(comment, "std::invalid_argument"))
                    REQUIRE(expected_exception == ExpectedException::INVALID_ARGUMENT);
                else if (strstr(comment, "std::exception"))
                    REQUIRE(expected_exception == ExpectedException::STD_EXCEPTION);
                else
                    REQUIRE(false); // Unexpected exception type

#if defined(__clang__) && defined(_MSC_VER)
                // On Clang with MSVC, longjmp from within noexcept leads to std::terminate call,
                // so exit instead. Under global_control::terminate_on_exception, we have noexcept.
                // Must use REQUIRE instead of CHECK to ensure the test fails immediately if the
                // condition is not met.
                _Exit(0);
#else
                std::longjmp(buffer, 1);
#endif
            });
        }
        if (workload & Workload::INTERNAL_EXCEPTION)
#if _MSC_VER
#pragma warning(push)
#pragma warning(disable:4611) // interaction between '_setjmp' and C++ object destruction is non - portable
#endif
            SUBCASE("internal exception") {
                if (setjmp(buffer) == 0) {
                    expected_exception = ExpectedException::INVALID_ARGUMENT;
                    oneapi::tbb::parallel_for(0, 1, -1, [](int) {});
                    FAIL("Unreachable code");
                }
            }
#if TBB_USE_EXCEPTIONS
        if (workload & Workload::USER_EXCEPTION)
            SUBCASE("user exception") {
                if (setjmp(buffer) == 0) {
                    expected_exception = ExpectedException::STD_EXCEPTION;
                    oneapi::tbb::parallel_for(0, 1, [](int) {
                        volatile bool suppress_unreachable_code_warning = true;
                        if (suppress_unreachable_code_warning) {
                            throw std::exception();
                        }
                    });
                    FAIL("Unreachable code");
                }
            }
#endif
#if _MSC_VER
#pragma warning(pop)
#endif
        if (test_case == TestCase::SET_TERMINATE) {
            std::set_terminate(prev_terminate_handler);
        } else if (test_case == TestCase::CUSTOM_ASSERTION_HANDLER) {
            tbb::set_assertion_handler(prev_assertion_handler);
        }
#if TBB_USE_EXCEPTIONS
    } catch (...) {
        FAIL("The exception is not expected in terminate_on_exception mode");
    }
#endif
    CHECK(terminate_handler_called);
}

#endif // TEST_CONFORMANCE_TERMINATE_ON_EXCEPTION_H
