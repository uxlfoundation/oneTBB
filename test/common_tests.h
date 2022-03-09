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
  check(true, "begin client_renegotiate callback");
  bool r = true;

  r &= check(invocation_reason.new_concurrency,
             "Reason invoking callback is new concurrency value");

  zerm_permit_handle_t* permit_via_arg = (zerm_permit_handle_t*)arg;
  r &= check(ph == *permit_via_arg, "Renegotiates for expected arg");

  auto count = renegotiating_permits.count(permit_via_arg);
  r &= check(count == 1, "Renegotiates for expected permit");

  // remove permit from the expected set, to make sure renegotiation does not
  // happen twice for it.
  renegotiating_permits.erase(permit_via_arg);

  check(true, "end client_renegotiate callback");
  return r ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_UNKNOWN;
}

bool test_alternating_clients() {
  check(true, "\n\nbegin test_alternating_clients");
  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           e_concurrency = total_number_of_threads;

  zerm_permit_t pA{&pA_concurrency, nullptr}, pB{&pB_concurrency, nullptr};
  zerm_permit_t e = {&e_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A") && check_permit(e, pA)))
    return check(false, "end test_alternating_clients");

  r = zermRegisterThread(phA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread A") &&
        check_permit(e, phA)))
    return check(false, "end test_alternating_clients");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread A") &&
        check_permit(e, phA)))
    return check(false, "end test_alternating_clients");

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit A"))
    return check(false, "end test_alternating_clients");

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B") && check_permit(e, pB)))
    return check(false, "end test_alternating_clients");

  r = zermRegisterThread(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread B") &&
        check_permit(e, phB)))
    return check(false, "end test_alternating_clients");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread B") &&
        check_permit(e, phB)))
    return check(false, "end test_alternating_clients");

  r = zermReleasePermit(phB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B"))
    return check(false, "end test_alternating_clients");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect A"))
    return check(false, "end test_alternating_clients");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect B"))
    return check(false, "end test_alternating_clients");

  std::cout << "test_alternating_clients done" << std::endl;
  return check(true, "end test_alternating_clients");
}
