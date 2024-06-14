/*
    Copyright (C) 2023-2024 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "test_utils.h"
#include "concurrency_utils.h"

#include "tcm.h"

#include "common_tests.h"

#include <algorithm>
#include <vector>
#include <set>
#include <iostream>
#include <sstream>
#include <thread>
#include <string>

constexpr skip_checks_t skip_concurrenency_check = {
  /*size*/ false, /*concurrency*/true, /*state*/false, /*flags*/false, /*mask*/false
};

int32_t compute_concurrency(int32_t total_demand, uint32_t demand, int32_t& carry) {
  int32_t distributed_threads = std::min(total_demand, num_oversubscribed_resources);
  int32_t permit = 0;
  if (total_demand > 0)
  {
    int32_t numerator = demand * distributed_threads + carry;
    permit = numerator / total_demand;
    carry = numerator % total_demand;
  }

  return permit;
}

// Given a set of demands the function returns a set of permits (no order
// implied) that should be given by the Thread Composability Manager.
std::vector<uint32_t> compute_concurrencies(const std::vector<int32_t>& demands,
                                            int32_t available_threads = num_oversubscribed_resources)
{
  int32_t total_demand = 0;
  for(std::size_t i = 0; i < demands.size(); ++i) {
    total_demand += demands[i];
  }

  int32_t carry = 0;
  std::vector<uint32_t> concurrencies(demands.size(), 0);
  for (std::size_t i = 0; i < demands.size(); ++i) {
    int32_t concurrency = compute_concurrency(total_demand, demands[i], carry);
    concurrency = std::min(available_threads, concurrency);
    concurrencies[i] = concurrency;
    available_threads -= concurrency;
  }
  return concurrencies;
}

//! Matches concurrencies from the given permits with the expected concurrencies
//! computed from the passed demands. Allows not more than two mismatches that
//! results in redistribution of a rounding error that might happen inside the
//! Thread Composability Manager due to use of an integer division.
bool check_permits_concurrencies(const std::set<tcm_permit_handle_t>& phs,
                                 // TODO: accept corresponding requests
                                 const std::vector<int32_t>& demands)
{
  bool result = true;

  auto concurrencies = compute_concurrencies(demands);
  auto concurrencies_copy = concurrencies;
  std::vector<uint32_t> permits_concurrencies(phs.size());
  std::vector<tcm_permit_t> permits(phs.size());
  for (unsigned i = 0; i < phs.size(); ++i) {
    permits[i] = make_void_permit(&permits_concurrencies[i]);
  }

  int i = 0;
  std::vector<uint32_t> missing;

  for (const auto& ph : phs) {
    result &= check_success(tcmGetPermitData(ph, &permits[i]),
                            "Reading data from permit " + to_string(ph));

    auto pos = std::find(concurrencies.begin(), concurrencies.end(), permits[i].concurrencies[0]);

    if (pos != concurrencies.end())
      concurrencies.erase(pos);
    else
      missing.push_back(permits[i].concurrencies[0]);

    i++;
  }

  if (!concurrencies.empty()) {
    // TODO: consider having multiple pairs of single resource redisitribution
    result &= check(concurrencies.size() == 2 && missing.size() == 2,
                    "At most two concurrencies differ from the expected set.");
    if (result) {
      if (concurrencies[0] > concurrencies[1])
        std::swap(concurrencies[0], concurrencies[1]);
      if (missing[0] > missing[1])
        std::swap(missing[0], missing[1]);
      uint32_t d1 = std::max(concurrencies[0], missing[0]) - std::min(concurrencies[0], missing[0]);
      uint32_t d2 = std::max(concurrencies[1], missing[1]) - std::min(concurrencies[1], missing[1]);
      result &= check(d1 == 1 && d2 == 1,
                      "The unmatched concurrency of " + std::to_string(d1) +
                      " redistributed with concurrency of " + std::to_string(d2) +
                      " in another permit.");
    }
  }

  if (!result) {
    std::stringstream ss;
    ss << "Expected concurrencies: ";
    for (const auto& c : concurrencies_copy)
      ss << c << " ";
    ss << std::endl << "Actual concurrencies: ";
    for (const auto& p : permits)
      ss << p.concurrencies[0] << " ";
    ss << std::endl;
    check(false, "Found all concurrencies in concurrency set for given permits." + ss.str());
  }

  return result;
}

