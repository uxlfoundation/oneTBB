/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_TESTS_TEST_EXCEPTIONS_HEADER
#define __TCM_TESTS_TEST_EXCEPTIONS_HEADER

/*
 * File defines a set of exceptions that can occur during the test run.
 */

#include <exception>

class tcm_exception : public std::exception {
public:
  tcm_exception(const char* message) : m_message(message) {}
  const char* what() const noexcept override { return m_message; }
private:
  const char* m_message = nullptr;
};

class tcm_connect_error : public tcm_exception {
public:
  tcm_connect_error(const char* message = nullptr) : tcm_exception(message) {}
};

class tcm_request_permit_error : public tcm_exception {
public:
  tcm_request_permit_error(const char* message = nullptr) : tcm_exception(message) {}
};

class tcm_get_permit_data_error : public tcm_exception {
public:
  tcm_get_permit_data_error(const char* message = nullptr) : tcm_exception(message) {}
};

class tcm_release_permit_error : public tcm_exception {
public:
  tcm_release_permit_error(const char* message = nullptr) : tcm_exception(message) {}
};

class tcm_disconnect_error : public tcm_exception {
public:
  tcm_disconnect_error(const char* message = nullptr) : tcm_exception(message) {}
};

#endif // __TCM_TESTS_TEST_EXCEPTIONS_HEADER
