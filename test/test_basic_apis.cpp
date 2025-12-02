/*
    Copyright (C) 2023-2025 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "test_utils.h"
#include "common_tests.h"

#include "tcm.h"

#include <cstdint>

TEST("Each of two sequentially composed clients gets all platform resources") {
  tcm_client_id_t clidA = connect_new_client(client_renegotiate);
  tcm_client_id_t clidB = connect_new_client(client_renegotiate);

  tcm_permit_handle_t phA = nullptr, phB = nullptr;
  uint32_t pA_concurrency, pB_concurrency,
           e_concurrency = platform_tcm_concurrency();

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               pB = make_void_permit(&pB_concurrency);

  tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_permit_request_t req = make_request(0, platform_tcm_concurrency());
  tcm_result_t r = tcmRequestPermit(clidA, req, &phA, &phA, &pA);
  check_success(r, "tcmRequestPermit A");
  check_permit(e, pA);

  r = tcmRegisterThread(phA);
  check_success(r, "tcmRegisterThread A");
  check_permit(e, phA);

  r = tcmUnregisterThread();
  check_success(r, "tcmUnregisterThread A");
  check_permit(e, phA);

  r = tcmReleasePermit(phA);
  check_success(r, "tcmReleasePermit A");

  r = tcmRequestPermit(clidB, req, &phB, &phB, &pB);
  check_success(r, "tcmRequestPermit B");
  check_permit(e, pB);

  r = tcmRegisterThread(phB);
  check_success(r, "tcmRegisterThread B");
  check_permit(e, phB);

  r = tcmUnregisterThread();
  check_success(r, "tcmUnregisterThread B");
  check_permit(e, phB);

  r = tcmReleasePermit(phB);
  check_success(r, "tcmReleasePermit B");

  disconnect_client(clidA);
  disconnect_client(clidB);
}

TEST("Manipulating permit state") {
  tcm_client_id_t clid = connect_new_client();

  uint32_t e_concurrency = platform_tcm_concurrency();
  tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_permit_request_t req = make_request(0, platform_tcm_concurrency());
  tcm_permit_handle_t ph = request_permit(clid, req, /*callback_arg*/nullptr);
  check_permit(e, ph);

  idle_permit(ph);
  e.state = TCM_PERMIT_STATE_IDLE;
  check_permit(e, ph);

  activate_permit(ph);
  e.state = TCM_PERMIT_STATE_ACTIVE;
  check_permit(e, ph);

  deactivate_permit(ph);  // lazy deactivate
  e.state = TCM_PERMIT_STATE_INACTIVE;
  check_permit(e, ph);

  activate_permit(ph); // lazy activate
  e.state = TCM_PERMIT_STATE_ACTIVE;
  check_permit(e, ph);

  idle_permit(ph);
  e.state = TCM_PERMIT_STATE_IDLE;
  check_permit(e, ph);

  deactivate_permit(ph);  // lazy deactivate
  e.state = TCM_PERMIT_STATE_INACTIVE;
  check_permit(e, ph);

  activate_permit(ph);
  e_concurrency = platform_tcm_concurrency();
  e.state = TCM_PERMIT_STATE_ACTIVE;
  check_permit(e, ph);

  release_permit(ph);

  disconnect_client(clid);
}

TEST("test_pending_state") {
  tcm_client_id_t clid = connect_new_client();

  tcm_permit_handle_t phA{nullptr}, phB{nullptr};
  uint32_t pA_concurrency{0}, pB_concurrency{0};
  uint32_t eA_concurrency{0}, eB_concurrency{0};
  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency),
               eB = make_pending_permit(&eB_concurrency);

  auto reqA = make_request(0, platform_tcm_concurrency());
  reqA.flags.rigid_concurrency = true;
  eA_concurrency = platform_tcm_concurrency(); eA.flags.rigid_concurrency = true;
  tcm_result_t r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  check_success(r, "tcmRequestPermit for A");
  check_permit(eA, pA);

  tcm_permit_request_t reqB = make_request(platform_tcm_concurrency(), platform_tcm_concurrency());
  r = tcmRequestPermit(clid, reqB, nullptr, &phB, &pB);
  check_success(r, "tcmRequestPermit for B");
  check_permit(eB, pB);

  r = tcmReleasePermit(phB);
  check_success(r, "tcmReleasePermit for B");

  r = tcmReleasePermit(phA);
  check_success(r, "tcmReleasePermit for A");

  disconnect_client(clid);
}

TEST("test_activate_pending_when_one_deactivates") {
  tcm_client_id_t clid = connect_new_client();

  tcm_permit_handle_t phA{nullptr}, phB{nullptr};
  uint32_t pA_concurrency{0}, pB_concurrency{0};
  uint32_t eA_concurrency{0}, eB_concurrency{0};
  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency),
               eB = make_pending_permit(&eB_concurrency);

  tcm_permit_request_t reqA = make_request(1, platform_tcm_concurrency());
  reqA.flags.rigid_concurrency = true;
  eA_concurrency = platform_tcm_concurrency();
  eA.flags.rigid_concurrency = true;
  tcm_result_t r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  check_success(r, "tcmRequestPermit for A");
  check_permit(eA, pA);

  tcm_permit_request_t reqB = make_request(1, platform_tcm_concurrency());
  reqB.flags.rigid_concurrency = true;
  eB.flags.rigid_concurrency = true;
  r = tcmRequestPermit(clid, reqB, nullptr, &phB, &pB);
  check_success(r, "tcmRequestPermit for B");
  check_permit(eB, pB);

  r = tcmDeactivatePermit(phA);
  eA_concurrency = 0;
  eA.state = TCM_PERMIT_STATE_INACTIVE;
  check_success(r, "tcmDeactivatePermit for A");
  check_permit(eA, phA);

  eB_concurrency = platform_tcm_concurrency();
  eB.state = TCM_PERMIT_STATE_ACTIVE;
  check_permit(eB, phB);

  r = tcmReleasePermit(phB);
  check_success(r, "tcmReleasePermit for B");

  r = tcmReleasePermit(phA);
  check_success(r, "tcmReleasePermit for A");

  disconnect_client(clid);

}

