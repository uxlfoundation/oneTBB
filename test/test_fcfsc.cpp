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
  check(true, "\n\nbegin test_nested_clients");
  zerm_client_id_t clidA, clidB;

  // TODO: introduce wrappers that do checks inside and return API's output.
  // E.g., client_id = connect_and_check(renegotiate_func, "<test message>")
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads,
           eB_concurrency = 0;

  zerm_permit_t pA{&pA_concurrency, nullptr}, pB{&pB_concurrency, nullptr};
  zerm_permit_t eA = {&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  zerm_permit_t eB = {&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A") && check_permit(eA, pA)))
    return check(false, "end test_nested_clients");

  r = zermRegisterThread(phA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread A") && check_permit(eA, phA)))
    return check(false, "end test_nested_clients");

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B") && check_permit(eB, pB)))
    return check(false, "end test_nested_clients");

  r = zermReleasePermit(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B") && check_permit(eA, phA)))
    return check(false, "end test_nested_clients");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread A") && check_permit(eA, phA)))
    return check(false, "end test_nested_clients");

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit A"))
    return check(false, "end test_nested_clients");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect A"))
    return check(false, "end test_nested_clients");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect B"))
    return check(false, "end test_nested_clients");

  std::cout << "test_nested_clients done" << std::endl;
  return check(true, "end test_nested_clients");
}

bool test_nested_clients_partial_consumption() {
  check(true, "\n\nbegin test_nested_clients_partial_consumption");
  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads/2,
           eB_concurrency = total_number_of_threads - total_number_of_threads/2;

  zerm_permit_t pA{&pA_concurrency, nullptr}, pB{&pB_concurrency, nullptr};
  zerm_permit_t eA = {&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  zerm_permit_t eB = {&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t req = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return check(false, "end test_nested_clients_partial_consumption");

  // TODO: add RegisterThread and UnregisterThread calls.

  req.max_sw_threads = total_number_of_threads;
  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B all threads") &&
        check_permit(eB, pB)))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermReleasePermit(phB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B"))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit A"))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect A"))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect B"))
    return check(false, "end test_nested_clients_partial_consumption");

  std::cout << "test_nested_clients_partial_consumption done" << std::endl;
  return check(true, "end test_nested_clients_partial_consumption");
}

bool test_overlapping_clients() {
  check(true, "\n\nbegin test_overlapping_clients");
  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads,
           eB_concurrency = 0;

  zerm_permit_t pA {&pA_concurrency, nullptr}, pB{&pB_concurrency, nullptr};
  zerm_permit_t eA = {&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  zerm_permit_t eB = {&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A all threads") &&
        check_permit(eA, pA)))
    return check(false, "end test_overlapping_clients");

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B all threads") &&
        check_permit(eB, pB)))
    return check(false, "end test_overlapping_clients");

  renegotiating_permits = {&phB};

  r = zermReleasePermit(phA);
  eB.concurrencies[0] = total_number_of_threads;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit A") &&
        check_permit(eB,phB) && renegotiating_permits.size() == 0))
    return check(false, "end test_overlapping_clients");

  r = zermReleasePermit(phB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B"))
    return check(false, "end test_overlapping_clients");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect A"))
    return check(false, "end test_overlapping_clients");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect B"))
    return check(false, "end test_overlapping_clients");

  std::cout << "test_overlapping_clients done" << std::endl;
  return check(true, "end test_overlapping_clients");
}

