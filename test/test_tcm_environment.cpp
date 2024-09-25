/*
    Copyright (C) 2023-2024 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "tcm.h"

#include "basic_test_utils.h"

#include <iostream>
#include <algorithm>

bool test_getting_version_info_succeeds() {
    const char* test_name = __func__;
    test_prolog(test_name);

    char buffer[1024] = {0};
    tcm_result_t res = tcmGetVersionInfo(buffer, 1024);
    std::fprintf(stderr, "%s", buffer);
    if (!check_success(res, "tcmGetVersionInfo returns successful status"))
        return test_fail(test_name);

    return test_epilog(test_name);
}

bool test_version_info_returns_error_when_given_empty_buffer() {
    const char* test_name = __func__;
    test_prolog(test_name);

    char* buffer = nullptr; const uint32_t buffer_size = 1024;
    tcm_result_t res = tcmGetVersionInfo(buffer, buffer_size);
    if (!check(res == TCM_RESULT_ERROR_INVALID_ARGUMENT, "Test empty buffer"))
        return test_fail(test_name);

    return test_epilog(test_name);
}

bool test_version_info_succeeds_but_writes_nothing_if_given_valid_buffer_with_zero_size() {
    const char* test_name = __func__;
    test_prolog(test_name);

    const unsigned actual_buffer_size = 1024;
    char buffer[actual_buffer_size] = {0};
    const uint32_t incorrect_buffer_size = 0;
    tcm_result_t res = tcmGetVersionInfo(buffer, incorrect_buffer_size);
    if (!check_success(res, "Test zero size"))
        return test_fail(test_name);

    const bool has_written_nothing = std::all_of(buffer, buffer + actual_buffer_size,
                                                 [] (char c) { return c == '\0'; });
    if (!check(has_written_nothing, "Nothing is written in the buffer"))
        return test_fail(test_name);

    return test_epilog(test_name);
}

int main() {
    bool res = true;

    res &= test_getting_version_info_succeeds();
    res &= test_version_info_returns_error_when_given_empty_buffer();
    res &= test_version_info_succeeds_but_writes_nothing_if_given_valid_buffer_with_zero_size();

    return int(!res);
}