std::atomic<bool> allow_rigid_concurrency_permit_negotiation{false};
std::atomic<bool> is_callback_invoked{false}; // TODO: use counter instead of a boolean flag
tcm_permit_handle_t phS{nullptr};
auto renegotiation_function = [](tcm_permit_handle_t p, void* arg,
                                  tcm_callback_flags_t reason)
{
  tcm_permit_handle_t permit_via_arg = *(tcm_permit_handle_t*)arg;
  bool r = true;
  r &= check(reason.new_concurrency, "Reason invoking callback.");
  r &= check(p == permit_via_arg, "Check correct arg is passed to the callback.");
  r &= check(p != phS || allow_rigid_concurrency_permit_negotiation,
             "Check for renegotiation possibility of rigid concurrency permits.");
  is_callback_invoked = true;
  return r ? TCM_RESULT_SUCCESS : TCM_RESULT_ERROR_UNKNOWN;
};

TEST("test_no_negotiation_for_active_rigid_concurrency") {
  // The test checks that negotiation for rigid concurrency permits does not happen while they are
  // in ACTIVE state, but deactivation of such permits happens when they are switched to IDLE state
  // and there is a competing resources demand.

  // TODO: based on the test description above consider splitting on two tests

  tcm_client_id_t clid = connect_new_client(renegotiation_function);
  is_callback_invoked = false;

  tcm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(platform_tcm_concurrency() / 2);

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(platform_tcm_concurrency()/4, (int32_t)eA_concurrency);

  tcm_result_t r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  check_success(r, "tcmRequestPermit regular");
  check_permit(eA, pA);

  // Request that shouldn't be renegotiated in active state
  tcm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  tcm_permit_request_t rS = make_request(1, platform_tcm_concurrency(), /*constraints*/nullptr,
                                         /*size*/0, TCM_REQUEST_PRIORITY_NORMAL,
                                         rigid_concurrency_flags);

  // Permit S won't negotiate with the permit A since its minimum is satisfied.
  uint32_t pS_concurrency, eS_concurrency = platform_tcm_concurrency() - eA_concurrency;

  tcm_permit_t pS = make_void_permit(&pS_concurrency);
  tcm_permit_t eS = make_active_permit(&eS_concurrency);
  eS.flags = rigid_concurrency_flags;

  phS = nullptr;
  r = tcmRequestPermit(clid, rS, &phS, &phS, &pS);
  check_success(r, "tcmRequestPermit (rigid concurrency)");
  check_permit(eA, phA);
  check_permit(eS, pS);
  check(!is_callback_invoked, "Renegotiation for the regular permit did not happen");

  r = tcmReleasePermit(phA);
  check_success(r, "tcmReleasePermit regular");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Rigid concurrency permit did not participate in the renegotiation "
                              "while in active state");

  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_IDLE;
  check_success(r, "tcmIdlePermit (rigid concurrency)");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
                              "was not negotiated since no other requests for resources exist");

  r = tcmDeactivatePermit(phS);
  eS.state = TCM_PERMIT_STATE_INACTIVE;
  check_success(r, "tcmDeactivatePermit (rigid concurrency)");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Callback was not invoked for the rigid concurrency permit that "
                              "was deactivated");

  r = tcmActivatePermit(phS);
  eS.concurrencies[0] = platform_tcm_concurrency(); eS.state = TCM_PERMIT_STATE_ACTIVE;
  check_success(r, "tcmActivatePermit (rigid concurrency)");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Callback was not invoked for the rigid concurrency permit that "
                              "was activated");

  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_IDLE;
  check_success(r, "tcmIdlePermit (rigid concurrency)");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
                              "was not negotiated since no other requests for resources exist");

  allow_rigid_concurrency_permit_negotiation = true;
  phA = nullptr;                // making a new permit
  r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  eS.state = TCM_PERMIT_STATE_INACTIVE;
  eS_concurrency = 0;
  check_success(r, "tcmRequestPermit regular while rigid concurrency permit is in idle state");
  check_permit(eA, pA);
  check_permit(eS, phS);
  check(is_callback_invoked, "Rigid concurrency permit in idle state was negotiated.");

  allow_rigid_concurrency_permit_negotiation = false;
  is_callback_invoked = false;
  r = tcmActivatePermit(phS);
  // Expected not getting previously given amount of resources as they are not available at the
  // moment, but still get as much as possible
  eS_concurrency = platform_tcm_concurrency() - eA_concurrency;
  eS.state = TCM_PERMIT_STATE_ACTIVE;
  check_success(r, "tcmActivatePermit (rigid concurrency)");
  check_permit(eS, phS);
  check_permit(eA, phA);
  check(!is_callback_invoked, "Activated rigid concurrency permit was not negotiated.");

  r = tcmReleasePermit(phA);
  check_success(r, "tcmReleasePermit (regular)");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Rigid concurrency permit in active state was not negotiated.");

  r = tcmReleasePermit(phS);
  check_success(r, "tcmReleasePermit (rigid concurrency)");
  phS = nullptr;                // avoid side effects in other tests

  disconnect_client(clid);

}

