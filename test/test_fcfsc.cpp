/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#include "test_utils.h"

#include "tcm.h"

#include "common_tests.h"

#include <iostream>

bool test_nested_clients() {
  const char* test_name = "test_nested_clients";
  test_prolog(test_name);

  zerm_client_id_t clidA, clidB;

  // TODO: introduce wrappers that do checks inside and return API's output.
  // E.g., client_id = connect_and_check(renegotiate_func, "<test message>")
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads,
           eB_concurrency = 0;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency);
  zerm_permit_t eA = make_active_permit(&eA_concurrency);
  zerm_permit_t eB = make_active_permit(&eB_concurrency);

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A") && check_permit(eA, pA)))
    return test_fail(test_name);

  r = zermRegisterThread(phA);
  if (!(check_success(r, "zermRegisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit B") && check_permit(eB, pB)))
    return test_fail(test_name);

  r = zermReleasePermit(phB);
  if (!(check_success(r, "zermReleasePermit B") && check_permit(eA, phA)))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  r = zermReleasePermit(phA);
  if (!check_success(r, "zermReleasePermit A"))
    return test_fail(test_name);

  r = zermDisconnect(clidA);
  if (!check_success(r, "zermDisconnect A"))
    return test_fail(test_name);

  r = zermDisconnect(clidB);
  if (!check_success(r, "zermDisconnect B"))
    return test_fail(test_name);

  std::cout << "test_nested_clients done" << std::endl;
  return test_epilog(test_name);
}

bool test_nested_clients_partial_consumption() {
  const char* test_name = "test_nested_clients_partial_consumption";
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
           eA_concurrency = total_number_of_threads/2,
           eB_concurrency = total_number_of_threads - total_number_of_threads/2;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency);
  zerm_permit_t eA = make_active_permit(&eA_concurrency);
  zerm_permit_t eB = make_active_permit(&eB_concurrency);

  zerm_permit_request_t req = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // TODO: add RegisterThread and UnregisterThread calls.

  req.max_sw_threads = total_number_of_threads;
  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit B all threads") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  r = zermReleasePermit(phB);
  if (!check_success(r, "zermReleasePermit B"))
    return test_fail(test_name);

  r = zermReleasePermit(phA);
  if (!check_success(r, "zermReleasePermit A"))
    return test_fail(test_name);

  r = zermDisconnect(clidA);
  if (!check_success(r, "zermDisconnect A"))
    return test_fail(test_name);

  r = zermDisconnect(clidB);
  if (!check_success(r, "zermDisconnect B"))
    return test_fail(test_name);

  std::cout << "test_nested_clients_partial_consumption done" << std::endl;
  return test_epilog(test_name);
}

bool test_overlapping_clients() {
  const char* test_name = "test_overlapping_clients";
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
           eA_concurrency = total_number_of_threads,
           eB_concurrency = 0;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency);
  zerm_permit_t eA = make_active_permit(&eA_concurrency);
  zerm_permit_t eB = make_active_permit(&eB_concurrency);

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A all threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit B all threads") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  renegotiating_permits = {&phB};

  r = zermReleasePermit(phA);
  eB.concurrencies[0] = total_number_of_threads;
  if (!(check_success(r, "zermReleasePermit A") &&
        check_permit(eB,phB) && renegotiating_permits.size() == 0))
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

  std::cout << "test_overlapping_clients done" << std::endl;
  return test_epilog(test_name);
}

