/*
    Copyright (C) 2023-2024 Intel Corporation

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

tcm_result_t
client_renegotiate(tcm_permit_handle_t ph, void* arg, tcm_callback_flags_t invocation_reason) {
  std::cout << "Start renegotiation callback \"" << __func__ << "\" for permit=" << ph << ", arg="
            << arg << ", invocation reason={new_concurrency=" << invocation_reason.new_concurrency
            << ", new_state=" << invocation_reason.new_state << "}\n";

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

  std::cout << "End permit renegotiation callback \"" << __func__ << "\"" << std::endl;
  return r ? TCM_RESULT_SUCCESS : TCM_RESULT_ERROR_UNKNOWN;
}

bool test_alternating_clients() {
  const char* test_name = __func__;
  test_prolog(test_name);

  tcm_client_id_t clidA, clidB;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return false;

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return false;

  tcm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           e_concurrency = platform_tcm_concurrency();

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency);

  tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_permit_request_t req = make_request(0, platform_tcm_concurrency());
  r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A") && check_permit(e, pA)))
    return test_fail(test_name);

  r = tcmRegisterThread(phA);
  if (!(check_success(r, "tcmRegisterThread A") &&
        check_permit(e, phA)))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread A") &&
        check_permit(e, phA)))
    return test_fail(test_name);

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit A"))
    return test_fail(test_name);

  r = tcmRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit B") && check_permit(e, pB)))
    return test_fail(test_name);

  r = tcmRegisterThread(phB);
  if (!(check_success(r, "tcmRegisterThread B") &&
        check_permit(e, phB)))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread B") &&
        check_permit(e, phB)))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  if (!check_success(r, "tcmReleasePermit B"))
    return test_fail(test_name);

  r = tcmDisconnect(clidA);
  if (!check_success(r, "tcmDisconnect A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidB);
  if (!check_success(r, "tcmDisconnect B"))
    return test_fail(test_name);

  std::cout << "test_alternating_clients done" << std::endl;
  return test_epilog(test_name);
}

#endif // __TCM_TESTS_COMMON_TESTS_HEADER