TEST("test_no_new_resources_for_rigid_concurrency") {
  // Test check that the amount of resources once given to a rigid concurrency permit does not
  // change when this permit re-activates after its renegotiation while being in IDLE state (because
  // of a separate demand for its resources) that effectively deactivated it.

  tcm_client_id_t clid = connect_new_client(renegotiation_function);

  tcm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(platform_tcm_concurrency());

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(1, (int32_t)eA_concurrency);

  tcm_result_t r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  check_success(r, "tcmRequestPermit regular");
  check_permit(eA, pA);

  // Request that shouldn't be renegotiated in active state
  tcm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  tcm_permit_request_t rS = make_request(platform_tcm_concurrency()/2, platform_tcm_concurrency(),
                                         /*constraints*/nullptr, /*size*/0,
                                         TCM_REQUEST_PRIORITY_NORMAL, rigid_concurrency_flags);

  // Permit S will negotiate with the permit A since its minimum isn't satisfied.
  uint32_t pS_concurrency, eS_concurrency = platform_tcm_concurrency()/2;

  tcm_permit_t pS = make_void_permit(&pS_concurrency);
  tcm_permit_t eS = make_active_permit(&eS_concurrency);
  eS.flags = rigid_concurrency_flags;

  is_callback_invoked = false;

  phS = nullptr;
  r = tcmRequestPermit(clid, rS, &phS, &phS, &pS);
  eA_concurrency = platform_tcm_concurrency() - platform_tcm_concurrency() / 2;
  check_success(r, "tcmRequestPermit (rigid concurrency)");
  check_permit(eA, phA);
  check_permit(eS, pS);
  check(is_callback_invoked, "Renegotiation for the regular permit happens");

  is_callback_invoked = false;
  allow_rigid_concurrency_permit_negotiation = true;
  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_INACTIVE; eS_concurrency = 0;
  eA_concurrency = platform_tcm_concurrency();
  check_success(r, "tcmIdlePermit (rigid concurrency)");
  check_permit(eS, phS);
  check_permit(eA, phA);
  // TODO: check that callback has been invoked two times - one for regular permit and another one -
  // for rigid.
  check(is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
                             "was negotiated since there are other requests for resources exist");

  allow_rigid_concurrency_permit_negotiation = false;
  is_callback_invoked = false;

  r = tcmReleasePermit(phA);
  check_success(r, "tcmReleasePermit");
  check_permit(eS, phS);
  check(!is_callback_invoked, "INACTIVE rigid concurrency permit was not negotiated.");

  r = tcmActivatePermit(phS);
  eS_concurrency = platform_tcm_concurrency();
  eS.state = TCM_PERMIT_STATE_ACTIVE;
  check_success(r, "tcmActivatePermit (rigid concurrency)");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Callback was not invoked for the rigid concurrency permit that "
                              "was activated");

  r = tcmReleasePermit(phS);
  check_success(r, "tcmReleasePermit (rigid concurrency)");
  phS = nullptr;                // avoid side effects in other tests

  disconnect_client(clid);
}

TEST("test_renegotiation_order") {
  // Test checks that satisfying a permit request searches for resources in specific order:
  // 1) Available resources
  // 2) Negotiation of the IDLE permits (including rigid concurrency ones)
  // 3) Negotiation of the ACTIVE permits

  tcm_client_id_t clid = connect_new_client(renegotiation_function);

  tcm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(platform_tcm_concurrency()/2);

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(1, (int32_t)eA_concurrency);

  tcm_result_t r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  check_success(r, "tcmRequestPermit regular " + std::to_string(eA_concurrency));
  check_permit(eA, pA);

  // Request that shouldn't be renegotiated in active state
  tcm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  uint32_t pS_concurrency = 0, eS_concurrency = platform_tcm_concurrency()/4;

  tcm_permit_request_t rS = make_request(tcm_automatic, int(eS_concurrency), /*constraints*/nullptr,
                                         /*size*/0, TCM_REQUEST_PRIORITY_NORMAL,
                                         rigid_concurrency_flags);

  tcm_permit_t pS = make_void_permit(&pS_concurrency);
  tcm_permit_t eS = make_active_permit(&eS_concurrency);
  eS.flags = rigid_concurrency_flags;
  is_callback_invoked = false;

  phS = nullptr;
  r = tcmRequestPermit(clid, rS, &phS, &phS, &pS);
  check_success(r, "tcmRequestPermit (rigid concurrency) pS " + std::to_string(eS_concurrency));
  check_permit(eA, phA);
  check_permit(eS, pS);
  check(!is_callback_invoked, "Renegotiation for the regular permit did not happen");

  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_IDLE;
  // TODO: print the request parameters in the log
  check_success(r, "tcmIdlePermit (rigid concurrency)");
  // TODO: indent corresponding permit checking messages in the log
  check_permit(eS, phS);
  check_permit(eA, phA);
  check(!is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
                              "was not negotiated since no other requests for resources exist");

  tcm_permit_handle_t phC{nullptr};
  uint32_t pC_concurrency = 0, eC_concurrency = platform_tcm_concurrency()/4;

  tcm_permit_t pC = make_void_permit(&pC_concurrency);
  tcm_permit_t eC = make_active_permit(&eC_concurrency);

  tcm_permit_request_t rC = make_request(/*min_sw_threads*/1, (int32_t)eC_concurrency);
  r = tcmRequestPermit(clid, rC, &phC, &phC, &pC);
  check_success(r, "tcmRequestPermit regular (C) " + std::to_string(eC_concurrency));
  check_permit(eC, pC);
  check_permit(eS, phS);
  check_permit(eA, phA);
  check(!is_callback_invoked, "No negotiations happen");

  r = tcmReleasePermit(phC);
  check_success(r, "tcmReleasePermit (C)");
  check_permit(eS, phS);
  check_permit(eA, phA);
  check(!is_callback_invoked, "Rigid concurrency permit in idle state was not negotiated.");

  eC_concurrency = platform_tcm_concurrency() / 2;
  rC.max_sw_threads = int32_t(eC_concurrency);
  phC = nullptr;                // make it a new request
  // The request going to negotiate IDLEd rigid concurrency permit
  allow_rigid_concurrency_permit_negotiation = true;
  r = tcmRequestPermit(clid, rC, &phC, &phC, &pC);
  eA.state = TCM_PERMIT_STATE_ACTIVE;
  eS.state = TCM_PERMIT_STATE_INACTIVE; eS_concurrency = 0;
  check_success(r, "tcmRequestPermit regular (C) " + std::to_string(rC.max_sw_threads));
  check_permit(eC, pC);
  check_permit(eA, phA);
  check_permit(eS, phS);
  check(is_callback_invoked, "Rigid concurrency permit in idle state was negotiated.");

  allow_rigid_concurrency_permit_negotiation = false;

  // TODO: Request amount of resources independent of floating point arithmetic
  eC_concurrency = 3 * platform_tcm_concurrency() / 4;
  rC.min_sw_threads = rC.max_sw_threads = eC_concurrency;
  eA_concurrency = platform_tcm_concurrency() - eC_concurrency;
  is_callback_invoked = false;
  r = tcmRequestPermit(clid, rC, &phC, &phC, &pC);
  check_success(r, "tcmRequestPermit phC regular, re-request for " + std::to_string(eC_concurrency));
  check_permit(eC, pC);
  check_permit(eA, phA);
  check_permit(eS, phS);
  check(is_callback_invoked, "ACTIVE permit was negotiated.");

  eA_concurrency = rA.max_sw_threads;
  is_callback_invoked = false;
  r = tcmReleasePermit(phC);
  check_success(r, "tcmReleasePermit phC");
  check_permits({{eA, phA}, {eS, phS}});
  check(is_callback_invoked, "ACTIVE permit phA has been negotiated");

  is_callback_invoked = false;
  r = tcmReleasePermit(phA);
  check_success(r, "tcmReleasePermit");
  check_permit(eS, phS);
  check(!is_callback_invoked, "Rigid concurrency IDLE permit phS has NOT been negotiated");

  r = tcmReleasePermit(phS);
  check_success(r, "tcmReleasePermit (rigid concurrency)");
  check(!is_callback_invoked, "Negotiation did not happen since there are no more permits");

  disconnect_client(clid);
}

