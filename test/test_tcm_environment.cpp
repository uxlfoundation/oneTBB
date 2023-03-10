/*
    Copyright (c) 2023 Intel Corporation
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
