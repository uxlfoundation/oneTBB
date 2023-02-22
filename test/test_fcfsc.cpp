/*
    Copyright (c) 2021-2023 Intel Corporation
*/

#include "test_utils.h"

#include "tcm.h"

#include "common_tests.h"

#include <iostream>

bool test_nested_clients() {
  const char* test_name = "test_nested_clients";
  test_prolog(test_name);

  tcm_client_id_t clidA, clidB;

  // TODO: introduce wrappers that do checks inside and return API's output.
  // E.g., client_id = connect_and_check(renegotiate_func, "<test message>")
  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return false;

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return false;

  tcm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads,
           eB_concurrency = 0;


  tcm_permit_t pA = make_void_permit(&pA_concurrency), pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency);
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t req = make_request(0, total_number_of_threads);
  r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A") && check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmRegisterThread(phA);
  if (!(check_success(r, "tcmRegisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  r = tcmRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit B") && check_permit(eB, pB)))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  if (!(check_success(r, "tcmReleasePermit B") && check_permit(eA, phA)))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidA);
  if (!check_success(r, "tcmDisconnect A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidB);
  if (!check_success(r, "tcmDisconnect B"))
    return test_fail(test_name);

  std::cout << "test_nested_clients done" << std::endl;
  return test_epilog(test_name);
}

bool test_nested_clients_partial_consumption() {
  const char* test_name = "test_nested_clients_partial_consumption";
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
           eA_concurrency = total_number_of_threads/2,
           eB_concurrency = total_number_of_threads - total_number_of_threads/2;

  tcm_permit_t pA = make_void_permit(&pA_concurrency), pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency);
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t req = make_request(0, total_number_of_threads/2);
  r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // TODO: add RegisterThread and UnregisterThread calls.

  req.max_sw_threads = total_number_of_threads;
  r = tcmRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit B all threads") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  if (!check_success(r, "tcmReleasePermit B"))
    return test_fail(test_name);

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidA);
  if (!check_success(r, "tcmDisconnect A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidB);
  if (!check_success(r, "tcmDisconnect B"))
    return test_fail(test_name);

  std::cout << "test_nested_clients_partial_consumption done" << std::endl;
  return test_epilog(test_name);
}

bool test_overlapping_clients() {
  const char* test_name = "test_overlapping_clients";
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
           eA_concurrency = total_number_of_threads,
           eB_concurrency = 0;

  tcm_permit_t pA = make_void_permit(&pA_concurrency), pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency);
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t req = make_request(0, total_number_of_threads);
  r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A all threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit B all threads") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  renegotiating_permits = {&phB};

  r = tcmReleasePermit(phA);
  eB_concurrency = total_number_of_threads;
  if (!(check_success(r, "tcmReleasePermit A") &&
        check_permit(eB, phB) && renegotiating_permits.size() == 0))
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

  std::cout << "test_overlapping_clients done" << std::endl;
  return test_epilog(test_name);
}