TEST("test_take_from_idle_when_required_is_satisfied") {
    // The test checks that idle resources are used to satisfy desired number of requested resources
    tcm_client_id_t client = connect_new_client();
    auto rigid_request = make_request(0, platform_tcm_concurrency());
    rigid_request.flags.rigid_concurrency = true;
    auto rigid_ph = request_permit(client, rigid_request, /*callback_arg*/nullptr);
    auto rigid_permit_actual_data = get_permit_data<>(rigid_ph);
    auto expected_rigid_permit = make_active_permit(/*expected resources*/ platform_tcm_concurrency(),
                                                    /*cpu_masks*/nullptr,
                                                    rigid_request.flags);
    check_permit(expected_rigid_permit, rigid_permit_actual_data);

    idle_permit(rigid_ph);

    auto request = make_request(0, platform_tcm_concurrency());
    auto ph = request_permit(client, request, /*callback*/nullptr);
    auto permit_actual_data = get_permit_data<>(ph);
    auto expected_data = make_active_permit(/*expected resources*/platform_tcm_concurrency());
    auto new_expected_rigid_permit = make_inactive_permit(/*cpu_masks*/nullptr, rigid_request.flags);
    auto new_rigid_permit_data = get_permit_data<>(rigid_ph);
    check_permit(expected_data, permit_actual_data);
    check_permit(new_expected_rigid_permit, new_rigid_permit_data);

    // TODO: utilize RAII for release and disconnect
    release_permit(ph, "Failed to release regular permit handle");
    release_permit(rigid_ph, "Failed to release rigid concurrency permit handle");
    disconnect_client(client);
}

TEST("test_thread_registration") {
  tcm_client_id_t clid = connect_new_client();

  tcm_permit_handle_t ph{nullptr};
  uint32_t p_concurrency;
  tcm_permit_t p = make_void_permit(&p_concurrency);
  uint32_t e_concurrency = platform_tcm_concurrency();
  tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_result_t r = tcmRegisterThread(ph);
  check(r == TCM_RESULT_ERROR_UNKNOWN, "tcmRegisterThread for empty permit handle");

  tcm_permit_request_t req = make_request(0, platform_tcm_concurrency());
  r = tcmRequestPermit(clid, req, nullptr, &ph, &p);
  check_success(r, "tcmRequestPermit");
  check_permit(e, p);

  r = tcmRegisterThread(ph);
  check_success(r, "tcmRegisterThread");

  r = tcmUnregisterThread();
  check_success(r, "tcmUnregisterThread");

  r = tcmReleasePermit(ph);
  check_success(r, "tcmReleasePermit");

  disconnect_client(clid);
}

TEST("test_default_constraints_construction") {
  tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
  check(constraints.min_concurrency == tcm_automatic, "Check default min_concurrency value");
  check(constraints.max_concurrency == tcm_automatic, "Check default max_concurrency value");
  check(constraints.mask == nullptr, "Check default mask value");
  check(constraints.numa_id == tcm_automatic, "Check default numa_id value");
  check(constraints.core_type_id == tcm_automatic, "Check default core_type_id value");
  check(constraints.threads_per_core == tcm_automatic, "Check default threads_per_core value");
}

TEST("test_request_initializer") {
  tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
  check(request.min_sw_threads == tcm_automatic, "Check default min_sw_threads value");
  check(request.max_sw_threads == tcm_automatic, "Check default max_sw_threads value");
  check(request.cpu_constraints == nullptr, "Check default cpu_constraints value");
  check(request.constraints_size == 0, "Check default constraints_size value");

  check(!request.flags.stale, "Check default permit flags - stale flag");
  check(!request.flags.rigid_concurrency, "Check default permit flags - rigid_concurrency flag");
  check(!request.flags.exclusive, "Check default permit flags - exluisive flag");
  check(!request.flags.reserved, "Check default permit flags - reserved field");

  std::size_t count = 0;
  for (const auto& el: request.reserved) {
    check(el == 0, "Check request reserved fields value");
    ++count;
  }
  check(count == 4, "All reserved fields equals to zero");
}

TEST("test_get_stale_permit") {
  // TODO: implement the test
}

static_assert(sizeof(tcm_permit_flags_t) == 4, "The permit flags type has wrong size");
static_assert(sizeof(tcm_callback_flags_t) == 4, "The callback flags type has wrong size");

TEST("test_allow_not_specifying_client_callback") {
  tcm_client_id_t client_id = connect_new_client();

  const int num_requests = 2;
  std::vector<tcm_permit_handle_t> handles(num_requests, nullptr);
  std::vector<tcm_permit_t> permits(2 * num_requests); // actual + expected
  std::vector<uint32_t> permit_concurrencies = {0, 0, uint32_t(platform_tcm_concurrency()), 0};
  for (auto i = 0; i < num_requests; ++i) {
    tcm_permit_request_t req = TCM_PERMIT_REQUEST_INITIALIZER;
    req.min_sw_threads = req.max_sw_threads = platform_tcm_concurrency();
    permits[i] = make_void_permit(&permit_concurrencies[i]);
    tcm_result_t r = tcmRequestPermit(client_id, req, /*callback_arg*/nullptr, &handles[i],
                                      &permits[i]);
    check_success(r, "tcmRequestPermit " + std::to_string(i));
  }

  permits[num_requests] = make_active_permit(&permit_concurrencies[num_requests]);
  permits[num_requests+1] = make_pending_permit(&permit_concurrencies[num_requests+1]);

  // Checking permits
  for (auto i = 0; i < num_requests; ++i) {
    auto& expected = permits[num_requests+i];
    check_permit(expected, permits[i]);
  }

  // Releasing permits, entailing negotiation
  tcm_result_t r = tcmReleasePermit(handles[0]);
  check_success(r, "tcmReleasePermit (active permit)");

  permit_concurrencies[num_requests+1] = uint32_t(platform_tcm_concurrency());
  permits[num_requests+1] = make_active_permit(&permit_concurrencies[num_requests+1]);
  check_permit(permits[num_requests+1], handles[1]);

  r = tcmReleasePermit(handles[1]);
  check_success(r, "tcmReleasePermit (last permit)");

  disconnect_client(client_id);
}