bool test_overlapping_clients_two_callbacks() {
  check(true, "\n\nbegin test_overlapping_clients_two_callbacks");
  zerm_client_id_t clidA, clidB, clidC;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect B"))
    return false;

  r = zermConnect(client_renegotiate, &clidC);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect C"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr, phC = nullptr;
  uint32_t pA_concurrency, pB_concurrency, pC_concurrency,
           eA_concurrency = total_number_of_threads/2,
           eB_concurrency = total_number_of_threads/2,
           eC_concurrency = total_number_of_threads - 2*(total_number_of_threads/2);

  zerm_permit_t pA{&pA_concurrency, nullptr},
              pB{&pB_concurrency, nullptr},
              pC{&pC_concurrency, nullptr};
  zerm_permit_t eA = {&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  zerm_permit_t eB = {&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  zerm_permit_t eC = {&eC_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t req = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return check(false, "end test_overlapping_clients_two_callbacks");

  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B half threads") &&
        check_permit(eB, pB)))
    return check(false, "end test_overlapping_clients_two_callbacks");

  req.max_sw_threads = total_number_of_threads;
  r = zermRequestPermit(clidC, req, &phC, &phC, &pC);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit C all threads") &&
        check_permit(eC, pC)))
    return check(false, "end test_overlapping_clients_two_callbacks");

  renegotiating_permits = {&phB, &phC};
  eC.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;

  r = zermReleasePermit(phA);
  auto unchanged_permits = list_unchanged_permits({{&phB, &pB}, {&phC, &pC}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit A") &&
        check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return check(false, "end test_overlapping_clients_two_callbacks");

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;

  if (!check(zermGetPermitData(phC, &pC) == ZE_RESULT_SUCCESS,
             "Reading data from permit " + std::to_string(uintptr_t(phC))))
    return check(false, "end test_overlapping_clients_two_callbacks");

  r = zermReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B") &&
        check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return check(false, "end test_overlapping_clients_two_callbacks");

  r = zermReleasePermit(phC);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit C"))
    return check(false, "end test_overlapping_clients_two_callbacks");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect A"))
    return check(false, "end test_overlapping_clients_two_callbacks");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect B"))
    return check(false, "end test_overlapping_clients_two_callbacks");

  r = zermDisconnect(clidC);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect C"))
    return check(false, "end test_overlapping_clients_two_callbacks");

  std::cout << "test_overlapping_clients_two_callbacks done" << std::endl;
  return check(true, "end test_overlapping_clients_two_callbacks");
}

bool test_partial_release() {
  check(true, "\n\nbegin test_partial_release");

  zerm_client_id_t clidA, clidB;
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA{&pA_concurrency, nullptr};
  zerm_permit_t eA = {&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return check(false, "end test_partial_release");

  // Release some of the resources by re-requesting for less
  eA.concurrencies[0] = req.max_sw_threads = total_number_of_threads/2;
  r = zermRequestPermit(clidA, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (re-request client A)") &&
        check_permit(eA, pA)))
    return check(false, "end test_partial_release");

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client B)"))
    return false;

  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;
  zerm_permit_t pB{&pB_concurrency, nullptr};
  zerm_permit_t eB = {&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  req.max_sw_threads = total_number_of_threads/2;
  r = zermRequestPermit(clidB, req, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return check(false, "end test_partial_release");

  r = zermReleasePermit(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit (client B)") &&
        check_permit(eA, phA)))
    return check(false, "end test_partial_release");

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit (client A)"))
    return check(false, "end test_partial_release");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect A"))
    return check(false, "end test_partial_release");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect B"))
    return check(false, "end test_partial_release");

  return check(true, "end test_partial_release");
}

bool test_permit_reactivation() {
  check(true, "\n\nbegin test_permit_reactivation");
  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA{&pA_concurrency, nullptr};
  zerm_permit_t eA = {&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t reqA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return check(false, "end test_permit_reactivation");

  r = zermDeactivatePermit(phA);
  eA.state = ZERM_PERMIT_STATE_INACTIVE;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermDeactivatePermit (client A)")
        && check_permit(eA, phA)))
    return check(false, "end test_permit_reactivation");

  // Now resources are given back to the Thread Composability Manager.
  // Request some of them from a different client.
  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client B)"))
    return false;

  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads / 2;
  zerm_permit_t pB{&pB_concurrency, nullptr};
  zerm_permit_t eB = {&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};
  zerm_permit_request_t reqB = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return check(false, "end test_permit_reactivation");

  // Activate previously deactivated request from client A.
  r = zermActivatePermit(phA);
  eA.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eA.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermActivatePermit (client A)") &&
        check_permit(eA, phA)))
    return check(false, "end test_permit_reactivation");

  renegotiating_permits = {&phA};
  eA.concurrencies[0] = total_number_of_threads;

  if (!check(zermGetPermitData(phA, &pA) == ZE_RESULT_SUCCESS,
             "Reading data from permit " + std::to_string(uintptr_t(phA))))
    return check(false, "end test_permit_reactivation");

  r = zermReleasePermit(phB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit (client B)") &&
        check_permit(eA, phA) && renegotiating_permits == unchanged_permits))
    return check(false, "end test_permit_reactivation");

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit (client A)"))
    return check(false, "end test_permit_reactivation");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect (client B)"))
    return check(false, "end test_permit_reactivation");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect (client A)"))
    return check(false, "end test_permit_reactivation");

  return check(true, "end test_permit_reactivation");
}

std::atomic<bool> allow_renegotiation{false};
std::atomic<bool> is_callback_invoked{false};
zerm_permit_handle_t phS{nullptr};