bool test_nested_clients() {
  const char* test_name = __func__;
  test_prolog(test_name);

  try {
    auto client_a = connect_new_client(client_renegotiate);
    auto client_b = connect_new_client(client_renegotiate);

    uint32_t eA_concurrency = num_oversubscribed_resources,
             eB_concurrency = 1;

    tcm_permit_t eA = make_active_permit(&eA_concurrency),
                 eB = make_active_permit(&eB_concurrency);

    for (int outer_num_thr = 1; outer_num_thr <= num_oversubscribed_resources; ++outer_num_thr) {
      tcm_permit_handle_t phA = nullptr;
      tcm_permit_request_t reqA = make_request(outer_num_thr, outer_num_thr);
      phA = request_permit(client_a, reqA, &phA);
      eA_concurrency = outer_num_thr;
      if (!check_permit(eA, phA)) {
        throw tcm_exception{nullptr};
      }

      register_thread(phA);
      uint32_t resources_left = num_oversubscribed_resources - outer_num_thr;
      for (uint32_t inner_thr = 1; inner_thr <= resources_left+1; ++inner_thr) {
        // TODO: Prohibit requests having zero min_sw_threads
        for (uint32_t min_inner_thr = 0; min_inner_thr <= inner_thr; ++min_inner_thr) {
          tcm_permit_handle_t phB = nullptr;
          tcm_permit_request_t reqB = make_request(min_inner_thr, inner_thr);
          phB = request_permit(client_b, reqB, &phB);
          eB_concurrency = inner_thr;
          if (!(check_permit(eB, phB) && check_permit(eA, phA))) {
            std::cout << min_inner_thr << " " << inner_thr << std::endl;
            auto outer_permit = get_permit_data(phA);
            auto inner_permit = get_permit_data(phB);
            check(false, "Outer permit concurrency: " + std::to_string(outer_permit.concurrency()) +
                         ", Inner permit concurrency: " + std::to_string(inner_permit.concurrency()));
            throw tcm_exception{nullptr};
          }

          register_thread(phB);
          renegotiating_permits = {};
          tcm_permit_handle_t phC = nullptr;
          bool expect_negotiation_in_release = false;
          if (min_inner_thr == resources_left + 1) { // All resources should have been taken
              uint32_t eC_concurrency = 0; tcm_permit_t eC = make_pending_permit(&eC_concurrency);
              // Requesting [2, 2] because of having one resource from outer permit
              phC = request_permit(client_a, make_request(/*min_sw_threads*/2, /*max_sw_threads*/2));
              if (!check_permit(eC, phC)) {
                  throw tcm_exception{"Found unallocated resources"};
              }
          } else {              // There are unoccupied resources still
              const uint32_t num_free = resources_left - (inner_thr - /*inherited*/1);
              uint32_t eC_concurrency = 0; tcm_permit_t eC = make_active_permit(&eC_concurrency);
              if (min_inner_thr == inner_thr) { // Cannot negotiate anything
                  eC_concurrency = num_free + /*inherited*/1;
                  phC = request_permit(client_a, make_request(/*min_sw_threads*/1,
                                                              num_oversubscribed_resources));
              } else {
                  expect_negotiation_in_release = true;
                  renegotiating_permits = {phB};
                  uint32_t num_negotiable = inner_thr - min_inner_thr;
                  if (min_inner_thr == 0)
                      num_negotiable -= 1; // Cannot negotiate inherited

                  eC_concurrency = num_free + num_negotiable + /*inherited*/1;
                  eB_concurrency = std::max(min_inner_thr, uint32_t(1));
                  phC = request_permit(client_a,
                                       make_request(/*min_sw_threads*/eC_concurrency,
                                                    /*max_sw_threads*/num_oversubscribed_resources));
              }
              if (!(check_permit(eC, phC) && check_permit(eB, phB) && check_permit(eA, phA))) {
                  throw tcm_exception{"Found unallocated resources"};
              }
          }
          unregister_thread();  // Unregister phB
          if (expect_negotiation_in_release)
              renegotiating_permits = {phB};
          release_permit(phC);
          release_permit(phB);
        }
      }
      unregister_thread();

      release_permit(phA);
    }
    disconnect_client(client_a);
    disconnect_client(client_b);
  } catch (tcm_exception&) {
    return test_fail(test_name);
  }

  return test_epilog(test_name);
}