namespace RequestAsInactive {

tcm_permit_flags_t request_as_inactive_flag{/*stale*/ false, /*rigid_concurrency*/ false,
                                            /*exclusive*/ false, /*request_as_inactive*/ true,
                                            /*reserved*/ 0};

TEST("allow_request_as_inactive") {
    auto client_id = connect_new_client();

    tcm_permit_request_t req = make_request(/*min_sw_threads*/ platform_tcm_concurrency(),
                                            /*max_sw_threads*/ platform_tcm_concurrency(),
                                            /*constraints*/ nullptr, /*size*/ 0,
                                            /*priority*/ TCM_REQUEST_PRIORITY_NORMAL,
                                            request_as_inactive_flag);

    auto expected_permit = make_inactive_permit(/*cpu_masks*/ nullptr, request_as_inactive_flag);
    tcm_permit_handle_t ph = request_permit(client_id, req);
    check_permit(expected_permit, ph);
    release_permit(ph);
    disconnect_client(client_id);
}

TEST("allow_request_as_inactive_for_deactivated") {
    renegotiating_permits = {};

    tcm_client_id_t client_id = connect_new_client(client_renegotiate);
    int32_t min_sw_threads = platform_tcm_concurrency()/2;
    int32_t max_sw_threads = platform_tcm_concurrency();
    auto req = make_request(min_sw_threads, max_sw_threads);
    auto ph = request_permit(client_id, req);
    auto expected_permit = make_active_permit(max_sw_threads);
    check_permit(expected_permit, ph);

    // Request another that negotiates the first
    auto req2 = make_request(/*min_sw_threads*/platform_tcm_concurrency()/2,
                             /*max_sw_threads*/platform_tcm_concurrency()/2);
    renegotiating_permits = {ph}; allow_null_in_callback_arg = true;
    is_client_renegotiate_callback_invoked = false;
    tcm_permit_t& p = expected_permit;
    p.concurrencies[0] = min_sw_threads;
    auto ph2 = request_permit(client_id, req2);
    auto expected_permit2 = make_active_permit(platform_tcm_concurrency()/2);
    check_permit(expected_permit, ph);
    check_permit(expected_permit2, ph2);
    check(is_client_renegotiate_callback_invoked, "Client callback was invoked");

    // Deactivate the first
    is_client_renegotiate_callback_invoked = false;
    allow_null_in_callback_arg = false;
    deactivate_permit(ph);
    p.concurrencies[0] = 0; // TODO: Set expected permit concurrency back to minimum here, i.e.,
    // to platform_tcm_concurrency()/2, when "lazy inactive permit"
    // feature does not release the concurrency from the deactivated
    // permit if there is no actual demand from other stakeholders
    p.state = TCM_PERMIT_STATE_INACTIVE;
    check_permit(expected_permit, ph);
    check_permit(expected_permit2, ph2);
    check(!is_client_renegotiate_callback_invoked, "Client callback was not invoked");

    // Update the first permit request parameters so that its minimum is still satisfied
    req.min_sw_threads = 1; req.flags.request_as_inactive = true;
    p.flags.request_as_inactive = true;
    tcm_permit_handle_t ph_prev = ph;
    ph = request_permit(client_id, req, /*callback*/nullptr, ph);
    check_permit(expected_permit, ph);
    check_permit(expected_permit2, ph2);
    check(!is_client_renegotiate_callback_invoked, "Client callback was not invoked");
    check(ph == ph_prev, "Permit handle was not changed");

    // Activating the first and check that both permits have expected resources distribution
    activate_permit(ph);
    p.concurrencies[0] = max_sw_threads - platform_tcm_concurrency()/2;
    p.state = TCM_PERMIT_STATE_ACTIVE; p.flags.request_as_inactive = false;
    check_permit(expected_permit, ph);
    check_permit(expected_permit2, ph2);
    check(!is_client_renegotiate_callback_invoked, "Client callback was not invoked");

    release_permit(ph);
    release_permit(ph2);

    disconnect_client(client_id);
}

TEST("allow_change_constraints_for_requested_as_inactive") {
    // Tests checks that it is allowed to change the constraints of a permit_handle that was
    // initially requested as inactive with constraints.

    auto expected_permit = make_inactive_permit(/*cpu_masks*/ nullptr, request_as_inactive_flag);

    // Request as inactive with one set of constraints
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    constraints.numa_id = tcm_any;

    auto req = make_request(/*min_sw_threads*/1, /*max_sw_threads*/platform_tcm_concurrency()/2,
                            &constraints, /*size*/1, TCM_REQUEST_PRIORITY_NORMAL,
                            request_as_inactive_flag);

    auto client_id = connect_new_client();
    auto ph = request_permit(client_id, req);
    check_permit(expected_permit, ph);

    // Request resources with changed constraints
    constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    constraints.min_concurrency = 0; constraints.max_concurrency = platform_tcm_concurrency();
    req = make_request(/*min_sw_threads*/platform_tcm_concurrency()/2,
                       /*max_sw_threads*/platform_tcm_concurrency(), &constraints, /*size*/1);

    tcm_cpu_mask_t mask = allocate_cpu_mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> req_mask_guard(&mask);
    uint32_t p_concurrency = platform_tcm_concurrency();
    auto p = make_permit(&p_concurrency, &mask);
    auto ph_prev = ph;
    tcm_result_t r = tcmRequestPermit(client_id, req, /*callback_arg*/nullptr, &ph, &p);
    auto expected_permit2 = make_active_permit(platform_tcm_concurrency());
    skip_checks_t skip_masks; skip_masks.mask = true;
    check_success(r, "tcmRequestPermit on initialized permit with constraints");
    check_permit(expected_permit2, p, skip_masks);
    check(has_masks(p), "Masks exist");
    check(ph == ph_prev, "The permit handle was not changed");

    release_permit(ph);
    disconnect_client(client_id);
}

TEST("allow_change_callback_arg_for_requested_as_inactive") {
    // The test checks that it is allowed to change callback argument for requested as inactive
    // permit request
    tcm_client_id_t client_id = connect_new_client(client_renegotiate);

    // Request as inactive first
    auto expected_permit = make_inactive_permit(/*cpu_masks*/ nullptr, request_as_inactive_flag);
    tcm_permit_request_t req = make_request(/*min_sw_threads*/platform_tcm_concurrency(),
                                            /*max_sw_threads*/platform_tcm_concurrency());
    req.flags.request_as_inactive = true;
    tcm_permit_handle_t ph = request_permit(client_id, req, /*callback_arg*/nullptr);
    check_permit(expected_permit, ph);

    // Update permit request with different request parameters including callback argument
    tcm_permit_handle_t ph_prev = ph;
    req.min_sw_threads = 1; req.max_sw_threads = platform_tcm_concurrency()/2;
    tcm_result_t r = tcmRequestPermit(client_id, req, /*callback_arg*/&ph, &ph, /*permit*/nullptr);
    check_success(r, "Re-initializing permit_handle using different request and callback");
    check_permit(expected_permit, ph);
    check(ph == ph_prev, "Permit handle was not changed");

    tcm_permit_t& ep = expected_permit;
    ep.concurrencies[0] = platform_tcm_concurrency()/2; ep.state = TCM_PERMIT_STATE_ACTIVE;
    ep.flags.request_as_inactive = false;
    activate_permit(ph, "Error activating permit");
    check_permit(ep, ph);

    // Check updating callback argument works.
    is_client_renegotiate_callback_invoked = false;
    renegotiating_permits = {ph};
    req = make_request(platform_tcm_concurrency() - 1, platform_tcm_concurrency());
    auto ph2 = request_permit(client_id, req, /*callback_arg*/ nullptr);
    ep.concurrencies[0] = 1; auto e2 = make_active_permit(platform_tcm_concurrency() - 1);
    check_permit(ep, ph);
    check_permit(e2, ph2);
    check(is_client_renegotiate_callback_invoked, "Client callback was invoked");

    renegotiating_permits = {ph2}; allow_null_in_callback_arg = true;
    release_permit(ph);
    release_permit(ph2);
    allow_null_in_callback_arg = false;
}

TEST("prohibit_request_as_inactive_for_activated") {
    auto client_id = connect_new_client();
    tcm_permit_request_t req =
        make_request(/*min_sw_threads*/ 1, /*max_sw_threads*/ platform_tcm_concurrency());

    auto expected_permit = make_active_permit(platform_tcm_concurrency());
    tcm_permit_handle_t ph = request_permit(client_id, req);
    check_permit(expected_permit, ph);

    req.min_sw_threads = req.max_sw_threads; req.flags.request_as_inactive = true;
    auto prev_ph = ph;
    auto r = tcmRequestPermit(client_id, req, /*callback_arg*/ nullptr, &ph, /*permit*/ nullptr);
    check_fail(r, "Got error in re-initializing owning permit_handle");
    check_permit(expected_permit, ph);
    check(ph == prev_ph, "Permit handle was not changed");

    release_permit(ph);
    disconnect_client(client_id);
}

TEST("prohibit_request_as_inactive_for_pending") {
    tcm_client_id_t client_id = connect_new_client();
    int32_t min_sw_threads = platform_tcm_concurrency();
    int32_t max_sw_threads = platform_tcm_concurrency();
    auto req = make_request(min_sw_threads, max_sw_threads);
    auto ph = request_permit(client_id, req);
    auto expected_permit = make_active_permit(max_sw_threads);
    check_permit(expected_permit, ph);

    // Request another that gets PENDING state
    auto req2 = make_request(/*min_sw_threads*/platform_tcm_concurrency()/2,
                             /*max_sw_threads*/platform_tcm_concurrency()/2);
    auto ph2 = request_permit(client_id, req2);
    uint32_t e_concurrency = 0; auto expected_permit2 = make_pending_permit(&e_concurrency);
    check_permit(expected_permit, ph);
    check_permit(expected_permit2, ph2);

    // Update pending permit request and expect error
    req2.min_sw_threads = 1; req2.flags.request_as_inactive = true;
    tcm_permit_handle_t ph2_prev = ph2;
    tcm_result_t r = tcmRequestPermit(client_id, req2, /*callback_arg*/nullptr, &ph2, /*permit*/nullptr);
    ph2 = request_permit(client_id, req, /*callback*/nullptr, ph2);
    check_fail(r, "Got error when requesting as inactive for pending");
    check(ph2 == ph2_prev, "Permit handle 2 was not changed");
    check_permit(expected_permit, ph);
    check_permit(expected_permit2, ph2);

    release_permit(ph2);
    release_permit(ph);
    disconnect_client(client_id);
}

TEST("prohibit_permit_reallocation_for_requested_as_inactive") {
    auto client_id = connect_new_client();
    tcm_permit_request_t req =
        make_request(/*min_sw_threads*/ platform_tcm_concurrency(),
                     /*max_sw_threads*/ platform_tcm_concurrency(),
                     /*constraints*/ nullptr, /*size*/ 0,
                     /*priority*/ TCM_REQUEST_PRIORITY_NORMAL, request_as_inactive_flag);

    auto expected_permit = make_inactive_permit(/*cpu_masks*/ nullptr, request_as_inactive_flag);
    tcm_permit_handle_t ph = request_permit(client_id, req);
    check_permit(expected_permit, ph);

    auto ph_prev = ph;
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    constraints.core_type_id = tcm_any;
    req.cpu_constraints = &constraints; req.constraints_size = 1;
    tcm_result_t r = tcmRequestPermit(client_id, req, /*callback*/nullptr, &ph, /*permit*/nullptr);
    check_fail(r, "Got error in re-initializing permit_handle with constraints");
    check(ph == ph_prev, "Permit handle was not changed");

    release_permit(ph);
    disconnect_client(client_id);
}

TEST("erroneous_request_as_inactive_does_not_change_request_parameters") {
    auto client_id = connect_new_client();
    tcm_permit_request_t req =
        make_request(/*min_sw_threads*/ 1, /*max_sw_threads*/ platform_tcm_concurrency(),
                     /*constraints*/ nullptr, /*size*/ 0,
                     /*priority*/ TCM_REQUEST_PRIORITY_NORMAL, request_as_inactive_flag);
    tcm_permit_handle_t ph = request_permit(client_id, req);
    auto expected_permit = make_inactive_permit(/*cpu_masks*/ nullptr, request_as_inactive_flag);
    check_permit(expected_permit, ph);

    auto ph_prev = ph;
    req.min_sw_threads = req.max_sw_threads = 1;
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    constraints.max_concurrency = 2;
    // Avoid MSVC C4312: 'operation' : conversion from 'unsigned int' to 'tcm_cpu_mask_t' of
    // greater size
    constraints.mask = (tcm_cpu_mask_t)(unsigned long long)(0xDEADBEEF);
    req.cpu_constraints = &constraints; req.constraints_size = 1;
    tcm_result_t r = tcmRequestPermit(client_id, req, /*callback*/nullptr, &ph, /*permit*/nullptr);
    check_fail(r, "Got error while updating permit parameters that require reallocation");
    check(ph == ph_prev, "Permit handle was not changed");

    uint32_t p_concurrency = 0; tcm_permit_t p = make_void_permit(&p_concurrency);
    r = tcmGetPermitData(ph, &p);
    check_success(r, "Reading permit data for " + to_string(ph));
    check_permit(expected_permit, p);

    auto e = make_active_permit(platform_tcm_concurrency());
    activate_permit(ph, "Error activating permit");
    check_permit(e, ph);

    release_permit(ph);
    disconnect_client(client_id);
}

} // namespace RequestAsInactive

