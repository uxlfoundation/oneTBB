/*
 *
 * Copyright (C) 2021-2023 Intel Corporation
 *
 */

#include "test_utils.h"

#include "tcm.h"

#include <iostream>
#include <string>

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
  uint32_t e_concurrency = total_number_of_threads;
  tcm_permit_t e = make_active_permit(&e_concurrency);

  tcm_permit_request_t req = make_request(0, total_number_of_threads);
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
  e_concurrency = total_number_of_threads;
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
  e_concurrency = total_number_of_threads;
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
  tcm_permit_t eA = make_void_permit(&eA_concurrency),
                eB = make_pending_permit(&eB_concurrency);

  tcm_permit_request_t reqA =
    make_request(2 * total_number_of_threads, 2 * total_number_of_threads);
  r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT && !phA, "tcmRequestPermit for A")
        && check_permit(eA, pA)))
    return test_fail(test_name);

  eA_concurrency = total_number_of_threads;
  eA.state = TCM_PERMIT_STATE_ACTIVE;
  eA.flags.rigid_concurrency = true;
  reqA = make_request(0, total_number_of_threads);
  reqA.flags.rigid_concurrency = true;
  r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit for A (re-requesting 1)")
        && check_permit(eA, pA)))
    return test_fail(test_name);

  reqA = make_request(2 * total_number_of_threads, 2 * total_number_of_threads);
  r = tcmRequestPermit(clid, reqA, nullptr, &phA, &pA);
  if (!(check(r == TCM_RESULT_ERROR_INVALID_ARGUMENT, "tcmRequestPermit for A (re-requesting 2)")
        && check_permit(eA, phA)))
    return test_fail(test_name);

  tcm_permit_request_t reqB = make_request(total_number_of_threads, total_number_of_threads);
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

  std::cout << "test_pending_state done" << std::endl;
  return test_epilog(test_name);
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
  uint32_t e_concurrency = total_number_of_threads;
  tcm_permit_t e = make_active_permit(&e_concurrency);

  r = tcmRegisterThread(ph);
  if (!(check(r == TCM_RESULT_ERROR_UNKNOWN, "tcmRegisterThread for empty permit handle")))
    return test_fail(test_name);

  tcm_permit_request_t req = make_request(0, total_number_of_threads);
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

  tcm_client_id_t client_id;

  auto r = tcmConnect(/*callback*/nullptr, &client_id);
  if (!check_success(r, "tcmConnect"))
    return test_fail(test_name);

  const int num_requests = 2;
  std::vector<tcm_permit_handle_t> handles(num_requests, nullptr);
  std::vector<tcm_permit_t> permits(2 * num_requests); // actual + expected
  std::vector<uint32_t> permit_concurrencies = {0, 0, uint32_t(total_number_of_threads), 0};
  for (auto i = 0; i < num_requests; ++i) {
    tcm_permit_request_t req = TCM_PERMIT_REQUEST_INITIALIZER;
    req.min_sw_threads = req.max_sw_threads = total_number_of_threads;
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

  permit_concurrencies[num_requests+1] = uint32_t(total_number_of_threads);
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

int main() {
  bool res = true;

  res &= test_state_functions();
  res &= test_pending_state();
  res &= test_thread_registration();
  res &= test_get_stale_permit();
  res &= test_default_constraints_construction();
  res &= test_request_initializer();
  res &= test_allow_not_specifying_client_callback();
  res &= test_requesting_zero_resources();

  return int(!res);
}
