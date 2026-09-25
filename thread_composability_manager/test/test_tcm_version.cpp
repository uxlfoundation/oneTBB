/*
   Copyright (c) 2023 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "tcm.h"

#include "basic_test_utils.h"

#include <cstring>

TEST("Macro version consists of major, minor, and patch components") {
    static_assert(TCM_VERSION / 10000 == TCM_VERSION_MAJOR);
    static_assert(TCM_VERSION % 10000 / 100 == TCM_VERSION_MINOR);
    static_assert(TCM_VERSION % 10000 % 100 == TCM_VERSION_PATCH);
}

#define TCM_TEST_TO_STR_AUX(x) #x
#define TCM_TEST_TO_STR(x) TCM_TEST_TO_STR_AUX(x)
const char* macro_version = TCM_TEST_TO_STR(TCM_VERSION_MAJOR) "."
                            TCM_TEST_TO_STR(TCM_VERSION_MINOR) "."
                            TCM_TEST_TO_STR(TCM_VERSION_PATCH);
#undef TCM_TEST_TO_STR_AUX
#undef TCM_TEST_TO_STR

TEST("tcmRuntimeVersion() returns string containing " + std::string(macro_version)) {
    const char* runtime_version = tcmRuntimeVersion();
    const bool is_equal = std::strcmp(macro_version, runtime_version) == 0;

    check(is_equal, "Runtime version as string equals macro-based version", /*num_indents*/0,
          "Test runs using library of different version than it was compiled with.");
}

TEST("tcmRuntimeInterfaceVersion() returns " + std::to_string(1000 * TCM_VERSION_MAJOR
                                                              + 10 * TCM_VERSION_MINOR))
{
    const unsigned api_version = tcmRuntimeInterfaceVersion();
    const bool is_equal = api_version == 1000 * TCM_VERSION_MAJOR + 10 * TCM_VERSION_MINOR;

    check(is_equal, "Runtime interface version equals macro-based version", /*num_indents*/0,
          "Test runs using library of different version than it was compiled with.");
}

TEST("tcmGetVersion() returns " + std::to_string(TCM_VERSION)) {
    const unsigned tcm_version = tcmGetVersion();
    const bool is_equal = TCM_VERSION == tcm_version;
    check(is_equal, "Runtime version equals to macro-based version", /*num_indents*/0,
          "Test runs using library of different version than it was compiled with.");
}