bool test_overlapping_clients_two_callbacks() {
  const char* test_name = "test_overlapping_clients_two_callbacks";
  test_prolog(test_name);

  zerm_client_id_t clidA, clidB, clidC;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect B"))
    return false;

  r = zermConnect(client_renegotiate, &clidC);
  if (!check_success(r, "zermConnect C"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr, phC = nullptr;
  uint32_t pA_concurrency, pB_concurrency, pC_concurrency,
           eA_concurrency = total_number_of_threads/2,
           eB_concurrency = total_number_of_threads/2,
           eC_concurrency = total_number_of_threads - 2*(total_number_of_threads/2);

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                pC = make_void_permit(&pC_concurrency);
  zerm_permit_t eA = make_active_permit(&eA_concurrency),
                eB = make_active_permit(&eB_concurrency),
                eC = make_active_permit(&eC_concurrency);

  zerm_permit_request_t req = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit B half threads") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  req.max_sw_threads = total_number_of_threads;
  r = zermRequestPermit(clidC, req, &phC, &phC, &pC);
  if (!(check_success(r, "zermRequestPermit C all threads") &&
        check_permit(eC, pC)))
    return test_fail(test_name);

  renegotiating_permits = {&phB, &phC};
  eC.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;

  r = zermReleasePermit(phA);
  auto unchanged_permits = list_unchanged_permits({{&phB, &pB}, {&phC, &pC}});
  if (!(check_success(r, "zermReleasePermit A") &&
        check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;

  if (!check_success(zermGetPermitData(phC, &pC),
                     "Reading data from permit " + std::to_string(uintptr_t(phC))))
    return test_fail(test_name);

  r = zermReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}});
  if (!(check_success(r, "zermReleasePermit B") &&
        check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = zermReleasePermit(phC);
  if (!check_success(r, "zermReleasePermit C"))
    return test_fail(test_name);

  r = zermDisconnect(clidA);
  if (!check_success(r, "zermDisconnect A"))
    return test_fail(test_name);

  r = zermDisconnect(clidB);
  if (!check_success(r, "zermDisconnect B"))
    return test_fail(test_name);

  r = zermDisconnect(clidC);
  if (!check_success(r, "zermDisconnect C"))
    return test_fail(test_name);

  std::cout << "test_overlapping_clients_two_callbacks done" << std::endl;
  return test_epilog(test_name);
}

bool test_partial_release() {
  const char* test_name = "test_partial_release";
  test_prolog(test_name);

  zerm_client_id_t clidA, clidB;
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA = make_void_permit(&pA_concurrency);
  zerm_permit_t eA = make_active_permit(&eA_concurrency);
  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // Release some of the resources by re-requesting for less
  eA.concurrencies[0] = req.max_sw_threads = total_number_of_threads/2;
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit (re-request client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect (client B)"))
    return false;

  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;
  zerm_permit_t pB = make_void_permit(&pB_concurrency);
  zerm_permit_t eB = make_active_permit(&eB_concurrency);
  req.max_sw_threads = total_number_of_threads/2;
  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  r = zermReleasePermit(phB);
  if (!(check_success(r, "zermReleasePermit (client B)") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  r = zermReleasePermit(phA);
  if (!check_success(r, "zermReleasePermit (client A)"))
    return test_fail(test_name);

  r = zermDisconnect(clidA);
  if (!check_success(r, "zermDisconnect A"))
    return test_fail(test_name);

  r = zermDisconnect(clidB);
  if (!check_success(r, "zermDisconnect B"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_permit_reactivation() {
  const char* test_name = "test_permit_reactivation";
  test_prolog(test_name);

  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA = make_void_permit(&pA_concurrency);
  zerm_permit_t eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t reqA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = zermDeactivatePermit(phA);
  eA.state = ZERM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "zermDeactivatePermit (client A)")
        && check_permit(eA, phA)))
    return test_fail(test_name);

  // Now resources are given back to the Thread Composability Manager.
  // Request some of them from a different client.
  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect (client B)"))
    return false;

  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads / 2;
  zerm_permit_t pB = make_void_permit(&pB_concurrency);
  zerm_permit_t eB = make_active_permit(&eB_concurrency);
  zerm_permit_request_t reqB = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  // Activate previously deactivated request from client A.
  r = zermActivatePermit(phA);
  eA.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eA.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "zermActivatePermit (client A)") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  eA.concurrencies[0] = total_number_of_threads;

  if (!check_success(zermGetPermitData(phA, &pA),
                     "Reading data from permit " + std::to_string(uintptr_t(phA))))
    return test_fail(test_name);

  r = zermReleasePermit(phB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "zermReleasePermit (client B)") &&
        check_permit(eA, phA) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = zermReleasePermit(phA);
  if (!check_success(r, "zermReleasePermit (client A)"))
    return test_fail(test_name);

  r = zermDisconnect(clidB);
  if (!check_success(r, "zermDisconnect (client B)"))
    return test_fail(test_name);

  r = zermDisconnect(clidA);
  if (!check_success(r, "zermDisconnect (client A)"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

std::atomic<bool> allow_renegotiation{false};
std::atomic<bool> is_callback_invoked{false};
zerm_permit_handle_t phS{nullptr};

bool test_static_permit() {
  const char* test_name = "test_static_permit";
  test_prolog(test_name);

  zerm_client_id_t clid;
  zerm_permit_handle_t phA = nullptr;

  uint32_t pA_concurrency, pS_concurrency;
  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pS = make_void_permit(&pS_concurrency);

  auto renegotiation_function = [](zerm_permit_handle_t p, void* arg,
                                   zerm_callback_flags_t reason)
  {
    zerm_permit_handle_t permit_via_arg = *(zerm_permit_handle_t*)arg;
    bool r = true;
    r &= check(reason.new_concurrency, "Reason invoking callback.");
    r &= check(p == permit_via_arg, "Check correct arg is passed to the callback.");
    r &= check(p != phS || allow_renegotiation, "Check static permit renegotiation.");
    is_callback_invoked = true;
    return r ? ZE_RESULT_SUCCESS : ZE_RESULT_ERROR_UNKNOWN;
  };

  ze_result_t r = zermConnect(renegotiation_function, &clid);
  if (!check_success(r, "zermConnect"))
    return test_fail(test_name);

  zerm_permit_request_t req = make_request(0, total_number_of_threads/2);
  uint32_t eA_concurrency = total_number_of_threads/2;
  zerm_permit_t eA = make_active_permit(&eA_concurrency);

  r = zermRequestPermit(clid, req, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit regular") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // Request that shouldn't be renegotiated in active state
  req.max_sw_threads = total_number_of_threads;
  req.flags.rigid_concurrency = true;
  uint32_t eS_concurrency = total_number_of_threads - total_number_of_threads/2;
  zerm_permit_t eS = make_active_permit(&eS_concurrency, nullptr, 1, req.flags);
  r = zermRequestPermit(clid, req, &phS, &phS, &pS);
  if (!(check_success(r, "zermRequestPermit static") &&
        check_permit(eS, pS)))
    return test_fail(test_name);

  check(!is_callback_invoked, "Renegotiation should not happen for any permit.");

  r = zermReleasePermit(phA);
  if (!(check_success(r, "zermReleasePermit regular") &&
        check_permit(eS, phS)))
    return test_fail(test_name);

  check(
    !is_callback_invoked,
    "Renegotiation happened for permit that cannot renegotiate"
    " while in ACTIVE state."
  );

  // Check that renegotiation takes place when the static permit transferred to
  // the IDLE state, but its callback is not invoked.
  allow_renegotiation = true;
  eS.concurrencies[0] = total_number_of_threads;
  eS.state = ZERM_PERMIT_STATE_IDLE;

  r = zermIdlePermit(phS);
  if (!(check_success(r, "zermIdlePermit static") &&
        check_permit(eS, phS)))
    return test_fail(test_name);

  if (!check(!is_callback_invoked, "Callback was invoked after renegotiating permit "
             "that switched to idle state."))
    return test_fail(test_name);

  r = zermReleasePermit(phS);
  if (!check_success(r, "zermReleasePermit static"))
    return test_fail(test_name);

  r = zermDisconnect(clid);
  if (!check_success(r, "zermDisconnect"))
    return test_fail(test_name);

  std::cout << "test_static_permit done" << std::endl;
  return test_epilog(test_name);
}

bool test_support_for_pending_state() {
  const char* test_name = "test_support_for_pending_state";
  test_prolog(test_name);

  zerm_client_id_t clidA, clidB, clidC, clidD;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect for client A"))
    return test_fail(test_name);

  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect for client B"))
    return test_fail(test_name);

  r = zermConnect(client_renegotiate, &clidC);
  if (!check_success(r, "zermConnect for client C"))
    return test_fail(test_name);

  r = zermConnect(client_renegotiate, &clidD);
  if (!check_success(r, "zermConnect for client C"))
    return test_fail(test_name);

  zerm_permit_handle_t phA{nullptr}, phB{nullptr}, phC{nullptr}, phD{nullptr};
  uint32_t pA_concurrency, pB_concurrency, pC_concurrency, pD_concurrency;
  zerm_permit_t pA = make_void_permit(&pA_concurrency);
  zerm_permit_t pB = make_void_permit(&pB_concurrency);
  zerm_permit_t pC = make_void_permit(&pC_concurrency);
  zerm_permit_t pD = make_void_permit(&pD_concurrency);

  uint32_t eA_concurrency, eB_concurrency, eC_concurrency, eD_concurrency;
  zerm_permit_t eA = make_active_permit(&eA_concurrency);
  zerm_permit_t eB = make_active_permit(&eB_concurrency);
  zerm_permit_t eC = make_active_permit(&eC_concurrency);
  zerm_permit_t eD = make_active_permit(&eD_concurrency);

  zerm_permit_request_t reqA = make_request(0, total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit for client A") && check_permit(eA, pA)))
    return test_fail(test_name);

  zerm_permit_request_t reqB = make_request(total_number_of_threads, total_number_of_threads);
  eB.concurrencies[0] = 0;
  eB.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit for client B") && check_permit(eB, pB)))
    return test_fail(test_name);

  zerm_permit_request_t reqC = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eC.concurrencies[0] = 0;
  eC.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidC, reqC, &phC, &phC, &pC);
  if (!(check_success(r, "zermRequestPermit for client C") && check_permit(eC, pC)))
    return test_fail(test_name);

  zerm_permit_request_t reqD = make_request(0, total_number_of_threads / 2);
  eD.concurrencies[0] = 0;
  r = zermRequestPermit(clidD, reqD, &phD, &phD, &pD);
  if (!(check_success(r, "zermRequestPermit for client D") && check_permit(eD, pD)))
    return test_fail(test_name);

  renegotiating_permits = {&phA, &phC, &phD};
  r = zermReleasePermit(phB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "zermReleasePermit for client B")
        && check(renegotiating_permits.size() == 3, "Check there are no renegotiated permits")
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  reqA = make_request(0, total_number_of_threads / 2);
  renegotiating_permits = {&phC, &phD};
  eA.concurrencies[0] = total_number_of_threads / 2;
  eC.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eC.state = ZERM_PERMIT_STATE_ACTIVE;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA); 
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "zermRequestPermit for client A (re-requesting)")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(zermGetPermitData(phC, &pC),
                     "Reading data from permit " + std::to_string(uintptr_t(phC))))
    return test_fail(test_name);

  renegotiating_permits = {&phC, &phD};
  eD.concurrencies[0] = total_number_of_threads / 2;
  r = zermReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "zermReleasePermit for client A")
        && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;
  r = zermReleasePermit(phD);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}});
  if (!(check_success(r, "zermReleasePermit for client D")
        && check_permit(eC, phC)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  r = zermReleasePermit(phC);
  if (!check_success(r, "zermReleasePermit for client C"))
    return test_fail(test_name);

  r = zermDisconnect(clidA);
  if (!check_success(r, "zermDisconnect for client A"))
    return test_fail(test_name);

  r = zermDisconnect(clidB);
  if (!check_success(r, "zermDisconnect for client B"))
    return test_fail(test_name);

  r = zermDisconnect(clidC);
  if (!check_success(r, "zermDisconnect for client C"))
    return test_fail(test_name);

  r = zermDisconnect(clidD);
  if (!check_success(r, "zermDisconnect for client D"))
    return test_fail(test_name);

  std::cout << "test_support_for_pending_state done" << std::endl;
  return test_epilog(test_name);
}

int main() {
  if (SetEnv("RM_STRATEGY", "FCFS")) {
    std::cout << "WARNING: Cannot set RM_STRATEGY env variable.";
  }

  bool res = true;

  res &= test_alternating_clients();
  res &= test_nested_clients();
  res &= test_nested_clients_partial_consumption();
  res &= test_overlapping_clients();
  res &= test_overlapping_clients_two_callbacks();
  res &= test_partial_release();
  res &= test_permit_reactivation();
  res &= test_static_permit();
  res &= test_support_for_pending_state();

  return int(!res);
}
