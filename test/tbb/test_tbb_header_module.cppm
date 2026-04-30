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

//! \file test_tbb_header_module.cppm
//! \brief C++20 module interface unit to test TBB public API for TU-local entity exposure
module;
// Preprocessing macros from test_tbb_header.cpp
#if __INTEL_COMPILER && _MSC_VER
#pragma warning(disable : 2586) // decorated name length exceeded, name was truncated
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#define __TBB_NO_IMPLICIT_LINKAGE 1
#define __TBB_TEST_MODULE_EXPORT 1

#define CHECK(x) do { if (!(x)) { std::terminate(); } } while (false)
#define CHECK_MESSAGE(x, y) CHECK(x);

#include "common/config.h"
#include "oneapi/tbb/detail/_config.h"
#include "tbb/tbb.h"

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <tuple>
#include <vector>

export module tbb_header_test;

#include "test_tbb_header.cpp"
