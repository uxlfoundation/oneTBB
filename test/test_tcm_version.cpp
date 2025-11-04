/*
    Copyright (C) 2023-2025 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "tcm.h"

#include "basic_test_utils.h"

#include <cstring>

TEST("test_runtime_version") {
    static_assert(TCM_INTERFACE_VERSION / 1000 == TCM_INTERFACE_VERSION_MAJOR);
    static_assert(TCM_INTERFACE_VERSION % 1000 / 10 == TCM_INTERFACE_VERSION_MINOR);

    const char* runtime_version_string = tcmRuntimeVersion();
    bool is_equal = std::strcmp(runtime_version_string, TCM_VERSION) == 0;
    check(is_equal, "Runtime version equals macro-based version", /*num_indents*/0,
          "Test runs using library of different version than it was compiled with.");

    unsigned api_version = tcmRuntimeInterfaceVersion();
    is_equal &= api_version == TCM_INTERFACE_VERSION;
    check(is_equal, "Runtime API version equals macro-based version", /*num_indents*/0,
          "Test runs using library of different version than it was compiled with.");
}
