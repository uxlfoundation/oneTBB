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

#include "common/test.h"
#include "oneapi/tbb/detail/_utils.h"

//! \file test_utils.cpp
//! \brief Test for [internal] functionality

TEST_CASE("Test alignment utility functions") {
    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(0, 1) == 0);
    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(1, 1) == 1);

    CHECK(tbb::detail::align_to_greater<std::size_t>(0, 1) == 1);
    CHECK(tbb::detail::align_to_greater<std::size_t>(1, 1) == 2);

    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(64, 64) == 64);
    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(128, 64) == 128);

    CHECK(tbb::detail::align_to_greater<std::size_t>(64, 64) == 128);
    CHECK(tbb::detail::align_to_greater<std::size_t>(128, 64) == 192);

    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(1, 64) == 64);
    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(63, 64) == 64);
    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(65, 64) == 128);

    CHECK(tbb::detail::align_to_greater<std::size_t>(1, 64) == 64);
    CHECK(tbb::detail::align_to_greater<std::size_t>(63, 64) == 64);
    CHECK(tbb::detail::align_to_greater<std::size_t>(65, 64) == 128);

    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(4095, 4096) == 4096);
    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(4096, 4096) == 4096);
    CHECK(tbb::detail::align_to_greater_or_equal<std::size_t>(4097, 4096) == 8192);

    CHECK(tbb::detail::align_to_greater<std::size_t>(4095, 4096) == 4096);
    CHECK(tbb::detail::align_to_greater<std::size_t>(4096, 4096) == 8192);
    CHECK(tbb::detail::align_to_greater<std::size_t>(4097, 4096) == 8192);
}
