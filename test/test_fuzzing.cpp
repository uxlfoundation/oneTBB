/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
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
