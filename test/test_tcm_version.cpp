/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "test_utils.h"
#include "tcm.h"

#include <cstring>

bool test_runtime_version() {
    const char* test_name = "test_runtime_version";
    test_prolog(test_name);
    bool res = true;

    static_assert(TCM_INTERFACE_VERSION / 1000 == TCM_INTERFACE_VERSION_MAJOR);
    static_assert(TCM_INTERFACE_VERSION % 1000 / 10 == TCM_INTERFACE_VERSION_MINOR);

    const char* runtime_version_string = tcmRuntimeVersion();
    bool is_equal = std::strcmp(runtime_version_string, TCM_VERSION) == 0;
    res &= check(is_equal,
        "Test runtime version",
        "Running with the library of different version than the test was compiled against."
    );

    unsigned api_version = tcmRuntimeInterfaceVersion();
    is_equal = api_version == TCM_INTERFACE_VERSION;
    res &= check(is_equal,
        "Test runtime API version",
        "Running with the library of different version than the test was compiled against."
    );

    return test_stop(res, test_name);
}

int main() {
    return test_runtime_version() == true ? 0 : 1;
}