TEST("test_request_initialized_by_default") {
    tcm_client_id_t client = connect_new_client();
    {
        auto req = make_request(); // default request without constraints
        auto ph = request_permit(client, req, /*callback_arg*/nullptr);
        auto actual_permit = get_permit_data<>(ph);
        auto expected_permit = make_active_permit(
            /*expected_concurrency*/platform_hardware_concurrency()
        );
        check_permit(expected_permit, actual_permit);
        // TODO: utilize RAII for release and disconnect
        release_permit(ph, "Failed to release permit handle");
    }
    {
        tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
        constraints.numa_id = tcm_any;
        auto req = make_request(tcm_automatic, tcm_automatic, &constraints, /*size*/1);
        auto ph = request_permit(client, req, /*callback_arg*/nullptr);
        auto actual_permit = get_permit_data<>(ph);
        tcm_permit_t expected_permit = tcm_permit_t{
            /*concurrencies*/nullptr, /*constraints*/nullptr, /*size*/1, TCM_PERMIT_STATE_ACTIVE,
            tcm_permit_flags_t{}
        };
        skip_checks_t skip_concurrency_check; skip_concurrency_check.concurrency = true;
        check_permit(expected_permit, actual_permit, skip_concurrency_check);

        // Check that the permit was given some concurrency
        check(tcm_permit_t(actual_permit).concurrencies[0] > 0,
              "Actual concurrency is not what is expected");

        // TODO: utilize RAII for release and disconnect
        release_permit(ph, "Failed to release permit handle");
    }

    disconnect_client(client);
}