bool test_nested_clients_renegotiation() {
  const char* test_name = __func__;
  test_prolog(test_name);

  renegotiating_permits = {};    // no renegotiation is expected
  tcm_client_id_t clidA,  clidB;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = num_oversubscribed_resources;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(0, num_oversubscribed_resources);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A") && check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmRegisterThread(phA);
  if (!(check_success(r, "tcmRegisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {phA};

  uint32_t eB_concurrency = 0;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(num_oversubscribed_resources/2, num_oversubscribed_resources);

  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  eA_concurrency = num_oversubscribed_resources - num_oversubscribed_resources/2 + /*nested*/ 1;
  eB_concurrency = num_oversubscribed_resources/2;
  auto unchanged_permits = list_unchanged_permits({{phA, &pA}});

  if (!(check_success(r, "tcmRequestPermit B") &&
        check_permit(eB, pB) && check_permit(eA, phA) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = tcmRegisterThread(phB);
  if (!(check_success(r, "tcmRegisterThread B") &&
        check_permit(eB, pB) && check_permit(eA, phA)))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread B") &&
        check_permit(eB, pB) && check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {phA};
  eA.concurrencies[0] = num_oversubscribed_resources;

  r = tcmReleasePermit(phB);
  if (!(check_success(r, "tcmReleasePermit B") &&
        check_permit(eA, phA) && renegotiating_permits.size() == 0))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread A") &&
        check_permit(eA, phA)))
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

  return test_epilog(test_name);
}

bool test_nested_clients_constrained() {
  const char* test_name = __func__;
  test_prolog(test_name);

  constexpr int num_threads_per_constraint = 2;
  constexpr int num_constraints = 2;
  if (num_oversubscribed_resources < num_threads_per_constraint*num_constraints) {
    return test_epilog(test_name);
  }

  try {
    tcm_test::system_topology::construct();

    auto client_a = connect_new_client(client_renegotiate);
    auto client_b = connect_new_client(client_renegotiate);
    tcm_permit_handle_t phA = nullptr, phB = nullptr;

    tcm_cpu_mask_t* cpu_masks = new tcm_cpu_mask_t[num_constraints];
    cpu_masks[0] = extract_n_bits_from_process_mask(num_threads_per_constraint);
    for (int i = 1; i < num_constraints; ++i) {
      cpu_masks[i] = extract_n_bits_from_process_mask(num_threads_per_constraint, cpu_masks[i - 1]);
    }
    std::unique_ptr<tcm_cpu_mask_t[], masks_guard_t> mask_guard{cpu_masks, num_constraints};

    uint32_t eA_concurrency[num_constraints];
    uint32_t eB_concurrency[num_constraints];
    tcm_permit_t eA = make_active_permit(eA_concurrency, cpu_masks, num_constraints),
                 eB = make_active_permit(eB_concurrency, cpu_masks, num_constraints);

    tcm_cpu_constraints_t cpu_constraintsA[num_constraints];
    tcm_cpu_constraints_t cpu_constraintsB[num_constraints];
    for (int i = 0; i < num_constraints-1; ++i) {
      cpu_constraintsA[i] = {num_threads_per_constraint-1, num_threads_per_constraint-1, cpu_masks[i], tcm_automatic, tcm_automatic, tcm_automatic};
      cpu_constraintsB[i] = {1, 1, cpu_masks[i], tcm_automatic, tcm_automatic, tcm_automatic};
    }
    cpu_constraintsA[num_constraints-1] = 
      {1, 1, cpu_masks[num_constraints-1], tcm_automatic, tcm_automatic, tcm_automatic};

    cpu_constraintsB[num_constraints-1] = 
      {num_threads_per_constraint, num_threads_per_constraint, cpu_masks[num_constraints-1], tcm_automatic, tcm_automatic, tcm_automatic};

    tcm_permit_request_t reqA =
      make_request((num_constraints-1)*(num_threads_per_constraint-1)+1, (num_constraints-1)*(num_threads_per_constraint-1)+1, cpu_constraintsA, num_constraints);

    phA = request_permit(client_a, reqA, &phA);
    std::fill(eA_concurrency, eA_concurrency + num_constraints, num_threads_per_constraint-1);
    eA_concurrency[num_constraints-1] = 1;
    if (!check_permit(eA, phA)) {
      throw tcm_exception{nullptr};
    }

    register_thread(phA);

    tcm_permit_request_t reqB =
      make_request(num_constraints+num_threads_per_constraint-1, num_constraints+num_threads_per_constraint-1, cpu_constraintsB, num_constraints);
    phB = request_permit(client_b, reqB, &phB);

    std::fill(eB_concurrency, eB_concurrency + num_constraints, 1);
    eB_concurrency[num_constraints-1] = num_threads_per_constraint;
    if (!(check_permit(eB, phB) && check_permit(eA, phA))) {
      throw tcm_exception{nullptr};
    }

    unregister_thread();

    release_permit(phA);
    release_permit(phB);

    disconnect_client(client_a);
    disconnect_client(client_b);
  } catch (tcm_exception&) {
    return test_fail(test_name);
  }

  return test_epilog(test_name);
}

bool test_nested_activation_with_deactivated_outer() {
  const char* test_name = __func__;
  test_prolog(test_name);

  try {
    auto client_a = connect_new_client(client_renegotiate);
    auto client_b = connect_new_client(client_renegotiate);

    tcm_permit_handle_t phA = nullptr, phB = nullptr;

    uint32_t eA_concurrency = num_oversubscribed_resources, 
             eB_concurrency = 1;

    tcm_permit_t eA = make_active_permit(&eA_concurrency),
                 eB = make_active_permit(&eB_concurrency);

    tcm_permit_request_t reqA = make_request(1, num_oversubscribed_resources);
    phA = request_permit(client_a, reqA, &phA);
    if (!check_permit(eA, phA)) {
      throw tcm_exception{nullptr};
    }

    register_thread(phA);

    tcm_permit_request_t reqB = make_request(1, num_oversubscribed_resources);
    phB = request_permit(client_b, reqB, &phB);

    if (!(check_permit(eB, phB) && check_permit(eA, phA))) {
      throw tcm_exception{nullptr};
    }

    deactivate_permit(phB);
    eB_concurrency = 1; // Implicit thread from outer permit
    eB.state = TCM_PERMIT_STATE_INACTIVE;
    if (!(check_permit(eB, phB) && check_permit(eA, phA))) {
	      throw tcm_exception{nullptr};
    }
    // Callback of a nested permit is invoked because its activation involves regular procedure for
    // resources search rather than doing that lazily. Lazy activation is allowed here since
    // there is no actual resources demand other than that comes from the nested permit
    renegotiating_permits = {phB};
    activate_permit(phB);
    eB_concurrency = 1;
    eB.state = TCM_PERMIT_STATE_ACTIVE;
    if (!check_permit(eB, phB)) {
      throw tcm_exception{nullptr};
    }

    deactivate_permit(phB);

    eB_concurrency = 1; // Implicit thread from outer permit
    eB.state = TCM_PERMIT_STATE_INACTIVE;
    if (!check_permit(eB, phB)) {
      throw tcm_exception{nullptr};
    }

    unregister_thread();

    deactivate_permit(phA);
    eA_concurrency = num_oversubscribed_resources; // Lazy inactive
    eA.state = TCM_PERMIT_STATE_INACTIVE;
    if (!check_permit(eA, phA)) {
      throw tcm_exception{nullptr};
    }
    // Expecting callback invocation since the permit is no longer nested,
    // and must be involved in resource search and getting a new concurrency value,
    // hence the client should be notified about permit's new concurrency.
    renegotiating_permits = {phB};
    activate_permit(phB);
    eA_concurrency = 0;
    eB_concurrency = num_oversubscribed_resources;
    eB.state = TCM_PERMIT_STATE_ACTIVE;
    if (!(check_permit(eB, phB) && check_permit(eA, phA))) {
      throw tcm_exception{nullptr};
    }

    release_permit(phA);
    release_permit(phB);

    disconnect_client(client_a);
    disconnect_client(client_b);
  } catch (tcm_exception&) {
    return test_fail(test_name);
  }

  return test_epilog(test_name);
}

bool test_nested_clients_concurrent() {
  const char* test_name = __func__;
  test_prolog(test_name);

  try {
    std::string runtime_name = "outer client";
    uint32_t min_sw_threads = 1; uint32_t max_sw_threads = num_oversubscribed_resources;
    uint32_t expected_outer_concurrency = max_sw_threads;
    client_thread_pool outer{runtime_name, min_sw_threads, max_sw_threads};

    int data_size = num_oversubscribed_resources*10;
    std::vector<int> data(data_size, 0);

    tcm_permit_t expected_outer_permit = make_active_permit(&expected_outer_concurrency);
    outer.parallel_for(0, data_size, [&data, min_sw_threads, max_sw_threads](int begin, int end) {
      client_thread_pool inner{"inner client " + std::to_string(begin), min_sw_threads, max_sw_threads};
      uint32_t expected_inner_concurrency = 1;
      tcm_permit_t expected_inner_permit = make_active_permit(&expected_inner_concurrency);
      inner.parallel_for(begin, end, [&data] (int s, int e) {
        for (int i = s; i < e; ++ i) {
          data[i] += 1;
        }
      }, expected_inner_permit);
    }, expected_outer_permit);

    bool is_data_valid = std::all_of(data.begin(), data.end(), [](int x) { return x == 1; });
    if (!check(is_data_valid, "Data is valid")) {
      return test_fail(test_name);
    }
  } catch (tcm_exception&) {
    return test_fail(test_name);
  }

  // test perfect nesting
  try {
    uint32_t expected_outer_concurrency = std::max(1, num_oversubscribed_resources / 4);
    client_thread_pool outer{"outer client", expected_outer_concurrency, expected_outer_concurrency};

    int data_size = num_oversubscribed_resources*10;
    std::vector<int> data(data_size, 0);

    tcm_permit_t expected_outer_permit = make_active_permit(&expected_outer_concurrency);
    outer.parallel_for(0, data_size, [&data, expected_outer_concurrency](int begin, int end) {
      uint32_t expected_inner_concurrency = /*nested*/ 1 + (num_oversubscribed_resources - expected_outer_concurrency) / expected_outer_concurrency;
      client_thread_pool inner{"inner client" + std::to_string(begin), expected_inner_concurrency, expected_inner_concurrency};
      tcm_permit_t expected_inner_permit = make_active_permit(&expected_inner_concurrency);
      inner.parallel_for(begin, end, [&data] (int s, int e) {
        for (int i = s; i < e; ++ i) {
          data[i] += 1;
        }
      }, expected_inner_permit);
    }, expected_outer_permit);

    bool is_data_valid = std::all_of(data.begin(), data.end(), [](int x) { return x == 1; });
    if (!check(is_data_valid, "Data is valid")) {
      return test_fail(test_name);
    }
  } catch (tcm_exception&) {
    return test_fail(test_name);
  }

  return test_epilog(test_name);
}

bool test_nested_clients_partial_consumption() {
  const char* test_name = __func__;
  test_prolog(test_name);

  tcm_client_id_t clidA, clidB;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = num_oversubscribed_resources/2;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(num_oversubscribed_resources/4, num_oversubscribed_resources/2);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A half threads") && check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmRegisterThread(phA);
  if (!(check_success(r, "tcmRegisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {phA};


  uint32_t eB_concurrency = 0;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(int((float)num_oversubscribed_resources/4*3), num_oversubscribed_resources);
  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);

  eA_concurrency = num_oversubscribed_resources - rB.min_sw_threads + /*nested*/ 1; eB_concurrency = rB.min_sw_threads;
  auto unchanged_permits = list_unchanged_permits({{phA, &pA}});
  if (!(check_success(r, "tcmRequestPermit B all threads") &&
        check_permit(eA, phA) && check_permit(eB, pB) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = tcmRegisterThread(phB);
  if (!(check_success(r, "tcmRegisterThread B") && check_permit(eA, phA) && check_permit(eB, phB)))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread B") && check_permit(eA, phA) && check_permit(eB, phB)))
    return test_fail(test_name);

  renegotiating_permits = {phA};
  if (!check_success(tcmGetPermitData(phA, &pA), "Reading permit " + to_string(phA))) {
    return test_fail(test_name);
  }
  eA.concurrencies[0] = num_oversubscribed_resources/2;

  r = tcmReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{phA, &pA}});
  if (!(check_success(r, "tcmReleasePermit B") &&
        check_permit(eA, phA) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = tcmUnregisterThread();
  if (!(check_success(r, "tcmUnregisterThread A")) && check_permit(eA, phA))
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

  return test_epilog(test_name);
}

bool test_overlapping_clients() {
  const char* test_name = __func__;
  test_prolog(test_name);

  renegotiating_permits = {};   // no renegotiation is expected
  tcm_client_id_t clidA, clidB;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = num_oversubscribed_resources;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(num_oversubscribed_resources/2, num_oversubscribed_resources);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A all threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  renegotiating_permits = {phA};
  uint32_t eB_concurrency;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(num_oversubscribed_resources/2, num_oversubscribed_resources);
  eA_concurrency = rA.max_sw_threads - rB.min_sw_threads;
  eB_concurrency = rB.min_sw_threads;

  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{phA, &pA}});
  if (!(check_success(r, "tcmRequestPermit B all threads") &&
        check_permit(eA, phA) && check_permit(eB, pB) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {phB};
  eB.concurrencies[0] = num_oversubscribed_resources;

  r = tcmReleasePermit(phA);
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

  return test_epilog(test_name);
}

bool test_overlapping_clients_two_callbacks() {
  const char* test_name = __func__;
  test_prolog(test_name);

  renegotiating_permits = {};   // no renegotiation is expected
  tcm_client_id_t clidA, clidB, clidC;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidC);
  if (!check_success(r, "tcmConnect C"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr, phB = nullptr, phC = nullptr;
  uint32_t pA_concurrency, pB_concurrency, pC_concurrency,
           eA_concurrency = num_oversubscribed_resources/2;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
               pB = make_void_permit(&pB_concurrency),
               pC = make_void_permit(&pC_concurrency),
               eA = make_active_permit(&eA_concurrency);

  uint32_t min_sw_threads_phA = 1, min_sw_threads_phB = 1,
           max_sw_threads_phA = num_oversubscribed_resources / 2,
           max_sw_threads_phB = num_oversubscribed_resources - max_sw_threads_phA;

  tcm_permit_request_t rA = make_request(min_sw_threads_phA, max_sw_threads_phA);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit half platform resources, phA=" + to_string(phA)) &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  uint32_t eB_concurrency = max_sw_threads_phB;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(min_sw_threads_phB, max_sw_threads_phB);
  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit the rest resources, phB=" + to_string(phB)) &&
        check_permit(eA, phA) && check_permit(eB, pB)))
    return test_fail(test_name);

  renegotiating_permits = {phA, phB};

  uint32_t eC_concurrency;
  tcm_permit_t eC = make_active_permit(&eC_concurrency);

  uint32_t min_sw_threads_phC = num_oversubscribed_resources / 4;
  tcm_permit_request_t rC = make_request(min_sw_threads_phC, num_oversubscribed_resources);

  r = tcmRequestPermit(clidC, rC, &phC, &phC, &pC);
  if (!check_success(r, "tcmRequestPermit all platform, phC=" + to_string(phC)))
      return test_fail(test_name);

  auto unchanged_permits = list_unchanged_permits({{phA, &pA}, {phB, &pB}});

  // The concurrencies of permit A and B can be borrowed since they have room for negotiation. The
  // request for permit C requires negotiation from only one of them to satisfy its minimum.
  // However, it is not known which one is chosen for negotiation. So we check first the expected
  // concurrency for permit C and then that the negotiation happened with only one of the permits.
  eC_concurrency = min_sw_threads_phC;
  if (!(check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phA, &pA), "Reading permit " + to_string(phA)))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phB, &pB), "Reading permit " + to_string(phB)))
    return test_fail(test_name);

  if (!(check_permit(eA, pA, skip_concurrenency_check) &&
        check_permit(eB, pB, skip_concurrenency_check)))
    return test_fail(test_name);

  // Then we check that negotiation succeeds for only one of the existing permits
  auto const& A_concurrency = pA.concurrencies[0]; auto const& B_concurrency = pB.concurrencies[0];
  bool succeeded =
      (A_concurrency == eA_concurrency - min_sw_threads_phC && B_concurrency == eB_concurrency) ||
      (A_concurrency == eA_concurrency && B_concurrency == eB_concurrency - min_sw_threads_phC);

  if (!check(succeeded, "Only one permit had been negotiated."))
    return test_fail(test_name);

  // When A is released, we should get a callback for others only if their permits has changed.
  renegotiating_permits = {phB, phC};

  // Reading current permits' data to expect them after release of phA
  if (!check_success(tcmGetPermitData(phB, &eB), "Reading permit phB=" + to_string(phB)))
    return test_fail(test_name);
  if (!check_success(tcmGetPermitData(phC, &eC), "Reading permit phC=" + to_string(phC)))
    return test_fail(test_name);

  // The A's resources should also be given to only one permit to minimize the disruption.
  // Determine the number of resources to be given additionally to another permit
  if (!check_success(tcmGetPermitData(phA, &pA), "Reading permit phA=" + to_string(phA)))
    return test_fail(test_name);
  const uint32_t released_concurrency = pA.concurrencies[0];
  r = tcmReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{phB, &pB}, {phC, &pC}});
  if (!(check_success(r, "tcmReleasePermit A=" + to_string(phA)) &&
        check(renegotiating_permits == unchanged_permits && unchanged_permits.size() == 1,
              "Unexpected negotiation during release of phA")))
    return test_fail(test_name);

  if (*unchanged_permits.cbegin() == phB) {
      eC_concurrency += released_concurrency;
  } else {
      eB_concurrency += released_concurrency;
  }
  if (!(check_permit(eB, phB) && check_permit(eC, phC)))
    return test_fail(test_name);

  // When B is released, all the resources should be given to C since no other demand exists
  renegotiating_permits = {phC};
  eC.concurrencies[0] = num_oversubscribed_resources;
  r = tcmReleasePermit(phB);
  if (!(check_success(r, "tcmReleasePermit B") && check_permit(eC, phC)))
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

  return test_epilog(test_name);
}