bool test_static_permit() {
  check(true, "\n\nbegin test_static_permit");
  zerm_client_id_t clid;
  zerm_permit_handle_t phA = nullptr;

  uint32_t pA_concurrency, pS_concurrency;
  zerm_permit_t pA{&pA_concurrency, nullptr}, pS{&pS_concurrency, nullptr};

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
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect"))
    return check(false, "end test_static_permit");

  zerm_permit_request_t req = make_request(0, total_number_of_threads/2);
  uint32_t eA_concurrency = total_number_of_threads/2;
  zerm_permit_t eA = {&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  r = zermRequestPermit(clid, req, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit regular") &&
        check_permit(eA, pA)))
    return check(false, "end test_static_permit");

  // Request that shouldn't be renegotiated in active state
  req.max_sw_threads = total_number_of_threads;
  req.flags.rigid_concurrency = true;
  uint32_t eS_concurrency = total_number_of_threads - total_number_of_threads/2;
  zerm_permit_t eS = {&eS_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, req.flags};
  r = zermRequestPermit(clid, req, &phS, &phS, &pS);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit static") &&
        check_permit(eS, pS)))
    return check(false, "end test_static_permit");

  check(!is_callback_invoked, "Renegotiation should not happen for any permit.");

  r = zermReleasePermit(phA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit regular") &&
        check_permit(eS, phS)))
    return check(false, "end test_static_permit");

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
  if (!(check(r == ZE_RESULT_SUCCESS, "zermIdlePermit static") &&
        check_permit(eS, phS)))
    return check(false, "end test_static_permit");

  if (!check(!is_callback_invoked, "Callback was invoked after renegotiating permit "
             "that switched to idle state."))
    return check(false, "end test_static_permit");

  r = zermReleasePermit(phS);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit static"))
    return check(false, "end test_static_permit");

  r = zermDisconnect(clid);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect"))
    return check(false, "end test_static_permit");

  std::cout << "test_static_permit done" << std::endl;
  return check(true, "end test_static_permit");
}

bool test_support_for_pending_state() {
  check(true, "\n\nbegin test_support_for_pending_state");
  zerm_client_id_t clidA, clidB, clidC, clidD;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect for client A"))
    return check(false, "end test_support_for_pending_state");

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect for client B"))
    return check(false, "end test_support_for_pending_state");

  r = zermConnect(client_renegotiate, &clidC);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect for client C"))
    return check(false, "end test_support_for_pending_state");

  r = zermConnect(client_renegotiate, &clidD);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect for client C"))
    return check(false, "end test_support_for_pending_state");

  zerm_permit_handle_t phA{nullptr}, phB{nullptr}, phC{nullptr}, phD{nullptr};
  uint32_t pA_concurrency, pB_concurrency, pC_concurrency, pD_concurrency;
  zerm_permit_t pA{&pA_concurrency, nullptr};
  zerm_permit_t pB{&pB_concurrency, nullptr};
  zerm_permit_t pC{&pC_concurrency, nullptr};
  zerm_permit_t pD{&pD_concurrency, nullptr};

  uint32_t eA_concurrency, eB_concurrency, eC_concurrency, eD_concurrency;
  zerm_permit_t eA = { &eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0 };
  zerm_permit_t eB = { &eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0 };
  zerm_permit_t eC = { &eC_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0 };
  zerm_permit_t eD = { &eD_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0 };

  zerm_permit_request_t reqA = make_request(0, total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client A") && check_permit(eA, pA)))
    return check(false, "end test_support_for_pending_state");

  zerm_permit_request_t reqB = make_request(total_number_of_threads, total_number_of_threads);
  eB.concurrencies[0] = 0;
  eB.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client B") && check_permit(eB, pB)))
    return check(false, "end test_support_for_pending_state");

  zerm_permit_request_t reqC = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eC.concurrencies[0] = 0;
  eC.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidC, reqC, &phC, &phC, &pC);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client C") && check_permit(eC, pC)))
    return check(false, "end test_support_for_pending_state");

  zerm_permit_request_t reqD = make_request(0, total_number_of_threads / 2);
  eD.concurrencies[0] = 0;
  r = zermRequestPermit(clidD, reqD, &phD, &phD, &pD);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client D") && check_permit(eD, pD)))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phA, &phC, &phD};
  r = zermReleasePermit(phB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phC, &pC}, {&phD, &pD}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for client B")
        && check(renegotiating_permits.size() == 3, "Check there are no renegotiated permits")
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return check(false, "end test_pending_permit_processing");

  reqA = make_request(0, total_number_of_threads / 2);
  renegotiating_permits = {&phC, &phD};
  eA.concurrencies[0] = total_number_of_threads / 2;
  eC.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eC.state = ZERM_PERMIT_STATE_ACTIVE;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA); 
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client A (re-requesting)")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return check(false, "end test_support_for_pending_state");

  if (!check(zermGetPermitData(phC, &pC) == ZE_RESULT_SUCCESS,
             "Reading data from permit " + std::to_string(uintptr_t(phC))))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phC, &phD};
  eD.concurrencies[0] = total_number_of_threads / 2;
  r = zermReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for client A")
        && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;
  r = zermReleasePermit(phD);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for client D")
        && check_permit(eC, phC)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return check(false, "end test_support_for_pending_state");

  r = zermReleasePermit(phC);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for client C"))
    return check(false, "end test_support_for_pending_state");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect for client A"))
    return check(false, "end test_support_for_pending_state");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect for client B"))
    return check(false, "end test_support_for_pending_state");

  r = zermDisconnect(clidC);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect for client C"))
    return check(false, "end test_support_for_pending_state");

  r = zermDisconnect(clidD);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect for client D"))
    return check(false, "end test_support_for_pending_state");

  std::cout << "test_support_for_pending_state done" << std::endl;
  return check(true, "end test_support_for_pending_state");
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