TEST("test_incorrect_requests") {
  tcm_client_id_t client = connect_new_client();

  auto has_tcm_returned_invalid_arg = [client] (const tcm_permit_request_t& req) {
      tcm_permit_handle_t ph{nullptr};
      auto r = tcmRequestPermit(client, req, /*callback_arg*/nullptr, &ph, /*permit*/nullptr);
      return TCM_RESULT_ERROR_INVALID_ARGUMENT == r;
  };

  { // default request with default constraints
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    auto req = make_request(tcm_automatic, tcm_automatic, &constraints, /*size*/1);
    check(has_tcm_returned_invalid_arg(req), "Default request with default constraints returned "
                                             "invalid argument status");
  }
  { // meaningful request with non-meaningful constraints
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    auto req = make_request(tcm_automatic, platform_hardware_concurrency(), &constraints, /*size*/1);
    check(has_tcm_returned_invalid_arg(req), "Request with non-meaningful constraints returned "
                                             "invalid argument status");
  }

  { // request with incorrect constraints size
    auto req = make_request(tcm_automatic, tcm_automatic, /*constraints*/nullptr, /*size*/0);
    std::uintptr_t dummy_constraints = 0xABCDEFED;
    req.cpu_constraints = (tcm_cpu_constraints_t*)dummy_constraints;
    check(has_tcm_returned_invalid_arg(req), "Request with constraints but zero size returned "
                                             "invalid argument status");
  }

  { // re-request with incorrect size
    auto req = make_request();
    auto ph = request_permit(client, req, /*callback_arg*/nullptr);
    req.constraints_size = 1;             // new size for constraints
    auto r = tcmRequestPermit(client, req, /*callback_arg*/nullptr, &ph, /*permit*/nullptr);
    check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT,
          "Re-request with incorrect constraints size returned invalid argument status");
    // TODO: utilize RAII for release and disconnect
    release_permit(ph, "Failed to release permit handle");
  }

  { // request with negative demand range
    auto req = make_request(-10, -5);
    check(has_tcm_returned_invalid_arg(req), "Request with negative demand range returned invalid "
                                             "argument status");

    tcm_cpu_constraints_t c = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    c.min_concurrency = -10; c.max_concurrency = -5;
    c.numa_id = tcm_any;
    req.constraints_size = 1;
    req.cpu_constraints = &c;
    check(has_tcm_returned_invalid_arg(req), "Request with negative demand range returned invalid "
                                             "argument status");
  }

  { // request with incorrect demand range
    auto req = make_request(-10, 10);
    check(has_tcm_returned_invalid_arg(req), "Request with incorrect demand range returned invalid "
                                             "argument status");
    tcm_cpu_constraints_t c = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    c.min_concurrency = 0; c.max_concurrency = 100;
    c.numa_id = tcm_any;
    req.constraints_size = 1;
    req.cpu_constraints = &c;
    check(has_tcm_returned_invalid_arg(req), "Request with negative demand range returned invalid "
                                             "argument status");
  }

  { // request with oversubscribed demand
    auto req = make_request(2 * platform_tcm_concurrency(), 2 * platform_tcm_concurrency());
    check(has_tcm_returned_invalid_arg(req), "Request with oversubscribed demand returned invalid "
                                             "argument status");

    tcm_cpu_constraints_t c = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    c.min_concurrency = 1; c.max_concurrency = 2 * platform_tcm_concurrency();
    c.numa_id = tcm_any;
    req.constraints_size = 1;
    req.cpu_constraints = &c;
    check(has_tcm_returned_invalid_arg(req), "Request with oversubscribing demand returned invalid "
                                             "argument status");
  }

  { // request overflow
    auto req = make_request();
    const int size = 2;
    tcm_cpu_constraints_t c[size];
    for (int i = 0; i < size; ++i) {
        c[i] = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
        c[i].min_concurrency = c[i].max_concurrency = std::numeric_limits<int32_t>::max() / 2 + 2;
        c[i].numa_id = tcm_any;
    }
    req.constraints_size = size;
    req.cpu_constraints = c;
    check(has_tcm_returned_invalid_arg(req), "Request with overflow returned invalid argument status");

    c[0].min_concurrency = c[1].min_concurrency = 0;
    check(has_tcm_returned_invalid_arg(req), "Request with overflow for max_concurrency in "
                                             "constraints returned invalid argument status");

    c[0].min_concurrency = c[1].min_concurrency = std::numeric_limits<int32_t>::max() / 2 + 2;
    c[0].max_concurrency = c[1].max_concurrency = 10;
    check(has_tcm_returned_invalid_arg(req), "Request with overflow for min_concurrency in "
                                             "constraints returned invalid argument status");
  }
  disconnect_client(client);
}

