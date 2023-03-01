/*
    Copyright (c) 2021-2023 Intel Corporation
*/

#ifndef __TCM_TESTS_COMMON_TESTS_HEADER
#define __TCM_TESTS_COMMON_TESTS_HEADER

#include "test_utils.h"

#include <iostream>

// permits expected for renegotiation
// TODO: rename to "expected_callback_invocation" or something similar
// TODO: wrap checking comparison in the tests into function with reporting
std::set<tcm_permit_handle_t*> renegotiating_permits;

tcm_result_t client_renegotiate(tcm_permit_handle_t ph, void* arg,
                               tcm_callback_flags_t invocation_reason)
{
  const char* test_name = "client_renegotiate callback";
  test_prolog(test_name);

  bool r = true;

  r &= check(invocation_reason.new_concurrency,
             "Reason invoking callback is a new concurrency value");

  tcm_permit_handle_t* permit_via_arg = (tcm_permit_handle_t*)arg;
  r &= check(permit_via_arg, "Callback arg is not nullptr.");
  r &= check(ph == *permit_via_arg, "Renegotiates for expected arg.");

  auto count = renegotiating_permits.count(permit_via_arg);
  r &= check(count == 1, "Renegotiates for expected permit");

  // remove permit from the expected set, to make sure renegotiation does not
  // happen twice for it.
  renegotiating_permits.erase(permit_via_arg);

  test_epilog(test_name);
  return r ? TCM_RESULT_SUCCESS : TCM_RESULT_ERROR_UNKNOWN;
}

bool test_alternating_clients() {
  const char* test_name = "test_alternating_clients";
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
           e_concurrency = num_oversubscribed_resources;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency);

  tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_permit_request_t req = make_request(0, num_oversubscribed_resources);
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
