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

#include "global_control_terminate_on_exception.h"

// Overall, the test case is not safe because the dtors might not be called during long jump.
// Therefore, it makes sense to run the test case after all other test cases.
//! Test terminate_on_exception behavior
//! \brief \ref interface \ref requirement
#if (!_MSC_VER || (defined(_DLL) && !defined(_DEBUG))) && !EMSCRIPTEN
TEST_CASE("terminate_on_exception: enabled") {
    global_control_terminate_on_exception(TestCase::CUSTOM_ASSERTION_HANDLER);
}
#else
TEST_CASE("terminate_on_exception: enabled" * doctest::skip()) {}
#endif