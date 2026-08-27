/*
   Copyright (c) 2023 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#include <iostream>
#include <string>
#include <cstdlib>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    std::string randomString(reinterpret_cast<const char*>(data), size);
    setenv("TCM_VERSION", randomString.c_str(), 1);

    const char* executablePath =
#ifdef TEST_EXECUTABLE_DIR
    TEST_EXECUTABLE_DIR
#endif
    "/test_basic_apis >>log.txt 2>>log.txt";

    int result = std::system(executablePath);
    return result != 0 ? -1 : 0;  // Values other than 0 and -1 are reserved for future use.
}
