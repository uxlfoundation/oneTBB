/*
    Copyright (c) 2021-2023 Intel Corporation
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
