/*
    Copyright (C) 2024 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_TESTS_BASIC_TEST_UTILS_HEADER
#define __TCM_TESTS_BASIC_TEST_UTILS_HEADER

/***************************************************************************************************
 * This file contains helper functions for testing infrastructure that depend only on standard C++
 * and OS API
 **************************************************************************************************/

#include "tcm/detail/_tcm_assert.h"

#include "tcm.h"

#include <string>
#include <iostream>
#include <sstream>

// MSVC Warning: Args can be incorrect: this does not match function name specification
__TCM_SUPPRESS_WARNING_WITH_PUSH(6387)
inline int SetEnv( const char *envname, const char *envval ) {
    __TCM_ASSERT( (envname && envval), "SetEnv requires two valid C strings" );
#if !(_MSC_VER || __MINGW32__ || __MINGW64__)
    // On POSIX systems use setenv
    return setenv(envname, envval, /*overwrite=*/1);
#elif __STDC_SECURE_LIB__>=200411
    // this macro is set in VC & MinGW if secure API functions are present
    return _putenv_s(envname, envval);
#else
    // If no secure API on Windows, use _putenv
    size_t namelen = strlen(envname), valuelen = strlen(envval);
    char* buf = new char[namelen+valuelen+2];
    strncpy(buf, envname, namelen);
    buf[namelen] = '=';
    strncpy(buf+namelen+1, envval, valuelen);
    buf[namelen+1+valuelen] = char(0);
    int status = _putenv(buf);
    delete[] buf;
    return status;
#endif
}

char* GetEnv(const char* envname) {
    __TCM_ASSERT(envname, "GetEnv requires valid C string");
    return std::getenv(envname);
}
__TCM_SUPPRESS_WARNING_POP


/***************************************************************************************************
 * TCM-specific helpers
 **************************************************************************************************/

tcm_permit_request_t make_request(int min_sw_threads = tcm_automatic,
                                  int max_sw_threads = tcm_automatic,
                                  tcm_cpu_constraints_t* constraints = nullptr, uint32_t size = 0,
                                  tcm_request_priority_t priority = TCM_REQUEST_PRIORITY_NORMAL,
                                  tcm_permit_flags_t flags = {})
{
    __TCM_ASSERT(!(!constraints ^ !size), "Inconsistent request.");

    return tcm_permit_request_t{
        min_sw_threads, max_sw_threads, constraints, size, priority, flags, /*reserved*/{0}
    };
}

tcm_permit_t make_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                         uint32_t size = 1, tcm_permit_state_t state = TCM_PERMIT_STATE_VOID,
                         tcm_permit_flags_t flags = {})
{
  __TCM_ASSERT(concurrencies, "Array of concurrencies cannot be nullptr.");
  return tcm_permit_t{concurrencies, cpu_masks, size, state, flags};
}

tcm_permit_t make_void_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                               uint32_t size = 1, tcm_permit_flags_t flags = {})
{
    return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_VOID, flags);
}

tcm_permit_t make_active_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                                 uint32_t size = 1, tcm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_ACTIVE, flags);
}

tcm_permit_t make_inactive_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                                  uint32_t size = 1, tcm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_INACTIVE, flags);
}

tcm_permit_t make_pending_permit(uint32_t* concurrencies, tcm_cpu_mask_t* cpu_masks = nullptr,
                                  uint32_t size = 1, tcm_permit_flags_t flags = {})
{
  return make_permit(concurrencies, cpu_masks, size, TCM_PERMIT_STATE_PENDING, flags);
}


/***************************************************************************************************
 * Testing and reporting functions
 **************************************************************************************************/

bool check(bool b, const std::string& msg, unsigned num_indents = 0,
           const std::string& report_msg = "")
{
  const std::string indent(2 * num_indents, ' '); // Multiplied by two for clearer line distinction

  if (!b) {
    std::cout << "***************** " << indent << msg << std::endl;
    std::cout << "*     ERROR     * " << indent << msg << std::endl;
    std::cout << "***************** " << indent << msg << std::endl;
    std::cout << report_msg;
  }  else if (!msg.empty()){
    std::cout << "SUCCESS: " << indent << msg << std::endl;
  }
  return b;
}

inline void test_prolog(const std::string& msg) {
  std::cout << "\n\nSUCCESS: begin " << msg << std::endl;
}

inline bool test_stop(bool b, const std::string& msg) {
  return check(b, "end " + msg);
}

inline bool test_fail(const std::string& msg) {
  return test_stop(false, msg);
}

inline bool test_epilog(const std::string& msg) {
  return test_stop(true, msg);
}

inline bool succeeded(tcm_result_t res) {
  return (TCM_RESULT_SUCCESS == res);
}

inline bool check_success(tcm_result_t res, const std::string& msg = "",
                          const std::string& report_msg = "")
{
  return check(succeeded(res), msg, /*num_indents*/0, report_msg);
}

inline bool check_fail(tcm_result_t res, const std::string& msg = "",
                       const std::string& report_msg = "")
{
  return check(!succeeded(res), msg, /*num_indents*/0, report_msg);
}

#endif // __TCM_TESTS_BASIC_TEST_UTILS_HEADER
