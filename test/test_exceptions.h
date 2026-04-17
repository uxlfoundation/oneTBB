/*
   Copyright (c) 2023 Intel Corporation

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

#ifndef __TCM_TESTS_TEST_EXCEPTIONS_HEADER
#define __TCM_TESTS_TEST_EXCEPTIONS_HEADER

/*
 * File defines a set of exceptions that can occur during the test run.
 */

#include <exception>
#include <string>

class tcm_exception : public std::exception {
public:
  tcm_exception(const std::string& message) : m_message(message) {}
  const char* what() const noexcept override { return m_message.c_str(); }
private:
  std::string m_message = "";
};

class tcm_connect_error : public tcm_exception {
public:
  tcm_connect_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_request_permit_error : public tcm_exception {
public:
  tcm_request_permit_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_activate_permit_error : public tcm_exception {
public:
  tcm_activate_permit_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_deactivate_permit_error : public tcm_exception {
public:
  tcm_deactivate_permit_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_idle_permit_error : public tcm_exception {
public:
  tcm_idle_permit_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_get_permit_data_error : public tcm_exception {
public:
  tcm_get_permit_data_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_release_permit_error : public tcm_exception {
public:
  tcm_release_permit_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_disconnect_error : public tcm_exception {
public:
  tcm_disconnect_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_register_thread_error : public tcm_exception {
public:
  tcm_register_thread_error(const std::string& message = "") : tcm_exception(message) {}
};

class tcm_unregister_thread_error : public tcm_exception {
public:
  tcm_unregister_thread_error(const std::string& message = "") : tcm_exception(message) {}
};

#endif // __TCM_TESTS_TEST_EXCEPTIONS_HEADER
