/*
   Copyright (c) 2023 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include "tcm.h"

#include "basic_test_utils.h"

#include <algorithm>

TEST("test_getting_version_info_succeeds") {
    char buffer[1024] = {0};
    tcm_result_t res = tcmGetVersionInfo(buffer, 1024);
    std::fprintf(stderr, "%s", buffer);
    check_success(res, "tcmGetVersionInfo returns successful status");
}

TEST("test_version_info_returns_error_when_given_empty_buffer") {
    char* buffer = nullptr; const uint32_t buffer_size = 1024;
    tcm_result_t res = tcmGetVersionInfo(buffer, buffer_size);
    check(res == TCM_RESULT_ERROR_INVALID_ARGUMENT, "Test empty buffer");
}

TEST("test_version_info_succeeds_but_writes_nothing_if_given_valid_buffer_with_zero_size") {
    const unsigned actual_buffer_size = 1024;
    char buffer[actual_buffer_size] = {0};
    const uint32_t incorrect_buffer_size = 0;
    tcm_result_t res = tcmGetVersionInfo(buffer, incorrect_buffer_size);
    check_success(res, "Test zero size");

    const bool has_written_nothing = std::all_of(buffer, buffer + actual_buffer_size,
                                                 [] (char c) { return c == '\0'; });
    check(has_written_nothing, "Nothing is written in the buffer");
}