bool test_overlapping_clients_two_callbacks() {
  const char* test_name = "test_overlapping_clients_two_callbacks";
  test_prolog(test_name);

  tcm_client_id_t clidA, clidB, clidC;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return false;

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return false;

  r = tcmConnect(client_renegotiate, &clidC);
  if (!check_success(r, "tcmConnect C"))
    return false;

  tcm_permit_handle_t phA = nullptr, phB = nullptr, phC = nullptr;
  uint32_t pA_concurrency, pB_concurrency, pC_concurrency,
           eA_concurrency = total_number_of_threads/2,
           eB_concurrency = total_number_of_threads/2,
           eC_concurrency = total_number_of_threads - 2*(total_number_of_threads/2);

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                pC = make_void_permit(&pC_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency),
                eB = make_active_permit(&eB_concurrency),
                eC = make_active_permit(&eC_concurrency);

  tcm_permit_request_t req = make_request(0, total_number_of_threads/2);
  r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit B half threads") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  req.max_sw_threads = total_number_of_threads;
  r = tcmRequestPermit(clidC, req, &phC, &phC, &pC);
  if (!(check_success(r, "tcmRequestPermit C all threads") &&
        check_permit(eC, pC)))
    return test_fail(test_name);

  renegotiating_permits = {&phB, &phC};
  eC.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;

  r = tcmReleasePermit(phA);
  auto unchanged_permits = list_unchanged_permits({{&phB, &pB}, {&phC, &pC}});
  if (!(check_success(r, "tcmReleasePermit A") &&
        check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;

  if (!check_success(tcmGetPermitData(phC, &pC),
                     "Reading data from permit " + std::to_string(uintptr_t(phC))))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}});
  if (!(check_success(r, "tcmReleasePermit B") &&
        check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = tcmReleasePermit(phC);
  if (!check_success(r, "tcmReleasePermit C"))
    return test_fail(test_name);

  r = tcmDisconnect(clidA);
  if (!check_success(r, "tcmDisconnect A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidB);
  if (!check_success(r, "tcmDisconnect B"))
    return test_fail(test_name);

  r = tcmDisconnect(clidC);
  if (!check_success(r, "tcmDisconnect C"))
    return test_fail(test_name);

  std::cout << "test_overlapping_clients_two_callbacks done" << std::endl;
  return test_epilog(test_name);
}

bool test_partial_release() {
  const char* test_name = "test_partial_release";
  test_prolog(test_name);

  tcm_client_id_t clidA, clidB;
  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect (client A)"))
    return false;

  tcm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  tcm_permit_t pA = make_void_permit(&pA_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency);
  tcm_permit_request_t req = make_request(0, total_number_of_threads);
  r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // Release some of the resources by re-requesting for less
  eA.concurrencies[0] = req.max_sw_threads = total_number_of_threads/2;
  r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (re-request client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect (client B)"))
    return false;

  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;
  tcm_permit_t pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eB = make_active_permit(&eB_concurrency);
  req.max_sw_threads = total_number_of_threads/2;
  r = tcmRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  if (!(check_success(r, "tcmReleasePermit (client B)") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit (client A)"))
    return test_fail(test_name);

  r = tcmDisconnect(clidA);
  if (!check_success(r, "tcmDisconnect A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidB);
  if (!check_success(r, "tcmDisconnect B"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_permit_reactivation() {
  const char* test_name = "test_permit_reactivation";
  test_prolog(test_name);

  tcm_client_id_t clidA, clidB;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect (client A)"))
    return false;

  tcm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  tcm_permit_t pA = make_void_permit(&pA_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t reqA = make_request(0, total_number_of_threads);
  r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmDeactivatePermit(phA);
  eA_concurrency = 0;
  eA.state = TCM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "tcmDeactivatePermit (client A)")
        && check_permit(eA, phA)))
    return test_fail(test_name);

  // Now resources are given back to the Thread Composability Manager.
  // Request some of them from a different client.
  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect (client B)"))
    return false;

  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads / 2;
  tcm_permit_t pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eB = make_active_permit(&eB_concurrency);
  tcm_permit_request_t reqB = make_request(0, total_number_of_threads/2);
  r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  // Activate previously deactivated request from client A.
  r = tcmActivatePermit(phA);
  eA_concurrency = total_number_of_threads - total_number_of_threads / 2;
  eA.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit (client A)") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  eA_concurrency = total_number_of_threads;

  if (!check_success(tcmGetPermitData(phA, &pA),
                     "Reading data from permit " + std::to_string(uintptr_t(phA))))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "tcmReleasePermit (client B)") &&
        check_permit(eA, phA) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit (client A)"))
    return test_fail(test_name);

  r = tcmDisconnect(clidB);
  if (!check_success(r, "tcmDisconnect (client B)"))
    return test_fail(test_name);

  r = tcmDisconnect(clidA);
  if (!check_success(r, "tcmDisconnect (client A)"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

std::atomic<bool> allow_renegotiation{false};
std::atomic<bool> is_callback_invoked{false};
tcm_permit_handle_t phS{nullptr};

bool test_static_permit() {
  const char* test_name = "test_static_permit";
  test_prolog(test_name);

  tcm_client_id_t clid;
  tcm_permit_handle_t phA = nullptr;

  uint32_t pA_concurrency, pS_concurrency;
  tcm_permit_t pA = make_void_permit(&pA_concurrency), pS = make_void_permit(&pS_concurrency);

  auto renegotiation_function = [](tcm_permit_handle_t p, void* arg,
                                   tcm_callback_flags_t reason)
  {
    tcm_permit_handle_t permit_via_arg = *(tcm_permit_handle_t*)arg;
    bool r = true;
    r &= check(reason.new_concurrency, "Reason invoking callback.");
    r &= check(p == permit_via_arg, "Check correct arg is passed to the callback.");
    r &= check(p != phS || allow_renegotiation, "Check static permit renegotiation.");
    is_callback_invoked = true;
    return r ? TCM_RESULT_SUCCESS : TCM_RESULT_ERROR_UNKNOWN;
  };

  tcm_result_t r = tcmConnect(renegotiation_function, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_request_t req = make_request(0, total_number_of_threads/2);
  uint32_t eA_concurrency = total_number_of_threads/2;
  tcm_permit_t eA = make_active_permit(&eA_concurrency);

  r = tcmRequestPermit(clid, req, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit regular") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // Request permit that shouldn't be renegotiated in active state
  req.max_sw_threads = total_number_of_threads;
  req.flags.rigid_concurrency = true;
  uint32_t eS_concurrency = total_number_of_threads - total_number_of_threads/2;
  tcm_permit_t eS = make_active_permit(&eS_concurrency, /*cpu_masks*/nullptr, /*size*/1, req.flags);

  r = tcmRequestPermit(clid, req, &phS, &phS, &pS);
  if (!(check_success(r, "tcmRequestPermit static") &&
        check_permit(eS, pS)))
    return test_fail(test_name);

  if (!check(!is_callback_invoked, "Renegotiation should not happen for any permit."))
      return check(false, "end test_static_permit");

  r = tcmReleasePermit(phA);
  if (!(check_success(r, "tcmReleasePermit regular") &&
        check_permit(eS, phS)))
    return test_fail(test_name);

  if (!check(!is_callback_invoked, "Renegotiation did not happen for rigid concurrency permit."))
      return check(false, "end test_static_permit");

  // Check that renegotiation does not take place when the static permit transferred to the IDLE
  // state.
  eS.state = TCM_PERMIT_STATE_IDLE;

  r = tcmIdlePermit(phS);
  if (!(check_success(r, "tcmIdlePermit static") &&
        check_permit(eS, phS)))
    return test_fail(test_name);

  if (!check(!is_callback_invoked, "Renegotiation did not happen for rigid concurrency permit"
             " that switched to idle state."))
    return test_fail(test_name);

  r = tcmDeactivatePermit(phS);
  eS_concurrency = 0;
  eS.state = TCM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "tcmDeactivatePermit static") && check_permit(eS, phS)))
    return test_fail(test_name);

  r = tcmActivatePermit(phS);  // Activation should be able to satisfy desired concurrency now
  eS_concurrency = total_number_of_threads;
  eS.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit static") && check_permit(eS, phS)))
    return test_fail(test_name);

  r = tcmReleasePermit(phS);
  if (!check_success(r, "tcmReleasePermit static"))
    return test_fail(test_name);

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  std::cout << "test_static_permit done" << std::endl;
  return test_epilog(test_name);
}

bool test_support_for_pending_state() {
  const char* test_name = "test_support_for_pending_state";
  test_prolog(test_name);

  tcm_client_id_t clidA, clidB, clidC, clidD;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect for client A"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect for client B"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidC);
  if (!check_success(r, "tcmConnect for client C"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidD);
  if (!check_success(r, "tcmConnect for client D"))
    return test_fail(test_name);

  tcm_permit_handle_t phA{nullptr}, phB{nullptr}, phC{nullptr}, phD{nullptr};
  uint32_t pA_concurrency, pB_concurrency, pC_concurrency, pD_concurrency;
  tcm_permit_t pA = make_void_permit(&pA_concurrency);
  tcm_permit_t pB = make_void_permit(&pB_concurrency);
  tcm_permit_t pC = make_void_permit(&pC_concurrency);
  tcm_permit_t pD = make_void_permit(&pD_concurrency);

  uint32_t eA_concurrency, eB_concurrency, eC_concurrency, eD_concurrency;
  tcm_permit_t eA = make_active_permit(&eA_concurrency);
  tcm_permit_t eB = make_active_permit(&eB_concurrency);
  tcm_permit_t eC = make_active_permit(&eC_concurrency);
  tcm_permit_t eD = make_active_permit(&eD_concurrency);

  tcm_permit_request_t reqA = make_request(0, total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads;
  r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit for client A") && check_permit(eA, pA)))
    return test_fail(test_name);

  tcm_permit_request_t reqB = make_request(total_number_of_threads, total_number_of_threads);
  eB.concurrencies[0] = 0;
  eB.state = TCM_PERMIT_STATE_PENDING;
  r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit for client B") && check_permit(eB, pB)))
    return test_fail(test_name);

  tcm_permit_request_t reqC = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eC.concurrencies[0] = 0;
  eC.state = TCM_PERMIT_STATE_PENDING;
  r = tcmRequestPermit(clidC, reqC, &phC, &phC, &pC);
  if (!(check_success(r, "tcmRequestPermit for client C") && check_permit(eC, pC)))
    return test_fail(test_name);

  tcm_permit_request_t reqD = make_request(0, total_number_of_threads / 2);
  eD.concurrencies[0] = 0;
  r = tcmRequestPermit(clidD, reqD, &phD, &phD, &pD);
  if (!(check_success(r, "tcmRequestPermit for client D") && check_permit(eD, pD)))
    return test_fail(test_name);

  renegotiating_permits = {&phA, &phC, &phD};
  r = tcmReleasePermit(phB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "tcmReleasePermit for client B")
        && check(renegotiating_permits.size() == 3, "Check there are no renegotiated permits")
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  reqA = make_request(0, total_number_of_threads / 2);
  renegotiating_permits = {&phC, &phD};
  eA.concurrencies[0] = total_number_of_threads / 2;
  eC.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eC.state = TCM_PERMIT_STATE_ACTIVE;
  r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "tcmRequestPermit for client A (re-requesting)")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phC, &pC),
                     "Reading data from permit " + std::to_string(uintptr_t(phC))))
    return test_fail(test_name);

  renegotiating_permits = {&phC, &phD};
  eD.concurrencies[0] = total_number_of_threads / 2;
  r = tcmReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "tcmReleasePermit for client A")
        && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;
  r = tcmReleasePermit(phD);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}});
  if (!(check_success(r, "tcmReleasePermit for client D")
        && check_permit(eC, phC)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  r = tcmReleasePermit(phC);
  if (!check_success(r, "tcmReleasePermit for client C"))
    return test_fail(test_name);

  r = tcmDisconnect(clidA);
  if (!check_success(r, "tcmDisconnect for client A"))
    return test_fail(test_name);

  r = tcmDisconnect(clidB);
  if (!check_success(r, "tcmDisconnect for client B"))
    return test_fail(test_name);

  r = tcmDisconnect(clidC);
  if (!check_success(r, "tcmDisconnect for client C"))
    return test_fail(test_name);

  r = tcmDisconnect(clidD);
  if (!check_success(r, "tcmDisconnect for client D"))
    return test_fail(test_name);

  std::cout << "test_support_for_pending_state done" << std::endl;
  return test_epilog(test_name);
}

int main() {
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
