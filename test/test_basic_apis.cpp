/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#include "test_utils.h"

#include "tcm.h"

#include <iostream>
#include <string>

bool test_state_functions() {
  check(true, "\n\nbegin test_state_functions");
  zerm_client_id_t clid;

  ze_result_t r = zermConnect(nullptr, &clid);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect"))
    return check(false, "end test_state_functions");

  zerm_permit_handle_t ph{nullptr};
  uint32_t p_concurrency;
  zerm_permit_t p{&p_concurrency, nullptr};
  uint32_t e_concurrency = total_number_of_threads;
  zerm_permit_t e = {
    &e_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0
  };

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clid, req, nullptr, &ph, &p);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit") && check_permit(e, p)))
    return check(false, "end test_state_functions");

  r = zermIdlePermit(ph);
  e.state = ZERM_PERMIT_STATE_IDLE;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermIdlePermit") && check_permit(e, ph)))
    return check(false, "end test_state_functions");

  r = zermActivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermActivatePermit 1") && check_permit(e, ph)))
    return check(false, "end test_state_functions");

  r = zermDeactivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_INACTIVE;
  if (!(check(r== ZE_RESULT_SUCCESS, "zermDeactivatePermit") && check_permit(e, ph)))
    return check(false, "end test_state_functions");

  r = zermActivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermActivatePermit 2") && check_permit(e, ph)))
    return check(false, "end test_state_functions");

  r = zermIdlePermit(ph);
  e.state = ZERM_PERMIT_STATE_IDLE;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermIdlePermit") && check_permit(e, ph)))
    return check(false, "end test_state_functions");

  r = zermDeactivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_INACTIVE;
  if (!(check(r== ZE_RESULT_SUCCESS, "zermDeactivatePermit") && check_permit(e, ph)))
    return check(false, "end test_state_functions");

  r = zermActivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check(r == ZE_RESULT_SUCCESS, "zermActivatePermit 3") && check_permit(e, ph)))
    return check(false, "end test_state_functions");

  r = zermReleasePermit(ph);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit"))
    return check(false, "end test_state_functions");

  r = zermDisconnect(clid);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect"))
    return check(false, "end test_state_functions");

  std::cout << "test_state_functions done" << std::endl;
  return check(true, "end test_state_functions");
}

bool test_pending_state() {
  check(true, "\n\nbegin test_pending_state");
  zerm_client_id_t clid;

  ze_result_t r = zermConnect(nullptr, &clid);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect"))
    return check(false, "end test_pending_state");

  zerm_permit_handle_t phA{nullptr}, phB{nullptr};
  uint32_t pA_concurrency{0}, pB_concurrency{0};
  uint32_t eA_concurrency{0}, eB_concurrency{0};
  zerm_permit_t pA{&pA_concurrency, nullptr, 1}, pB{&pB_concurrency, nullptr, 1};
  zerm_permit_t eA = { &eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_VOID, 0 };
  zerm_permit_t eB = { &eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_PENDING, 0 };

  zerm_permit_request_t reqA =
    make_request(2 * total_number_of_threads, 2 * total_number_of_threads);
  r = zermRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check(r == ZE_RESULT_ERROR_INVALID_ARGUMENT && !phA, "zermRequestPermit for A")
        && check_permit(eA, pA)))
    return check(false, "end test_pending_state");

  eA_concurrency = total_number_of_threads;
  eA.state = ZERM_PERMIT_STATE_ACTIVE;
  eA.flags.rigid_concurrency = true;
  reqA = make_request(0, total_number_of_threads);
  reqA.flags.rigid_concurrency = true;
  r = zermRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for A (re-requesting 1)")
        && check_permit(eA, pA)))
    return check(false, "end test_pending_state");

  reqA = make_request(2 * total_number_of_threads, 2 * total_number_of_threads);
  r = zermRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check(r == ZE_RESULT_ERROR_INVALID_ARGUMENT, "zermRequestPermit for A (re-requesting 2)")
        && check_permit(eA, phA)))
    return check(false, "end test_pending_state");

  zerm_permit_request_t reqB = make_request(total_number_of_threads, total_number_of_threads);
  r = zermRequestPermit(clid, reqB, nullptr, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for B") && check_permit(eB, pB)))
    return check(false, "end test_pending_state");

  r = zermReleasePermit(phB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for B"))
    return check(false, "end test_pending_state");

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for A"))
    return check(false, "end test_pending_state");

  r = zermDisconnect(clid);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect"))
    return check(false, "end test_pending_state");

  std::cout << "test_pending_state done" << std::endl;
  return check(true, "end test_pending_state");
}

bool test_thread_registration() {
  check(true, "\n\nbegin test_registration");
  zerm_client_id_t clid;

  ze_result_t r = zermConnect(nullptr, &clid);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect"))
    return check(false, "end test_registration");

  zerm_permit_handle_t ph{nullptr};
  uint32_t p_concurrency;
  zerm_permit_t p{&p_concurrency, nullptr};
  uint32_t e_concurrency = total_number_of_threads;
  zerm_permit_t e = {&e_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  r = zermRegisterThread(ph);
  if (!(check(r == ZE_RESULT_ERROR_UNKNOWN, "zermRegisterThread for empty permit handle")))
    return check(false, "end test_registration");

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clid, req, nullptr, &ph, &p);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit") && check_permit(e, p)))
    return check(false, "end test_registration");

  r = zermRegisterThread(ph);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread")))
    return check(false, "end test_registration");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread")))
    return check(false, "end test_registration");

  r = zermReleasePermit(ph);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit"))
    return check(false, "end test_registration");

  r = zermDisconnect(clid);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect"))
    return check(false, "end test_registration");

  std::cout << "test_registration done" << std::endl;
  return check(true, "end test_registration");
}

bool test_default_constraints_construction() {
  check(true, "\n\nbegin test_default_constraints_construction");

  zerm_cpu_constraints_t constraints = ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
  bool res = true;
  res &= check(constraints.min_concurrency == zerm_automatic, "Check default min_concurrency value");
  res &= check(constraints.max_concurrency == zerm_automatic, "Check default max_concurrency value");
  res &= check(constraints.mask == nullptr, "Check default mask value");
  res &= check(constraints.numa_id == zerm_automatic, "Check default numa_id value");
  res &= check(constraints.core_type_id == zerm_automatic, "Check default core_type_id value");
  res &= check(constraints.threads_per_core == zerm_automatic, "Check default threads_per_core value");
  return check(res, "end test_default_constraints_construction");
}

bool test_request_initializer() {
  check(true, "\n\nbegin test_default_request_construction");

  zerm_permit_request_t request = ZERM_PERMIT_REQUEST_INITIALIZER;
  bool res = true;
  res &= check(request.min_sw_threads == zerm_automatic, "Check default min_sw_threads value");
  res &= check(request.max_sw_threads == zerm_automatic, "Check default max_sw_threads value");
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
  return check(res, "end test_default_request_construction");
}

bool test_get_stale_permit() {
  check(true, "\n\nbegin test_get_stale_permit");

  // TODO: implement the test

  return check(true, "end test_get_stale_permit");
}

static_assert(sizeof(zerm_permit_flags_t) == 4, "The permit flags type has wrong size");
static_assert(sizeof(zerm_callback_flags_t) == 4, "The permit flags type has wrong size");

int main() {
  bool res = true;

  res &= test_state_functions();
  res &= test_pending_state();
  res &= test_thread_registration();
  res &= test_get_stale_permit();
  res &= test_default_constraints_construction();
  res &= test_request_initializer();

  return int(!res);
}
