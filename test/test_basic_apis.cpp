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
  const char* test_name = "test_state_functions";
  test_prolog(test_name);

  zerm_client_id_t clid;
  ze_result_t r = zermConnect(nullptr, &clid);
  if (!check_success(r, "zermConnect"))
    return test_fail(test_name);

  zerm_permit_handle_t ph{nullptr};
  uint32_t p_concurrency;
  zerm_permit_t p = make_void_permit(&p_concurrency);
  uint32_t e_concurrency = total_number_of_threads;
  zerm_permit_t e = make_active_permit(&e_concurrency);

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clid, req, nullptr, &ph, &p);
  if (!(check_success(r, "zermRequestPermit") && check_permit(e, p)))
    return test_fail(test_name);

  r = zermIdlePermit(ph);
  e.state = ZERM_PERMIT_STATE_IDLE;
  if (!(check_success(r, "zermIdlePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = zermActivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "zermActivatePermit 1") && check_permit(e, ph)))
    return test_fail(test_name);

  r = zermDeactivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "zermDeactivatePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = zermActivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "zermActivatePermit 2") && check_permit(e, ph)))
    return test_fail(test_name);

  r = zermIdlePermit(ph);
  e.state = ZERM_PERMIT_STATE_IDLE;
  if (!(check_success(r, "zermIdlePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = zermDeactivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "zermDeactivatePermit") && check_permit(e, ph)))
    return test_fail(test_name);

  r = zermActivatePermit(ph);
  e.state = ZERM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "zermActivatePermit 3") && check_permit(e, ph)))
    return test_fail(test_name);

  r = zermReleasePermit(ph);
  if (!check_success(r, "zermReleasePermit"))
    return test_fail(test_name);

  r = zermDisconnect(clid);
  if (!check_success(r, "zermDisconnect"))
    return test_fail(test_name);

  std::cout << "test_state_functions done" << std::endl;
  return test_epilog(test_name);
}

bool test_pending_state() {
  const char* test_name = "test_pending_state";
  test_prolog(test_name);

  zerm_client_id_t clid;
  ze_result_t r = zermConnect(nullptr, &clid);
  if (!check_success(r, "zermConnect"))
    return test_fail(test_name);

  zerm_permit_handle_t phA{nullptr}, phB{nullptr};
  uint32_t pA_concurrency{0}, pB_concurrency{0};
  uint32_t eA_concurrency{0}, eB_concurrency{0};
  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency);
  zerm_permit_t eA = make_void_permit(&eA_concurrency);
  zerm_permit_t eB = make_pending_permit(&eB_concurrency);

  zerm_permit_request_t reqA =
    make_request(2 * total_number_of_threads, 2 * total_number_of_threads);
  r = zermRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check(r == ZE_RESULT_ERROR_INVALID_ARGUMENT && !phA, "zermRequestPermit for A")
        && check_permit(eA, pA)))
    return test_fail(test_name);

  eA_concurrency = total_number_of_threads;
  eA.state = ZERM_PERMIT_STATE_ACTIVE;
  eA.flags.rigid_concurrency = true;
  reqA = make_request(0, total_number_of_threads);
  reqA.flags.rigid_concurrency = true;
  r = zermRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit for A (re-requesting 1)")
        && check_permit(eA, pA)))
    return test_fail(test_name);

  reqA = make_request(2 * total_number_of_threads, 2 * total_number_of_threads);
  r = zermRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check(r == ZE_RESULT_ERROR_INVALID_ARGUMENT, "zermRequestPermit for A (re-requesting 2)")
        && check_permit(eA, phA)))
    return test_fail(test_name);

  zerm_permit_request_t reqB = make_request(total_number_of_threads, total_number_of_threads);
  r = zermRequestPermit(clid, reqB, nullptr, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit for B") && check_permit(eB, pB)))
    return test_fail(test_name);

  r = zermReleasePermit(phB);
  if (!check_success(r, "zermReleasePermit for B"))
    return test_fail(test_name);

  r = zermReleasePermit(phA);
  if (!check_success(r, "zermReleasePermit for A"))
    return test_fail(test_name);

  r = zermDisconnect(clid);
  if (!check_success(r, "zermDisconnect"))
    return test_fail(test_name);

  std::cout << "test_pending_state done" << std::endl;
  return test_epilog(test_name);
}

bool test_thread_registration() {
  const char* test_name = "test_thread_registration";
  test_prolog(test_name);

  zerm_client_id_t clid;
  ze_result_t r = zermConnect(nullptr, &clid);
  if (!check_success(r, "zermConnect"))
    return test_fail(test_name);

  zerm_permit_handle_t ph{nullptr};
  uint32_t p_concurrency;
  zerm_permit_t p = make_void_permit(&p_concurrency);
  uint32_t e_concurrency = total_number_of_threads;
  zerm_permit_t e = make_active_permit(&e_concurrency);

  r = zermRegisterThread(ph);
  if (!(check(r == ZE_RESULT_ERROR_UNKNOWN, "zermRegisterThread for empty permit handle")))
    return test_fail(test_name);

  zerm_permit_request_t req = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clid, req, nullptr, &ph, &p);
  if (!(check_success(r, "zermRequestPermit") && check_permit(e, p)))
    return test_fail(test_name);

  r = zermRegisterThread(ph);
  if (!(check_success(r, "zermRegisterThread")))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread")))
    return test_fail(test_name);

  r = zermReleasePermit(ph);
  if (!check_success(r, "zermReleasePermit"))
    return test_fail(test_name);

  r = zermDisconnect(clid);
  if (!check_success(r, "zermDisconnect"))
    return test_fail(test_name);

  std::cout << "test_registration done" << std::endl;
  return test_epilog(test_name);
}

bool test_default_constraints_construction() {
  const char* test_name = "test_default_constraints_construction";
  test_prolog(test_name);

  zerm_cpu_constraints_t constraints = ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
  bool res = true;
  res &= check(constraints.min_concurrency == zerm_automatic, "Check default min_concurrency value");
  res &= check(constraints.max_concurrency == zerm_automatic, "Check default max_concurrency value");
  res &= check(constraints.mask == nullptr, "Check default mask value");
  res &= check(constraints.numa_id == zerm_automatic, "Check default numa_id value");
  res &= check(constraints.core_type_id == zerm_automatic, "Check default core_type_id value");
  res &= check(constraints.threads_per_core == zerm_automatic, "Check default threads_per_core value");
  return test_stop(res, test_name);
}

bool test_request_initializer() {
  const char* test_name = "test_request_initializer";
  test_prolog(test_name);

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
  return test_stop(res, test_name);
}

bool test_get_stale_permit() {
  const char* test_name = "test_get_stale_permit";
  test_prolog(test_name);

  // TODO: implement the test

  return test_epilog(test_name);
}

static_assert(sizeof(zerm_permit_flags_t) == 4, "The permit flags type has wrong size");
static_assert(sizeof(zerm_callback_flags_t) == 4, "The callback flags type has wrong size");

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
