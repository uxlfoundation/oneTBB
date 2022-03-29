/*
    Copyright (c) 2022 Intel Corporation

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

#if _WIN32 || _WIN64
#define _CRT_SECURE_NO_WARNINGS
#endif

#define __TBB_NO_IMPLICIT_LINKAGE 1
#include "../../src/tbb/assert_impl.h" // Out-of-line TBB assertion handling routines are instantiated here.

#include "common/test.h"
#include "common/utils.h"

#include "../../src/tbb/literal_const_string.h"

using tbb::detail::r1::literal_const_string;
TEST_CASE("Test creation") {

    REQUIRE_MESSAGE( literal_const_string("test").size() == std::strlen("test"), "Wrong size of non empty string");
    REQUIRE_MESSAGE( std::string(literal_const_string("test").c_str()) == std::string("test"), "");
}

TEST_CASE("Test creation with non-string"
        * doctest::skip() //Test needs to be revised as implementation uses assertions instead of exceptions
) {

    char not_a_tring[] = {'n','o','t',' ','a','s','t','r','i','n','g'};
    REQUIRE_THROWS(literal_const_string(not_a_tring));
}
