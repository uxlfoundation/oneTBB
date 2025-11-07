/*
    Copyright (C) 2023-2025 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_TESTS_COMMON_TESTS_HEADER
#define __TCM_TESTS_COMMON_TESTS_HEADER

#include "test_utils.h"

#include <iostream>

// permits expected for renegotiation
// TODO: rename to "expected_callback_invocation" or something similar
// TODO: wrap checking comparison in the tests into function with reporting
std::set<tcm_permit_handle_t> renegotiating_permits;

bool allow_null_in_callback_arg = false;
bool is_client_renegotiate_callback_invoked = false;

tcm_result_t client_renegotiate(tcm_permit_handle_t ph, void *arg,
                                tcm_callback_flags_t invocation_reason)
{
  logger.log("Start renegotiation callback \"" + std::string(__func__) +
             "\" for permit=" + to_string(ph) + ", arg=" + to_string(arg) +
             ", invocation reason={new_concurrency=" +
             std::to_string(invocation_reason.new_concurrency) + ", new_state=" +
             std::to_string(invocation_reason.new_state) + "}\n");

  is_client_renegotiate_callback_invoked = true;

  bool r = check(invocation_reason.new_concurrency,
                 "Reason invoking callback is a new concurrency value");

  if (!allow_null_in_callback_arg) {
      tcm_permit_handle_t* permit_via_arg = (tcm_permit_handle_t*)arg;
      r &= check(permit_via_arg, "Callback arg is not nullptr.");
      r &= check(ph == *permit_via_arg, "Renegotiates for expected arg.");
  }

  const auto count = renegotiating_permits.count(ph);
  r &= check(count == 1, "Renegotiates for expected permit");

  // Remove permit from the expected set, to make sure renegotiation does not happen twice for it.
  renegotiating_permits.erase(ph);

  logger.log("End permit renegotiation callback \"" + std::string(__func__) + "\"");
  return r ? TCM_RESULT_SUCCESS : TCM_RESULT_ERROR_UNKNOWN;
}

#endif // __TCM_TESTS_COMMON_TESTS_HEADER