bool test_partial_release() {
  const char* test_name = __func__;
  test_prolog(test_name);

  renegotiating_permits = {};   // no renegotiation is expected

  tcm_client_id_t clidA;
  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect (client A)"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = num_oversubscribed_resources;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(0, num_oversubscribed_resources);

  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // Release some of the resources by re-requesting for less
  eA.concurrencies[0] = rA.max_sw_threads = num_oversubscribed_resources/2;
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (re-request client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  tcm_client_id_t clidB;
  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect (client B)"))
    return test_fail(test_name);

  tcm_permit_handle_t phB = nullptr;
  uint32_t pB_concurrency, eB_concurrency = num_oversubscribed_resources/2;

  tcm_permit_t pB = make_void_permit(&pB_concurrency),
                eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(0, num_oversubscribed_resources/2);
  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit (client B)") &&
        check_permit(eA, phA) && check_permit(eB, phB)))
    return test_fail(test_name);

  // Renegotiation should not happen for permit A since it was fully satisfied
  // already

  r = tcmReleasePermit(phB);
  if (!check_success(r, "tcmReleasePermit (client B)") &&
      check_permit(eA, phA))
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
  const char* test_name = __func__;
  test_prolog(test_name);

  tcm_client_id_t clidA;
  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect (client A)"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = num_oversubscribed_resources;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(0, num_oversubscribed_resources);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  eA.state = TCM_PERMIT_STATE_INACTIVE;
  eA_concurrency = num_oversubscribed_resources;
  r = tcmDeactivatePermit(phA);
  if (!(check_success(r, "tcmDeactivatePermit (client A)") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  tcm_client_id_t clidB;
  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect (client B)"))
    return test_fail(test_name);

  tcm_permit_handle_t phB = nullptr;
  uint32_t pB_concurrency, eB_concurrency = num_oversubscribed_resources/2;

  tcm_permit_t pB = make_void_permit(&pB_concurrency),
                eB = make_active_permit(&eB_concurrency);

  eA_concurrency = 0;
  tcm_permit_request_t rB = make_request(1, num_oversubscribed_resources/2);
  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit (client B)") &&
        check_permit(eB, pB) && check_permit(eA, phA)))
    return test_fail(test_name);


  // Activate previously deactivated request from client A. Since the amount of
  // previously held resources are not available, the renegotiation mechanism
  // should take place for client A. The callback should be invoked for client A
  // to notify about the state/concurrency change.
  // Since the renegotiation should happen for client B as well,
  // its callback should also be invoked.
  renegotiating_permits = {phA, phB};

  eA.state = TCM_PERMIT_STATE_ACTIVE;
  eB_concurrency = rB.max_sw_threads;
  // Permit A won't negotiate since its minimum is satisfied. However, it will use the remaining
  // resources since its desired number is full machine
  eA_concurrency = num_oversubscribed_resources - rB.max_sw_threads;

  r = tcmActivatePermit(phA);
  auto unchanged_permits = list_unchanged_permits({{phB, &pB}});
  if (!(check_success(r, "tcmActivatePermit (client A)") &&
        check_permit(eA, phA) && check_permit(eB, phB) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {phA};

  eA_concurrency = num_oversubscribed_resources;
  r = tcmReleasePermit(phB);
  if (!(check_success(r, "tcmReleasePermit (client B)") &&
        check_permit(eA, phA) && renegotiating_permits.size() == 0))
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

bool test_support_for_pending_state() {
  const char* test_name = __func__;
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

  tcm_permit_request_t reqA = make_request(
      num_oversubscribed_resources / 2, num_oversubscribed_resources
  );
  eA.concurrencies[0] = num_oversubscribed_resources;
  r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit for client A for {" +
                      std::to_string(reqA.min_sw_threads) + ", " +
                      std::to_string(reqA.max_sw_threads) + "}") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  renegotiating_permits = {phA};
  tcm_permit_request_t reqB = make_request(
      num_oversubscribed_resources - num_oversubscribed_resources / 2, num_oversubscribed_resources
  );
  eA.concurrencies[0] = num_oversubscribed_resources / 2;
  eB.concurrencies[0] = num_oversubscribed_resources - eA_concurrency;
  r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{phA, &pA}});
  if (!(check_success(r, "tcmRequestPermit for client B for {" +
                      std::to_string(reqB.min_sw_threads) + ", " +
                      std::to_string(reqB.max_sw_threads) + "}") &&
        check_permit(eA, phA) && check_permit(eB, pB) &&
        check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phA, &pA), "Reading data from permit " + to_string(phA)))
    return test_fail(test_name);

  tcm_permit_request_t reqC = make_request(num_oversubscribed_resources, num_oversubscribed_resources);
  eC.concurrencies[0] = 0;
  eC.state = TCM_PERMIT_STATE_PENDING;
  r = tcmRequestPermit(clidC, reqC, &phC, &phC, &pC);
  if (!(check_success(r, "tcmRequestPermit for client C for {" + std::to_string(reqC.min_sw_threads)
                      + ", " + std::to_string(reqC.max_sw_threads) + "}") &&
        check_permit(eC, pC)))
    return test_fail(test_name);

  tcm_permit_request_t reqD = make_request(
      num_oversubscribed_resources - num_oversubscribed_resources / 2, num_oversubscribed_resources
  );
  eD.concurrencies[0] = 0;
  eD.state = TCM_PERMIT_STATE_PENDING;
  r = tcmRequestPermit(clidD, reqD, &phD, &phD, &pD);
  if (!(check_success(r, "tcmRequestPermit for client D for {" +
                      std::to_string(reqD.min_sw_threads) + ", " +
                      std::to_string(reqD.max_sw_threads) + "}") &&
        check_permit(eA, phA) && check_permit(eB, phB) && check_permit(eC, phC) &&
        check_permit(eD, pD)))
    return test_fail(test_name);

  renegotiating_permits = {phA, phC, phD};
  eD.concurrencies[0] = num_oversubscribed_resources - num_oversubscribed_resources / 2;
  eD.state = TCM_PERMIT_STATE_ACTIVE;
  r = tcmReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{phA, &pA}, {phC, &pC}, {phD, &pD}});
  if (!(check_success(r, "tcmReleasePermit for client B")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phD, &pD), "Reading data from permit " + to_string(phD)))
    return test_fail(test_name);

  renegotiating_permits = {phC, phD};
  reqA = make_request(num_oversubscribed_resources, num_oversubscribed_resources);
  eA.concurrencies[0] = 0; eA.state = TCM_PERMIT_STATE_PENDING;
  eD.concurrencies[0] = num_oversubscribed_resources;
  r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
  unchanged_permits = list_unchanged_permits({{phC, &pC}, {phD, &pD}});
  if (!(check_success(r, "tcmRequestPermit for client A (re-requesting)")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phD, &pD), "Reading data from permit " + to_string(phD)))
    return test_fail(test_name);

  renegotiating_permits = {phC, phD};
  r = tcmReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{phC, &pC}, {phD, &pD}});
  if (!(check_success(r, "tcmReleasePermit for client A")
        && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  renegotiating_permits = {phC};
  eC.concurrencies[0] = num_oversubscribed_resources;
  eC.state = TCM_PERMIT_STATE_ACTIVE;
  r = tcmReleasePermit(phD);
  unchanged_permits = list_unchanged_permits({{phC, &pC}});
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

  return test_epilog(test_name);
}

int main() {
  bool res = true;

  res &= test_alternating_clients();
  res &= test_nested_clients();
  res &= test_nested_clients_renegotiation();
  res &= test_nested_clients_constrained();
  res &= test_nested_activation_with_deactivated_outer();
  res &= test_nested_clients_concurrent();
  res &= test_nested_clients_partial_consumption();
  res &= test_overlapping_clients();
  res &= test_overlapping_clients_two_callbacks();
  res &= test_partial_release();
  res &= test_permit_reactivation();
  res &= test_support_for_pending_state();

  return int(!res);
}