TEST("test_releasing_nullptr") {
    auto client_id = connect_new_client(nullptr);

    auto r = tcmReleasePermit(/*permit_handle*/nullptr);
    check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT,
          "tcmReleasePermit(nullptr) returns invalid argument");

    disconnect_client(client_id);
}

TEST("test_releasing_inactive") {
    auto client_id = connect_new_client(nullptr);

    tcm_permit_handle_t ph{nullptr};
    uint32_t p_concurrency;
    tcm_permit_t p = make_void_permit(&p_concurrency);
    uint32_t e_concurrency = platform_tcm_concurrency();
    tcm_permit_t e = make_active_permit(&e_concurrency);

    // Check that lazy inactive permit is released successfully
    tcm_permit_request_t req = make_request(0, platform_tcm_concurrency());
    auto r = tcmRequestPermit(client_id, req, nullptr, &ph, &p);
    check_success(r, "tcmRequestPermit");
    check_permit(e, p);

    r = tcmDeactivatePermit(ph);
    e.state = TCM_PERMIT_STATE_INACTIVE;
    check_success(r, "tcmDeactivatePermit");
    check_permit(e, ph);

    r = tcmReleasePermit(ph);
    check_success(r, "tcmReleasePermit");

    // Check that resources were released from released lazy inactive permit
    ph = nullptr;
    e.state = TCM_PERMIT_STATE_ACTIVE;
    r = tcmRequestPermit(client_id, req, nullptr, &ph, &p);
    check_success(r, "tcmRequestPermit");
    check_permit(e, p);

    r = tcmReleasePermit(ph);
    check_success(r, "tcmReleasePermit");

    disconnect_client(client_id);
}

namespace TestDeactivatingInactive {

void test_deactivating_inactive(const bool release_while_active) {
    constexpr unsigned num_deactivations = 3;

    auto client_id = connect_new_client(nullptr);

    uint32_t e_concurrency = platform_tcm_concurrency();
    tcm_permit_t e = make_active_permit(&e_concurrency);

    tcm_permit_request_t req = make_request(0, platform_tcm_concurrency());
    auto ph = request_permit(client_id, req);
    check_permit(e, ph);

    e.state = TCM_PERMIT_STATE_INACTIVE; // concurrency is not changed since it is lazy deactivation
    for (unsigned i = 0; i < num_deactivations; ++i) {
        deactivate_permit(ph);
        check_permit(e, ph);
    }

    if (release_while_active) {
        activate_permit(ph);
        e.state = TCM_PERMIT_STATE_ACTIVE;
        check_permit(e, ph);
    }

    release_permit(ph);
    disconnect_client(client_id);
}

TEST("Multiple deactivations of inactive permit and releasing after reactivation") {
    test_deactivating_inactive(/*release_while_active*/true);
}

TEST("Multiple deactivations of inactive permit") {
    test_deactivating_inactive(/*release_while_active*/false);
}

} // namespace TestDeactivatingInactive

TEST("Release of client permits when it disconnects") {
  tcm_client_id_t client_id = connect_new_client(/*callback*/nullptr);

  const unsigned num_requests = platform_tcm_concurrency() + /*INACTIVE*/1 + /*PENDING*/1;
  auto req = make_request(/*min_sw_threads*/1);

  uint32_t concurrency = platform_tcm_concurrency();
  auto expected = make_active_permit(&concurrency);
  skip_checks_t skip_concurrency_check{
    /*size*/false, /*concurrency*/true, /*state*/false, /*flags*/false, /*mask*/false
  };
  for (auto i = 0u; i < num_requests; ++i) {
    auto ph = request_permit(client_id, req);

    expected.state = TCM_PERMIT_STATE_ACTIVE;
    if (i > uint32_t(platform_tcm_concurrency()))
      expected.state = TCM_PERMIT_STATE_PENDING;

    check_permit(expected, ph, skip_concurrency_check);

    if (i == 0) {
      deactivate_permit(ph);
      expected.state = TCM_PERMIT_STATE_INACTIVE;
      check_permit(expected, ph, skip_concurrency_check);
    }
  }

  disconnect_client(client_id);

  assert_all_resources_available();

  // Test disconnecting while holding IDLE permit
  client_id = connect_new_client();
  int32_t min_sw_threads = platform_tcm_concurrency(), max_sw_threads = min_sw_threads;
  auto ph = request_permit(client_id, make_request(min_sw_threads, max_sw_threads));
  permit_t</*size*/1> expected_permit_wrapper = make_active_permit(max_sw_threads);
  tcm_permit_t& expected_permit = expected_permit_wrapper;
  check_permit(expected_permit, ph);
  idle_permit(ph);
  expected_permit.state = TCM_PERMIT_STATE_IDLE;
  check_permit(expected_permit, ph);
  disconnect_client(client_id);

  assert_all_resources_available();
}
