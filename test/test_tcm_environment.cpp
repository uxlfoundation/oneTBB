/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "tcm.h"

#include <iostream>
#include "test_utils.h"

bool test_print() {
    char buffer[1024];
    tcm_result_t res = tcmGetVersionInfo(buffer, 1024);
    std::fprintf(stderr, "%s", buffer);
    return check(res == TCM_RESULT_SUCCESS, "Test correct print");
}

bool error_guessing() {
    const char* test_name = "Test TCM stability when passing incorrect buffers";
    test_prolog(test_name);
    bool test_result = true;
    {
        char* buffer = nullptr;
        tcm_result_t res = tcmGetVersionInfo(buffer, 1024);
        test_result &= check(res == TCM_RESULT_ERROR_INVALID_ARGUMENT, "Test empty buffer");
    }
    {
        char buffer[1024];
        tcm_result_t res = tcmGetVersionInfo(buffer, 0);
        test_result &= check(res == TCM_RESULT_SUCCESS, "Test zero size");
    }

    return test_result;
}

int main() {
    bool res = true;
    res &= test_print();
    res &= error_guessing();
    return int(!res);
}
