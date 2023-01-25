/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#include "test_utils.h"

#include <iostream>

// permits expected for renegotiation
// TODO: rename to "expected_callback_invocation" or something similar
// TODO: wrap checking comparison in the tests into function with reporting
std::set<zerm_permit_handle_t*> renegotiating_permits;

ze_result_t client_renegotiate(zerm_permit_handle_t ph, void* arg,
                               zerm_callback_flags_t invocation_reason)
{
  const char* test_name = "client_renegotiate callback";
  test_prolog(test_name);

  bool r = true;

  r &= check(invocation_reason.new_concurrency,
             "Reason invoking callback is a new concurrency value");

  zerm_permit_handle_t* permit_via_arg = (zerm_permit_handle_t*)arg;
  r &= check(ph == *permit_via_arg, "Renegotiates for expected arg");

  auto count = renegotiating_permits.count(permit_via_arg);
  r &= check(count == 1, "Renegotiates for expected permit");

  // remove permit from the expected set, to make sure renegotiation does not
  // happen twice for it.
  renegotiating_permits.erase(permit_via_arg);

  test_epilog(test_name);
  return r ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_UNKNOWN;
}

bool test_alternating_clients() {
  const char* test_name = "test_alternating_clients";
  test_prolog(test_name);

  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           e_concurrency = total_number_of_threads;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency);

  zerm_permit_t e = make_active_permit(&e_concurrency);

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A") && check_permit(e, pA)))
    return test_fail(test_name);

  r = zermRegisterThread(phA);
  if (!(check_success(r, "zermRegisterThread A") &&
        check_permit(e, phA)))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread A") &&
        check_permit(e, phA)))
    return test_fail(test_name);

  r = zermReleasePermit(phA);
  if (!check_success(r, "zermReleasePermit A"))
    return test_fail(test_name);

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit B") && check_permit(e, pB)))
    return test_fail(test_name);

  r = zermRegisterThread(phB);
  if (!(check_success(r, "zermRegisterThread B") &&
        check_permit(e, phB)))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread B") &&
        check_permit(e, phB)))
    return test_fail(test_name);

  r = zermReleasePermit(phB);
  if (!check_success(r, "zermReleasePermit B"))
    return test_fail(test_name);

  r = zermDisconnect(clidA);
  if (!check_success(r, "zermDisconnect A"))
    return test_fail(test_name);

  r = zermDisconnect(clidB);
  if (!check_success(r, "zermDisconnect B"))
    return test_fail(test_name);

  std::cout << "test_alternating_clients done" << std::endl;
  return test_epilog(test_name);
}
