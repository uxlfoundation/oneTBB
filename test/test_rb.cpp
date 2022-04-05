/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#include "test_utils.h"

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
  int32_t distributed_threads = std::min(total_demand, total_number_of_threads);
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
                                            int32_t available_threads = total_number_of_threads)
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
bool check_permits_concurrencies(const std::set<zerm_permit_handle_t>& phs,
                                 // TODO: accept corresponding requests
                                 const std::vector<int32_t>& demands)
{
  bool result = true;

  auto concurrencies = compute_concurrencies(demands);
  auto concurrencies_copy = concurrencies;
  std::vector<uint32_t> permits_concurrencies(phs.size());
  std::vector<zerm_permit_t> permits(phs.size());
  for (unsigned i = 0; i < phs.size(); ++i) {
    permits[i] = make_void_permit(&permits_concurrencies[i]);
  }

  int i = 0;
  std::vector<uint32_t> missing;

  for (const auto& ph : phs) {
    result &= check_success(zermGetPermitData(ph, &permits[i]),
                            "Reading data from permit " + std::to_string(uintptr_t(ph)));

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
    check(false, "Found all concurrencies in concurrency set for given permits.", ss.str());
  }

  return result;
}

bool test_nested_clients() {
  const char* test_name = "test_nested_clients";
  test_prolog(test_name);

  renegotiating_permits = {};    // no renegotiation is expected
  zerm_client_id_t clidA,  clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A") && check_permit(eA, pA)))
    return test_fail(test_name);

  r = zermRegisterThread(phA);
  if (!(check_success(r, "zermRegisterThread A") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  uint32_t dummy_concurrency;
  zerm_permit_t eB = make_active_permit(&dummy_concurrency);

  zerm_permit_request_t rB = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "zermRequestPermit B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = zermRegisterThread(phB);
  if (!(check_success(r, "zermRegisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        check_permit(eA, phA, skip_concurrenency_check)))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        check_permit(eA, phA, skip_concurrenency_check)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  eA.concurrencies[0] = total_number_of_threads;

  r = zermReleasePermit(phB);
  if (!(check_success(r, "zermReleasePermit B") &&
        check_permit(eA, phA) && renegotiating_permits.size() == 0))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread A") &&
        check_permit(eA, phA)))
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
           eA_concurrency = total_number_of_threads/2;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rA = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  r = zermRegisterThread(phA);
  if (!(check_success(r, "zermRegisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  uint32_t dummy_concurrency;
  zerm_permit_t eB = make_active_permit(&dummy_concurrency);

  zerm_permit_request_t rB = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "zermRequestPermit B all threads") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = zermRegisterThread(phB);
  if (!(check_success(r, "zermRegisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check)))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  if (!check_success(zermGetPermitData(phA, &pA),
                     "Reading permit " + std::to_string(uintptr_t(phA)))) {
    return test_fail(test_name);
  }
  eA.concurrencies[0] = total_number_of_threads/2;

  r = zermReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "zermReleasePermit B") &&
        check_permit(eA, phA) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  r = zermUnregisterThread();
  if (!(check_success(r, "zermUnregisterThread A")) && check_permit(eA, phA))
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

  renegotiating_permits = {};   // no renegotiation is expected
  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A all threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  uint32_t eB_concurrency;
  zerm_permit_t eB = make_active_permit(&eB_concurrency);

  zerm_permit_request_t rB = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "zermRequestPermit B all threads") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {&phB};
  eB.concurrencies[0] = total_number_of_threads;

  r = zermReleasePermit(phA);
  if (!(check_success(r, "zermReleasePermit A") &&
        check_permit(eB, phB) && renegotiating_permits.size() == 0))
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

  renegotiating_permits = {};   // no renegotiation is expected
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
           eA_concurrency = total_number_of_threads/2;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                pC = make_void_permit(&pC_concurrency),
                eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rA = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  uint32_t eB_concurrency = total_number_of_threads/2;
  zerm_permit_t eB = make_active_permit(&eB_concurrency);

  zerm_permit_request_t rB = make_request(0, total_number_of_threads / 2);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit B half threads") &&
        check_permit(eA, phA) && check_permit(eB, pB)))
    return test_fail(test_name);

  renegotiating_permits = {&phA, &phB};
  std::vector<int32_t> demands = {
    total_number_of_threads/2, total_number_of_threads/2, total_number_of_threads
  };

  uint32_t eC_concurrency;
  zerm_permit_t eC = make_active_permit(&eC_concurrency);

  zerm_permit_request_t rC = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidC, rC, &phC, &phC, &pC);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phB, &pB}});
  if (!(check_success(r, "zermRequestPermit C all threads") &&
        check_permits_concurrencies({phA, phB, phC}, demands) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check) &&
        check_permit(eC, phC, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  // When A is released, we should get a callback for others only if their
  // permits has changed.
  renegotiating_permits = {&phB, &phC};
  check_success(zermGetPermitData(phB, &pB), "Reading permit phB");
  check_success(zermGetPermitData(phC, &pC), "Reading permit phC");

  demands = {total_number_of_threads/2, total_number_of_threads};
  r = zermReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phB, &pB}, {&phC, &pC}});
  if (!(check_success(r, "zermReleasePermit A") &&
        check_permits_concurrencies({phB, phC}, demands) &&
        check_permit(eB, phB, skip_concurrenency_check) &&
        check_permit(eC, phC, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  // When B is released, we should get a second callback for pC:
  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;

  r = zermReleasePermit(phB);
  if (!(check_success(r, "zermReleasePermit B") &&
        check_permit(eC, phC) && renegotiating_permits.size() == 0))
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

  renegotiating_permits = {};   // no renegotiation is expected

  zerm_client_id_t clidA;
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // Release some of the resources by re-requesting for less
  eA.concurrencies[0] = rA.max_sw_threads = total_number_of_threads/2;
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit (re-request client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  zerm_client_id_t clidB;
  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect (client B)"))
    return false;

  zerm_permit_handle_t phB = nullptr;

  zerm_permit_t pB = make_void_permit(&pA_concurrency),
                eB = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rB = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit (client B)") &&
        check_permit(eA, phA) && check_permit(eB, phB)))
    return test_fail(test_name);

  // Renegotiation should not happen for permit A since it was fully satisfied
  // already

  r = zermReleasePermit(phB);
  if (!check_success(r, "zermReleasePermit (client B)") &&
      check_permit(eA, phA))
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

  zerm_client_id_t clidA;
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check_success(r, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  eA.state = ZERM_PERMIT_STATE_INACTIVE;
  r = zermDeactivatePermit(phA);
  if (!(check_success(r, "zermDeactivatePermit (client A)") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  zerm_client_id_t clidB;
  r = zermConnect(client_renegotiate, &clidB);
  if (!check_success(r, "zermConnect (client B)"))
    return false;
  
  zerm_permit_handle_t phB = nullptr;
  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;

  zerm_permit_t pB = make_void_permit(&pB_concurrency),
                eB = make_active_permit(&eB_concurrency);

  zerm_permit_request_t rB = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "zermRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return test_fail(test_name);

  // Activate previously deactivated request from client A. Since the amount of
  // previously held resources are not available, the renegotiation mechanism
  // should take place for client A, but its callback should not be invoked.
  // Since, however, the renegotiation should happen for client B as well,
  // its callback should be invoked.
  renegotiating_permits = {&phB};

  auto demands = {total_number_of_threads, total_number_of_threads/2};
  eA.state = ZERM_PERMIT_STATE_ACTIVE;

  r = zermActivatePermit(phA);
  auto unchanged_permits = list_unchanged_permits({{&phB, &pB}});
  if (!(check_success(r, "zermActivatePermit (client A)") &&
        check_permits_concurrencies({phA, phB}, demands) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {&phA};

  r = zermReleasePermit(phB);
  if (!(check_success(r, "zermReleasePermit (client B)") &&
        check_permit(eA, phA) && renegotiating_permits.size() == 0))
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

std::atomic<bool> allow_renegotiation{false};
std::atomic<bool> is_callback_invoked{false};
zerm_permit_handle_t phS{nullptr};

bool test_rigid_concurrency_permit() {
  const char* test_name = "test_rigid_concurrency_permit";
  test_prolog(test_name);

  zerm_client_id_t clid{0};

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

  zerm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(total_number_of_threads / 2);

  auto clear_state = [&clid, test_name]()
  {
    zermDisconnect(clid);
    return test_fail(test_name);
  };

  zerm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  zerm_permit_request_t rA = make_request(0, (int32_t)eA_concurrency);

  r = zermRequestPermit(clid, rA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit regular") &&
        check_permit(eA, pA)))
    return clear_state();

  // Request that shouldn't be renegotiated in active state
  zerm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  zerm_permit_request_t rS = make_request(0, total_number_of_threads, rigid_concurrency_flags);

  std::vector<int32_t> demands = { rA.max_sw_threads, rS.max_sw_threads };
  auto concurrencies = compute_concurrencies(demands, total_number_of_threads);

  uint32_t pS_concurrency{0}, eS_concurrency{uint32_t(concurrencies[1])};

  zerm_permit_t pS = make_void_permit(&pS_concurrency),
                eS = make_active_permit(&eS_concurrency, nullptr, 1, rigid_concurrency_flags);

  r = zermRequestPermit(clid, rS, &phS, &phS, &pS);
  if (!(check_success(r, "zermRequestPermit static") &&
        check_permits_concurrencies({ phA, phS }, demands) &&
        check_permit(eS, pS, skip_concurrenency_check)))
  {
    zermReleasePermit(phA);
    zermReleasePermit(phS);
    return clear_state();
  }

  check(is_callback_invoked, "Check renegotiation for the regular permit.");

  is_callback_invoked = false;

  r = zermReleasePermit(phA);
  if (!check_success(r, "zermReleasePermit regular"))
  {
    zermReleasePermit(phS);
    return clear_state();
  }

  check(!is_callback_invoked,
        "Check static permit does not participates in the renegotiation"
        " while in active state.");

  // Check that renegotiation takes place when the static permit transferred to
  // the IDLE state, but its callback is not invoked.
  allow_renegotiation = true;
  eS.concurrencies[0] = total_number_of_threads;
  eS.state = ZERM_PERMIT_STATE_IDLE;

  r = zermIdlePermit(phS);
  if (!(check_success(r, "zermIdlePermit static") &&
        check_permit(eS, phS)))
  {
    zermReleasePermit(phS);
    return clear_state();
  }

  if (!check(!is_callback_invoked, "Callback shouldn't be invoked after "
             "renegotiating permit that switched to idle state."))
  {
    zermReleasePermit(phS);
    return clear_state();
  }

  r = zermReleasePermit(phS);
  if (!check_success(r, "zermReleasePermit static"))
    return clear_state();

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
  if (!check_success(r, "zermConnect for client D"))
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

  zerm_permit_request_t reqA = make_request(total_number_of_threads / 2, total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check_success(r, "zermRequestPermit for client A") && check_permit(eA, pA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  zerm_permit_request_t reqB = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads / 2;
  eB.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "zermRequestPermit for client B")
        && check_permit(eA, phA) && check_permit(eB, pB))
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation"))
    return test_fail(test_name);

  if (!check_success(zermGetPermitData(phA, &pA),
             "Reading data from permit " + std::to_string(uintptr_t(phA))))
    return test_fail(test_name);

  zerm_permit_request_t reqC = make_request(total_number_of_threads, total_number_of_threads);
  eC.concurrencies[0] = 0;
  eC.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidC, reqC, &phC, &phC, &pC);
  if (!(check_success(r, "zermRequestPermit for client C") && check_permit(eC, pC)))
    return test_fail(test_name);

  zerm_permit_request_t reqD = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eD.concurrencies[0] = 0;
  eD.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidD, reqD, &phD, &phD, &pD);
  if (!(check_success(r, "zermRequestPermit for client D") && check_permit(eA, phA)
      && check_permit(eB, phB) && check_permit(eC, phC) && check_permit(eD, pD)))
    return test_fail(test_name);

  renegotiating_permits = {&phA, &phC, &phD};
  eD.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eD.state = ZERM_PERMIT_STATE_ACTIVE;
  r = zermReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "zermReleasePermit for client B")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(zermGetPermitData(phD, &pD),
             "Reading data from permit " + std::to_string(uintptr_t(phD))))
    return test_fail(test_name);

  renegotiating_permits = {&phC, &phD};
  reqA = make_request(total_number_of_threads, total_number_of_threads);
  eA.concurrencies[0] = 0;
  eD.concurrencies[0] = total_number_of_threads;
  eA.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "zermRequestPermit for client A (re-requesting)")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(zermGetPermitData(phD, &pD),
                     "Reading data from permit " + std::to_string(uintptr_t(phD))))
    return test_fail(test_name);

  renegotiating_permits = {&phC, &phD};
  r = zermReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "zermReleasePermit for client A")
        && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;
  eC.state = ZERM_PERMIT_STATE_ACTIVE;
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
  if (SetEnv("RM_STRATEGY", "FAIR")) {
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
  res &= test_rigid_concurrency_permit();
  res &= test_support_for_pending_state();

  return int(!res);
}
