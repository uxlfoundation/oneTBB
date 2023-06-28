/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "test_utils.h"

#include "tcm.h"

#include <cstdint>

bool test_state_functions() {
  const char* test_name = "test_state_functions";
  test_prolog(test_name);

  tcm_client_id_t clid;
  tcm_result_t r = tcmConnect(nullptr, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_handle_t ph{nullptr};
  uint32_t p_concurrency;
  tcm_permit_t p = make_void_permit(&p_concurrency);
  uint32_t e_concurrency = num_oversubscribed_resources;
  tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_permit_request_t req = make_request(0, num_oversubscribed_resources);
  r = tcmRequestPermit(clid, req, nullptr, &ph, &p);
  if (!(check_success(r, "tcmRequestPermit") && check_permit(e, p)))
    return test_fail(test_name);

  r = tcmIdlePermit(ph);
  e.state = TCM_PERMIT_STATE_IDLE;
  if (!(check_success(r, "tcmIdlePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmActivatePermit(ph);
  e.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit 1") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmDeactivatePermit(ph);
  e_concurrency = 0;
  e.state = TCM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "tcmDeactivatePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmActivatePermit(ph);
  e_concurrency = num_oversubscribed_resources;
  e.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit 2") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmIdlePermit(ph);
  e.state = TCM_PERMIT_STATE_IDLE;
  if (!(check_success(r, "tcmIdlePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmDeactivatePermit(ph);
  e_concurrency = 0;
  e.state = TCM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "tcmDeactivatePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmActivatePermit(ph);
  e_concurrency = num_oversubscribed_resources;
  e.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit 3") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmReleasePermit(ph);
  if (!check_success(r, "tcmReleasePermit"))
    return test_fail(test_name);

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  std::cout << "test_state_functions done" << std::endl;
  return test_epilog(test_name);
}

bool test_pending_state() {
  const char* test_name = "test_pending_state";
  test_prolog(test_name);

  tcm_client_id_t clid;
  tcm_result_t r = tcmConnect(nullptr, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_handle_t phA{nullptr}, phB{nullptr};
  uint32_t pA_concurrency{0}, pB_concurrency{0};
  uint32_t eA_concurrency{0}, eB_concurrency{0};
  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency),
               eB = make_pending_permit(&eB_concurrency);

  auto reqA = make_request(0, num_oversubscribed_resources);
  reqA.flags.rigid_concurrency = true;
  eA_concurrency = num_oversubscribed_resources; eA.flags.rigid_concurrency = true;
  r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit for A") && check_permit(eA, pA)))
    return test_fail(test_name);

  tcm_permit_request_t reqB = make_request(num_oversubscribed_resources, num_oversubscribed_resources);
  r = tcmRequestPermit(clid, reqB, nullptr, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit for B") && check_permit(eB, pB)))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  if (!check_success(r, "tcmReleasePermit for B"))
    return test_fail(test_name);

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit for A"))
    return test_fail(test_name);

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_activate_pending_when_one_deactivates() {
  const char* test_name = "test_activate_pending_when_one_deactivates";
  test_prolog(test_name);

  tcm_client_id_t clid;
  tcm_result_t r = tcmConnect(nullptr, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_handle_t phA{nullptr}, phB{nullptr};
  uint32_t pA_concurrency{0}, pB_concurrency{0};
  uint32_t eA_concurrency{0}, eB_concurrency{0};
  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               pB = make_void_permit(&pB_concurrency);
  tcm_permit_t eA = make_active_permit(&eA_concurrency),
               eB = make_pending_permit(&eB_concurrency);

  tcm_permit_request_t reqA = make_request(1, num_oversubscribed_resources);
  reqA.flags.rigid_concurrency = true;
  eA_concurrency = num_oversubscribed_resources;
  eA.flags.rigid_concurrency = true;
  r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit for A") && check_permit(eA, pA)))
    return test_fail(test_name);

  tcm_permit_request_t reqB = make_request(1, num_oversubscribed_resources);
  reqB.flags.rigid_concurrency = true;
  eB.flags.rigid_concurrency = true;
  r = tcmRequestPermit(clid, reqB, nullptr, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit for B") && check_permit(eB, pB)))
    return test_fail(test_name);

  r = tcmDeactivatePermit(phA);
  eA_concurrency = 0;
  eA.state = TCM_PERMIT_STATE_INACTIVE;
  if (!check_success(r, "tcmDeactivatePermit for A") && check_permit(eA, phA))
    return test_fail(test_name);

  eB_concurrency = num_oversubscribed_resources;
  eB.state = TCM_PERMIT_STATE_ACTIVE;
  if (!check_permit(eB, phB))
    return test_fail(test_name);

  r = tcmReleasePermit(phB);
  if (!check_success(r, "tcmReleasePermit for B"))
    return test_fail(test_name);

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit for A"))
    return test_fail(test_name);

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  return test_epilog(test_name);
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

bool test_no_negotiation_for_active_rigid_concurrency() {
  // The test checks that negotiation for rigid concurrency permits does not happen while they are
  // in ACTIVE state, but deactivation of such permits happens when they are switched to IDLE state
  // and there is a competing resources demand.

  // TODO: based on the test description above consider splitting on two tests

  const char* test_name = __func__;
  test_prolog(test_name);

  tcm_client_id_t clid{0};
  is_callback_invoked = false;

  tcm_result_t r = tcmConnect(renegotiation_function, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(num_oversubscribed_resources / 2);

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(num_oversubscribed_resources/4, (int32_t)eA_concurrency);

  r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit regular") && check_permit(eA, pA)))
    return test_fail(test_name);

  // Request that shouldn't be renegotiated in active state
  tcm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  tcm_permit_request_t rS = make_request(1, num_oversubscribed_resources, /*constraints*/nullptr,
                                         /*size*/0, TCM_REQUEST_PRIORITY_NORMAL,
                                         rigid_concurrency_flags);

  // Permit S won't negotiate with the permit A since its minimum is satisfied.
  uint32_t pS_concurrency, eS_concurrency = num_oversubscribed_resources - eA_concurrency;

  tcm_permit_t pS = make_void_permit(&pS_concurrency);
  tcm_permit_t eS = make_active_permit(&eS_concurrency);
  eS.flags = rigid_concurrency_flags;

  phS = nullptr;
  r = tcmRequestPermit(clid, rS, &phS, &phS, &pS);
  if (!(check_success(r, "tcmRequestPermit (rigid concurrency)") &&
        check_permit(eA, phA) && check_permit(eS, pS) &&
        check(!is_callback_invoked, "Renegotiation for the regular permit did not happen")))
  {
    return test_fail(test_name);
  }

  r = tcmReleasePermit(phA);
  if (!(check_success(r, "tcmReleasePermit regular") && check_permit(eS, phS) &&
        check(!is_callback_invoked, "Rigid concurrency permit did not participate in the "
              "renegotiation while in active state"))) {
    return test_fail(test_name);
  }

  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_IDLE;
  if (!(check_success(r, "tcmIdlePermit (rigid concurrency)") && check_permit(eS, phS) &&
        check(!is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
              "was not negotiated since no other requests for resources exist")))
  {
    return test_fail(test_name);
  }

  r = tcmDeactivatePermit(phS);
  eS.concurrencies[0] = 0; eS.state = TCM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "tcmDeactivatePermit (rigid concurrency)") && check_permit(eS, phS) &&
      check(!is_callback_invoked, "Callback was not invoked for the rigid concurrency permit that "
            "was deactivated")))
  {
    return test_fail(test_name);
  }

  r = tcmActivatePermit(phS);
  eS.concurrencies[0] = num_oversubscribed_resources; eS.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit (rigid concurrency)") && check_permit(eS, phS) &&
      check(!is_callback_invoked, "Callback was not invoked for the rigid concurrency permit that "
            "was activated")))
  {
    return test_fail(test_name);
  }

  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_IDLE;
  if (!(check_success(r, "tcmIdlePermit (rigid concurrency)") && check_permit(eS, phS) &&
        check(!is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
              "was not negotiated since no other requests for resources exist")))
  {
    return test_fail(test_name);
  }

  allow_rigid_concurrency_permit_negotiation = true;
  phA = nullptr;                // making a new permit
  r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  eS.state = TCM_PERMIT_STATE_INACTIVE;
  eS_concurrency = 0;
  if (!(check_success(r, "tcmRequestPermit regular while rigid concurrency permit is in idle state")
        && check_permit(eA, pA) && check_permit(eS, phS) &&
        check(is_callback_invoked, "Rigid concurrency permit in idle state was negotiated.")))
    return test_fail(test_name);

  allow_rigid_concurrency_permit_negotiation = false;
  is_callback_invoked = false;
  r = tcmActivatePermit(phS);
  // Expected not getting previously given amount of resources as they are not available at the
  // moment, but still get as much as possible
  eS_concurrency = num_oversubscribed_resources - eA_concurrency;
  eS.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit (rigid concurrency)") &&
        check_permit(eS, phS) && check_permit(eA, phA) &&
        check(!is_callback_invoked, "Activated rigid concurrency permit was not negotiated.")))
  {
    return test_fail(test_name);
  }

  r = tcmReleasePermit(phA);
  if (!(check_success(r, "tcmReleasePermit (regular)") && check_permit(eS, phS) &&
        check(!is_callback_invoked, "Rigid concurrency permit in active state was not negotiated.")))
    return test_fail(test_name);

  r = tcmReleasePermit(phS);
  if (!check_success(r, "tcmReleasePermit (rigid concurrency)"))
    return test_fail(test_name);
  phS = nullptr;                // avoid side effects in other tests

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_no_new_resources_for_rigid_concurrency() {
  // Test check that the amount of resources once given to a rigid concurrency permit does not
  // change when this permit re-activates after its renegotiation while being in IDLE state (because
  // of a separate demand for its resources) that effectively deactivated it.

  const char* test_name = "test_no_new_resources_for_rigid_concurrency";
  test_prolog(test_name);

  tcm_client_id_t clid{0};

  tcm_result_t r = tcmConnect(renegotiation_function, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(num_oversubscribed_resources);

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(1, (int32_t)eA_concurrency);

  r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit regular") && check_permit(eA, pA)))
    return test_fail(test_name);

  // Request that shouldn't be renegotiated in active state
  tcm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  tcm_permit_request_t rS = make_request(num_oversubscribed_resources/2, num_oversubscribed_resources,
                                         /*constraints*/nullptr, /*size*/0,
                                         TCM_REQUEST_PRIORITY_NORMAL, rigid_concurrency_flags);

  // Permit S will negotiate with the permit A since its minimum isn't satisfied.
  uint32_t pS_concurrency, eS_concurrency = num_oversubscribed_resources/2;

  tcm_permit_t pS = make_void_permit(&pS_concurrency);
  tcm_permit_t eS = make_active_permit(&eS_concurrency);
  eS.flags = rigid_concurrency_flags;

  is_callback_invoked = false;

  phS = nullptr;
  r = tcmRequestPermit(clid, rS, &phS, &phS, &pS);
  eA_concurrency = num_oversubscribed_resources - num_oversubscribed_resources / 2;
  if (!(check_success(r, "tcmRequestPermit (rigid concurrency)") &&
        check_permit(eA, phA) && check_permit(eS, pS) &&
        check(is_callback_invoked, "Renegotiation for the regular permit happens")))
  {
    return test_fail(test_name);
  }

  is_callback_invoked = false;
  allow_rigid_concurrency_permit_negotiation = true;
  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_INACTIVE; eS_concurrency = 0;
  eA_concurrency = num_oversubscribed_resources;
  if (!(check_success(r, "tcmIdlePermit (rigid concurrency)") &&
        check_permit(eS, phS) && check_permit(eA, phA) &&
        // TODO: check that callback has been invoked two times - one for regular permit and another
        // one - for rigid.
        check(is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
              "was negotiated since there are other requests for resources exist")))
  {
    return test_fail(test_name);
  }

  allow_rigid_concurrency_permit_negotiation = false;
  is_callback_invoked = false;

  r = tcmReleasePermit(phA);
  if (!(check_success(r, "tcmReleasePermit") && check_permit(eS, phS) &&
        check(!is_callback_invoked, "INACTIVE rigid concurrency permit was not negotiated.")))
    return test_fail(test_name);

  r = tcmActivatePermit(phS);
  eS_concurrency = num_oversubscribed_resources;
  eS.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit (rigid concurrency)") && check_permit(eS, phS) &&
      check(!is_callback_invoked, "Callback was not invoked for the rigid concurrency permit that "
            "was activated")))
  {
    return test_fail(test_name);
  }

  r = tcmReleasePermit(phS);
  if (!check_success(r, "tcmReleasePermit (rigid concurrency)"))
    return test_fail(test_name);
  phS = nullptr;                // avoid side effects in other tests

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_renegotiation_order() {
  // Test checks that satisfying a permit request searches for resources in specific order:
  // 1) Available resources
  // 2) Negotiation of the IDLE permits (including rigid concurrency ones)
  // 3) Negotiation of the ACTIVE permits

  const char* test_name = "test_renegotiation_order";
  test_prolog(test_name);

  tcm_client_id_t clid{0};

  tcm_result_t r = tcmConnect(renegotiation_function, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(num_oversubscribed_resources/2);

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(1, (int32_t)eA_concurrency);

  r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit regular " + std::to_string(eA_concurrency)) &&
        check_permit(eA, pA))) {
    return test_fail(test_name);
  }

  // Request that shouldn't be renegotiated in active state
  tcm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  uint32_t pS_concurrency = 0, eS_concurrency = num_oversubscribed_resources/4;

  tcm_permit_request_t rS = make_request(tcm_automatic, int(eS_concurrency), /*constraints*/nullptr,
                                         /*size*/0, TCM_REQUEST_PRIORITY_NORMAL,
                                         rigid_concurrency_flags);

  tcm_permit_t pS = make_void_permit(&pS_concurrency);
  tcm_permit_t eS = make_active_permit(&eS_concurrency);
  eS.flags = rigid_concurrency_flags;
  is_callback_invoked = false;

  phS = nullptr;
  r = tcmRequestPermit(clid, rS, &phS, &phS, &pS);
  if (!(check_success(r, "tcmRequestPermit (rigid concurrency) pS " +
                      std::to_string(eS_concurrency)) &&
        check_permit(eA, phA) && check_permit(eS, pS) &&
        check(!is_callback_invoked, "Renegotiation for the regular permit did not happen")))
  {
    return test_fail(test_name);
  }

  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_IDLE;
  // TODO: print the request parameters in the log
  if (!(check_success(r, "tcmIdlePermit (rigid concurrency)") &&
        // TODO: indent corresponding permit checking messages in the log
        check_permit(eS, phS) && check_permit(eA, phA) &&
        check(!is_callback_invoked, "Rigid concurrency permit that switched to the idle state "
              "was not negotiated since no other requests for resources exist")))
  {
    return test_fail(test_name);
  }

  tcm_permit_handle_t phC{nullptr};
  uint32_t pC_concurrency = 0, eC_concurrency = num_oversubscribed_resources/4;

  tcm_permit_t pC = make_void_permit(&pC_concurrency);
  tcm_permit_t eC = make_active_permit(&eC_concurrency);

  tcm_permit_request_t rC = make_request(/*min_sw_threads*/1, (int32_t)eC_concurrency);
  r = tcmRequestPermit(clid, rC, &phC, &phC, &pC);
  if (!(check_success(r, "tcmRequestPermit regular (C) " + std::to_string(eC_concurrency)) &&
        check_permit(eC, pC) && check_permit(eS, phS) && check_permit(eA, phA) &&
        check(!is_callback_invoked, "No negotiations happen")))
  {
      return test_fail(test_name);
  }

  r = tcmReleasePermit(phC);
  if (!(check_success(r, "tcmReleasePermit (C)") && check_permit(eS, phS) && check_permit(eA, phA) &&
        check(!is_callback_invoked, "Rigid concurrency permit in idle state was not negotiated.")))
  {
      return test_fail(test_name);
  }

  eC_concurrency = num_oversubscribed_resources / 2;
  rC.max_sw_threads = int32_t(eC_concurrency);
  phC = nullptr;                // make it a new request
  // The request going to negotiate IDLEd rigid concurrency permit
  allow_rigid_concurrency_permit_negotiation = true;
  r = tcmRequestPermit(clid, rC, &phC, &phC, &pC);
  eA.state = TCM_PERMIT_STATE_ACTIVE;
  eS.state = TCM_PERMIT_STATE_INACTIVE; eS_concurrency = 0;
  if (!(check_success(r, "tcmRequestPermit regular (C) " + std::to_string(rC.max_sw_threads)) &&
        check_permit(eC, pC) && check_permit(eA, phA) && check_permit(eS, phS) &&
        check(is_callback_invoked, "Rigid concurrency permit in idle state was negotiated.")))
  {
      return test_fail(test_name);
  }
  allow_rigid_concurrency_permit_negotiation = false;

  // TODO: Request amount of resources independent of floating point arithmetic
  eC_concurrency = 3 * num_oversubscribed_resources / 4;
  rC.min_sw_threads = rC.max_sw_threads = eC_concurrency;
  eA_concurrency = num_oversubscribed_resources - eC_concurrency;
  is_callback_invoked = false;
  r = tcmRequestPermit(clid, rC, &phC, &phC, &pC);
  if (!(check_success(r, "tcmRequestPermit phC regular, re-request for " +
                      std::to_string(eC_concurrency)) &&
        check_permit(eC, pC) && check_permit(eA, phA) && check_permit(eS, phS) &&
        check(is_callback_invoked, "ACTIVE permit was negotiated.")))
  {
      return test_fail(test_name);
  }

  eA_concurrency = rA.max_sw_threads;
  is_callback_invoked = false;
  r = tcmReleasePermit(phC);
  if (!(check_success(r, "tcmReleasePermit phC") && check_permits({{eA, phA}, {eS, phS}}) &&
        check(is_callback_invoked, "ACTIVE permit phA has been negotiated")))
  {
      return test_fail(test_name);
  }

  is_callback_invoked = false;
  r = tcmReleasePermit(phA);
  if (!(check_success(r, "tcmReleasePermit") && check_permit(eS, phS) &&
        check(!is_callback_invoked, "Rigid concurrency IDLE permit phS has NOT been negotiated")))
  {
      return test_fail(test_name);
  }

  r = tcmReleasePermit(phS);
  if (!(check_success(r, "tcmReleasePermit (rigid concurrency)") &&
        check(!is_callback_invoked, "Negotiation did not happen since there are no more permits")))
  {
      return test_fail(test_name);
  }

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_take_from_idle_when_required_is_satisfied() {
    // The test checks that idle resources are used to satisfy desired number of requested resources
    const char* test_name = __func__;
    test_prolog(test_name);

    bool test_succeeded = false;
    try {
        tcm_client_id_t client = connect_new_client();
        {
            auto rigid_request = make_request(0, num_oversubscribed_resources);
            rigid_request.flags.rigid_concurrency = true;
            auto rigid_ph = request_permit(client, rigid_request, /*callback_arg*/nullptr);
            auto rigid_permit_actual_data = get_permit_data<>(rigid_ph);
            auto expected_rigid_permit = make_active_permit(
                /*expected resources*/num_oversubscribed_resources, /*cpu_masks*/nullptr,
                rigid_request.flags
            );
            test_succeeded = check_permit(expected_rigid_permit, rigid_permit_actual_data);

            idle_permit(rigid_ph);

            auto request = make_request(0, num_oversubscribed_resources);
            auto ph = request_permit(client, request, /*callback*/nullptr);
            auto permit_actual_data = get_permit_data<>(ph);
            auto expected_data = make_active_permit(
                /*expected resources*/num_oversubscribed_resources
            );
            auto new_expected_rigid_permit = make_inactive_permit(/*cpu_masks*/nullptr, rigid_request.flags);
            auto new_rigid_permit_data = get_permit_data<>(rigid_ph);
            test_succeeded = check_permit(expected_data, permit_actual_data) &&
                check_permit(new_expected_rigid_permit, new_rigid_permit_data);

            // TODO: utilize RAII for release and disconnect
            release_permit(ph, "Failed to release regular permit handle");
            release_permit(rigid_ph, "Failed to release rigid concurrency permit handle");
        }
        disconnect_client(client);
    } catch (const tcm_exception& e) {
        test_succeeded = check(false, "Exception thrown: ", e.what());
    }

    return test_stop(test_succeeded, test_name);
}

bool test_thread_registration() {
  const char* test_name = "test_thread_registration";
  test_prolog(test_name);

  tcm_client_id_t clid;
  tcm_result_t r = tcmConnect(nullptr, &clid);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  tcm_permit_handle_t ph{nullptr};
  uint32_t p_concurrency;
  tcm_permit_t p = make_void_permit(&p_concurrency);
  uint32_t e_concurrency = num_oversubscribed_resources;
  tcm_permit_t e = make_active_permit(&e_concurrency);

  r = tcmRegisterThread(ph);
  if (!(check(r == TCM_RESULT_ERROR_UNKNOWN, "tcmRegisterThread for empty permit handle")))
    return test_fail(test_name);

  tcm_permit_request_t req = make_request(0, num_oversubscribed_resources);
  r = tcmRequestPermit(clid, req, nullptr, &ph, &p);
  if (!(check_success(r, "tcmRequestPermit") && check_permit(e, p)))
    return test_fail(test_name);

  r = tcmRegisterThread(ph);
  if (!(check_success(r, "tcmRegisterThread")))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread")))
    return test_fail(test_name);

  r = tcmReleasePermit(ph);
  if (!check_success(r, "tcmReleasePermit"))
    return test_fail(test_name);

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  std::cout << "test_registration done" << std::endl;
  return test_epilog(test_name);
}

bool test_default_constraints_construction() {
  const char* test_name = "test_default_constraints_construction";
  test_prolog(test_name);

  tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
  bool res = true;
  res &= check(constraints.min_concurrency == tcm_automatic, "Check default min_concurrency value");
  res &= check(constraints.max_concurrency == tcm_automatic, "Check default max_concurrency value");
  res &= check(constraints.mask == nullptr, "Check default mask value");
  res &= check(constraints.numa_id == tcm_automatic, "Check default numa_id value");
  res &= check(constraints.core_type_id == tcm_automatic, "Check default core_type_id value");
  res &= check(constraints.threads_per_core == tcm_automatic, "Check default threads_per_core value");
  return test_stop(res, test_name);
}

bool test_request_initializer() {
  const char* test_name = "test_request_initializer";
  test_prolog(test_name);

  tcm_permit_request_t request = TCM_PERMIT_REQUEST_INITIALIZER;
  bool res = true;
  res &= check(request.min_sw_threads == tcm_automatic, "Check default min_sw_threads value");
  res &= check(request.max_sw_threads == tcm_automatic, "Check default max_sw_threads value");
  res &= check(request.cpu_constraints == nullptr, "Check default cpu_constraints value");
  res &= check(request.constraints_size == 0, "Check default constraints_size value");

  res &= check(!request.flags.stale,
    "Check default permit flags - stale flag");
  res &= check(!request.flags.rigid_concurrency,
    "Check default permit flags - rigid_concurrency flag");
  res &= check(!request.flags.exclusive,
    "Check default permit flags - exluisive flag");
  res &= check(!request.flags.reserved,
    "Check default permit flags - reserved field");

  std::size_t count = 0;
  for (const auto& el: request.reserved) {
    res &= check(el == 0, "Check request reserved fields value");
    ++count;
  }
  res &= (count == 4);
  return test_stop(res, test_name);
}

bool test_get_stale_permit() {
  const char* test_name = "test_get_stale_permit";
  test_prolog(test_name);

  // TODO: implement the test

  return test_epilog(test_name);
}

static_assert(sizeof(tcm_permit_flags_t) == 4, "The permit flags type has wrong size");
static_assert(sizeof(tcm_callback_flags_t) == 4, "The callback flags type has wrong size");

bool test_allow_not_specifying_client_callback() {
  const char* test_name = "test_allow_not_specifying_client_callback";
  test_prolog(test_name);

  tcm_client_id_t client_id;

  auto r = tcmConnect(/*callback*/nullptr, &client_id);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  const int num_requests = 2;
  std::vector<tcm_permit_handle_t> handles(num_requests, nullptr);
  std::vector<tcm_permit_t> permits(2 * num_requests); // actual + expected
  std::vector<uint32_t> permit_concurrencies = {0, 0, uint32_t(num_oversubscribed_resources), 0};
  for (auto i = 0; i < num_requests; ++i) {
    tcm_permit_request_t req = TCM_PERMIT_REQUEST_INITIALIZER;
    req.min_sw_threads = req.max_sw_threads = num_oversubscribed_resources;
    permits[i] = make_void_permit(&permit_concurrencies[i]);
    r = tcmRequestPermit(client_id, req, /*callback_arg*/nullptr, &handles[i], &permits[i]);
    if (!check_success(r, "tcmRequestPermit " + std::to_string(i)))
      return test_fail(test_name);
  }

  permits[num_requests] = make_active_permit(&permit_concurrencies[num_requests]);
  permits[num_requests+1] = make_pending_permit(&permit_concurrencies[num_requests+1]);

  // Checking permits
  for (auto i = 0; i < num_requests; ++i) {
    auto& expected = permits[num_requests+i];
    if (!check_permit(expected, permits[i]))
      return test_fail(test_name);
  }

  // Releasing permits, entailing negotiation
  r = tcmReleasePermit(handles[0]);
  if (!check_success(r, "tcmReleasePermit (active permit)"))
    return test_fail(test_name);

  permit_concurrencies[num_requests+1] = uint32_t(num_oversubscribed_resources);
  permits[num_requests+1] = make_active_permit(&permit_concurrencies[num_requests+1]);
  if (!check_permit(permits[num_requests+1], handles[1]))
    return test_fail(test_name);

  r = tcmReleasePermit(handles[1]);
  if (!check_success(r, "tcmReleasePermit (last permit)"))
    return test_fail(test_name);

  r = tcmDisconnect(client_id);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_requesting_zero_resources() {
  // The test checks that it is okay to ask for zero resources in order to get
  // the permit handle initialized by the TCM. This is useful when client wants
  // to have actual permit handle value to make further, perhaps, concurrent
  // request updates on it.

  const char* test_name = "test_requesting_zero_resources";
  test_prolog(test_name);

  tcm_client_id_t client_id;

  tcm_result_t r = tcmConnect(nullptr, &client_id);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  int min_sw_threads = 0, max_sw_threads = 0;
  uint32_t p_concurrency = 0; tcm_permit_t p = make_void_permit(&p_concurrency);
  uint32_t e_concurrency = max_sw_threads; tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_permit_request_t req = make_request(min_sw_threads, max_sw_threads);
  tcm_permit_handle_t ph{nullptr};
  r = tcmRequestPermit(client_id, req, nullptr, &ph, &p);
  if (!(check_success(r, "tcmRequestPermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = tcmReleasePermit(ph);
  if (!check_success(r, "tcmReleasePermit"))
    return test_fail(test_name);

  r = tcmDisconnect(client_id);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

  return test_epilog(test_name);
}

bool test_request_initialized_by_default() {
  const char* test_name = "test_request_initialized_by_default";
  test_prolog(test_name);

  bool test_succeeded = false;
  try {
      tcm_client_id_t client = connect_new_client();
      {
          auto req = make_request(); // default request without constraints
          auto ph = request_permit(client, req, /*callback_arg*/nullptr);
          auto actual_permit = get_permit_data<>(ph);
          auto expected_permit = make_active_permit(/*expected_concurrency*/num_total_resources);
          test_succeeded = check_permit(expected_permit, actual_permit);
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
          test_succeeded = check_permit(expected_permit, actual_permit, skip_concurrency_check);

          // Check that the permit was given some concurrency
          test_succeeded &= check(tcm_permit_t(actual_permit).concurrencies[0] > 0,
                                  "Actual concurrency is not what is expected");

          // TODO: utilize RAII for release and disconnect
          release_permit(ph, "Failed to release permit handle");
      }

      disconnect_client(client);
  } catch (const tcm_exception& e) {
      test_succeeded = check(false, "Exception thrown: ", e.what());
  }

  return test_stop(test_succeeded, test_name);
}


bool test_incorrect_requests() {
  const char* test_name = "test_incorrect_requests";
  test_prolog(test_name);

  tcm_client_id_t client = connect_new_client();

  { // default request with default constraints
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    auto req = make_request(tcm_automatic, tcm_automatic, &constraints, /*size*/1);
    tcm_permit_handle_t ph{nullptr};
    auto r = tcmRequestPermit(client, req, /*callback_arg*/nullptr, &ph, /*permit*/nullptr);
    if (!check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT,
               "Default request with default constraints returned invalid argument status"))
    {
      return test_fail(test_name);
    }
  }

  { // meaningful request with non-meaningful constraints
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    auto req = make_request(tcm_automatic, num_total_resources, &constraints, /*size*/1);
    tcm_permit_handle_t ph{nullptr};
    auto r = tcmRequestPermit(client, req, /*callback_arg*/nullptr, &ph, /*permit*/nullptr);
    if (!check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT,
               "Request with non-meaningful constraints returned invalid argument status"))
    {
      return test_fail(test_name);
    }
  }

  { // request with incorrect constraints size
    auto req = make_request(tcm_automatic, tcm_automatic, /*constraints*/nullptr, /*size*/0);
    std::uintptr_t dummy_constraints = 0xABCDEFED;
    req.cpu_constraints = (tcm_cpu_constraints_t*)dummy_constraints;
    tcm_permit_handle_t ph{nullptr};
    auto r = tcmRequestPermit(client, req, /*callback_arg*/nullptr, &ph, /*permit*/nullptr);
    if (!check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT,
               "Request with constraints but zero size returned invalid argument status"))
    {
      return test_fail(test_name);
    }
  }

  { // re-request with incorrect size
    auto req = make_request();
    auto ph = request_permit(client, req, /*callback_arg*/nullptr);
    req.constraints_size = 1;             // new size for constraints
    auto r = tcmRequestPermit(client, req, /*callback_arg*/nullptr, &ph, /*permit*/nullptr);
    if (!check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT,
               "Re-request with incorrect constraints size returned invalid argument status")) {
      return test_fail(test_name);
    }
    // TODO: utilize RAII for release and disconnect
    release_permit(ph, "Failed to release permit handle");
  }

  disconnect_client(client);

  // TODO: to complete the tests below

  // tcm_permit_request_t reqA =
  //   make_request(2 * num_oversubscribed_resources, 2 * num_oversubscribed_resources);
  // r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  // if (!(check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT && !phA, "tcmRequestPermit for A")
  //       && check_permit(eA, pA)))
  //   return test_fail(test_name);

  return test_epilog(test_name);
}

int main() {
  bool res = true;

  res &= test_state_functions();
  res &= test_pending_state();
  res &= test_activate_pending_when_one_deactivates();
  res &= test_no_negotiation_for_active_rigid_concurrency();
  res &= test_no_new_resources_for_rigid_concurrency();
  res &= test_renegotiation_order();
  res &= test_take_from_idle_when_required_is_satisfied();
  res &= test_thread_registration();
  res &= test_default_constraints_construction();
  res &= test_request_initializer();
  res &= test_get_stale_permit();

  res &= test_allow_not_specifying_client_callback();
  res &= test_requesting_zero_resources();
  res &= test_request_initialized_by_default();

  res &= test_incorrect_requests();

  return int(!res);
}
