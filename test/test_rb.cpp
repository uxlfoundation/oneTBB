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
  tcm_client_id_t clidA,  clidB;

  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect A"))
    return test_fail(test_name);

  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect B"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A") && check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmRegisterThread(phA);
  if (!(check_success(r, "tcmRegisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};

  uint32_t eB_concurrency = 0;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(total_number_of_threads/2, total_number_of_threads);

  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  eA_concurrency = total_number_of_threads - total_number_of_threads/2;
  eB_concurrency = total_number_of_threads/2;
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});

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

  renegotiating_permits = {&phA};
  eA.concurrencies[0] = total_number_of_threads;

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

bool test_nested_clients_partial_consumption() {
  const char* test_name = "test_nested_clients_partial_consumption";
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
           eA_concurrency = total_number_of_threads/2;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(total_number_of_threads/4, total_number_of_threads/2);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A half threads") && check_permit(eA, pA)))
    return test_fail(test_name);

  r = tcmRegisterThread(phA);
  if (!(check_success(r, "tcmRegisterThread A") && check_permit(eA, phA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};


  uint32_t eB_concurrency = 0;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(total_number_of_threads/4*3, total_number_of_threads);
  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);

  eA_concurrency = total_number_of_threads - rB.min_sw_threads; eB_concurrency = rB.min_sw_threads;
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
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

  renegotiating_permits = {&phA};
  if (!check_success(tcmGetPermitData(phA, &pA),
                     "Reading permit " + std::to_string(uintptr_t(phA)))) {
    return test_fail(test_name);
  }
  eA.concurrencies[0] = total_number_of_threads/2;

  r = tcmReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phA, &pA}});
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
  const char* test_name = "test_overlapping_clients";
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
           eA_concurrency = total_number_of_threads;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(total_number_of_threads/2, total_number_of_threads);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A all threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  uint32_t eB_concurrency;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(total_number_of_threads/2, total_number_of_threads);

  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "tcmRequestPermit B all threads") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {&phB};
  eB.concurrencies[0] = total_number_of_threads;

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
  const char* test_name = "test_overlapping_clients_two_callbacks";
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
           eA_concurrency = total_number_of_threads/2;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                pB = make_void_permit(&pB_concurrency),
                pC = make_void_permit(&pC_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(0, total_number_of_threads/2);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  uint32_t eB_concurrency = total_number_of_threads/2;
  tcm_permit_t eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(0, total_number_of_threads/2);
  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit B half threads") &&
        check_permit(eA, phA) && check_permit(eB, pB)))
    return test_fail(test_name);

  renegotiating_permits = {&phA, &phB};

  uint32_t eC_concurrency;
  tcm_permit_t eC = make_active_permit(&eC_concurrency);

  tcm_permit_request_t rC = make_request(total_number_of_threads/2, total_number_of_threads);

  r = tcmRequestPermit(clidC, rC, &phC, &phC, &pC);
  if (!check_success(r, "tcmRequestPermit C all threads"))
      return test_fail(test_name);

  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phB, &pB}});

  // The concurrencies of permit A and B can be borrowed since they specified that they can have
  // them zero at the minimum. The request for permit C requires negotiation from one of them to
  // satisfy its minimum. However, it is not known which one is chosen for negotiation. So we check
  // first the determined value in permit C and that expected negotation happened.
  eC_concurrency = total_number_of_threads/2;
  if (!(check_permit(eC, phC) && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phA, &pA),
                     "Reading permit " + std::to_string(uintptr_t(phA))))
    return test_fail(test_name);
  if (!check_success(tcmGetPermitData(phB, &pB),
                     "Reading permit " + std::to_string(uintptr_t(phB))))
    return test_fail(test_name);

  if (!(check_permit(eA, pA, skip_concurrenency_check) &&
        check_permit(eB, pB, skip_concurrenency_check)))
    return test_fail(test_name);

  // Then we check that negotiation succeeds for one of the existing permits
  const uint32_t &A_concurrency = pA.concurrencies[0], &B_concurrency = pB.concurrencies[0];
  const uint32_t expected_concurrency = uint32_t(total_number_of_threads/2);

  bool succeeded =
      (A_concurrency == 0 && B_concurrency == expected_concurrency) ||
      (A_concurrency == expected_concurrency && B_concurrency == 0);

  if (!succeeded) {
    check(false, "Unexpected resource distribuion.");
    return test_fail(test_name);
  }

  // When A is released, we should get a callback for others only if their
  // permits has changed.
  renegotiating_permits = {&phB, &phC};
  if (!check_success(tcmGetPermitData(phC, &pC), "Reading permit phC"))
    test_fail(test_name);

  r = tcmReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phB, &pB}, {&phC, &pC}});
  if (!(check_success(r, "tcmReleasePermit A") && renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  if (A_concurrency == 0) {
      // Nothing has changed for permits B and C, since no additional concurrency appears by the
      // release of A
      eB_concurrency = expected_concurrency;
  } else {
      // The permit A released total_number_of_threads/2 of resources. They are distributed evenly
      // between two existing permits. However, the number of resources might not divide by two
      // without a remainder.
      eB_concurrency = total_number_of_threads/2/2;
      eC_concurrency += total_number_of_threads/2 - eB_concurrency;
  }
  if (!check_success(tcmGetPermitData(phB, &pB),
                     "Reading permit " + std::to_string(uintptr_t(phB))))
    return test_fail(test_name);
  if (!check_success(tcmGetPermitData(phC, &pC),
                     "Reading permit " + std::to_string(uintptr_t(phC))))
    return test_fail(test_name);

  if (!(check_permit(eB, pB, skip_concurrenency_check) &&
        check_permit(eC, pC, skip_concurrenency_check)))
    return test_fail(test_name);

  succeeded = (eB_concurrency == pB.concurrencies[0] && eC_concurrency == pC.concurrencies[0]) ||
      (eC_concurrency == pB.concurrencies[0] && eB_concurrency == pC.concurrencies[0]);
  if (!succeeded) {
    check(false, "Unexpected resource distribuion.");
    test_fail(test_name);
  }

  // When B is released, all the resources should be given to C since no other demand exists
  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;
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
  const char* test_name = "test_partial_release";
  test_prolog(test_name);

  renegotiating_permits = {};   // no renegotiation is expected

  tcm_client_id_t clidA;
  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect (client A)"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(0, total_number_of_threads);

  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  // Release some of the resources by re-requesting for less
  eA.concurrencies[0] = rA.max_sw_threads = total_number_of_threads/2;
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (re-request client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  tcm_client_id_t clidB;
  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect (client B)"))
    return test_fail(test_name);

  tcm_permit_handle_t phB = nullptr;
  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;

  tcm_permit_t pB = make_void_permit(&pB_concurrency),
                eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(0, total_number_of_threads/2);
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
  const char* test_name = "test_permit_reactivation";
  test_prolog(test_name);

  tcm_client_id_t clidA;
  tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
  if (!check_success(r, "tcmConnect (client A)"))
    return test_fail(test_name);

  tcm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = tcmRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return test_fail(test_name);

  eA.state = TCM_PERMIT_STATE_INACTIVE;
  eA_concurrency = 0;
  r = tcmDeactivatePermit(phA);
  if (!(check_success(r, "tcmDeactivatePermit (client A)") &&
        check_permit(eA, phA)))
    return test_fail(test_name);

  tcm_client_id_t clidB;
  r = tcmConnect(client_renegotiate, &clidB);
  if (!check_success(r, "tcmConnect (client B)"))
    return test_fail(test_name);

  tcm_permit_handle_t phB = nullptr;
  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;

  tcm_permit_t pB = make_void_permit(&pB_concurrency),
                eB = make_active_permit(&eB_concurrency);

  tcm_permit_request_t rB = make_request(1, total_number_of_threads/2);
  r = tcmRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check_success(r, "tcmRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return test_fail(test_name);


  // Activate previously deactivated request from client A. Since the amount of
  // previously held resources are not available, the renegotiation mechanism
  // should take place for client A, but its callback should not be invoked.
  // Since, however, the renegotiation should happen for client B as well,
  // its callback should be invoked.
  renegotiating_permits = {&phB};

  eA.state = TCM_PERMIT_STATE_ACTIVE;
  eB_concurrency = rB.max_sw_threads;
  // Permit A won't negotiate since its minimum is satisfied. However, it will use the remaining
  // resources since its desired number is full machine
  eA_concurrency = total_number_of_threads - rB.max_sw_threads;

  r = tcmActivatePermit(phA);
  auto unchanged_permits = list_unchanged_permits({{&phB, &pB}});
  if (!(check_success(r, "tcmActivatePermit (client A)") &&
        check_permit(eA, phA) && check_permit(eB, phB) &&
        renegotiating_permits == unchanged_permits))
    return test_fail(test_name);

  renegotiating_permits = {&phA};

  eA_concurrency = total_number_of_threads;
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

std::atomic<bool> allow_renegotiation{false};
std::atomic<bool> is_callback_invoked{false};
tcm_permit_handle_t phS{nullptr};

bool test_rigid_concurrency_permit() {
  const char* test_name = "test_rigid_concurrency_permit";
  test_prolog(test_name);

  tcm_client_id_t clid{0};

  auto clear_state = [&clid, test_name]()
  {
    tcmDisconnect(clid);
    return test_fail(test_name);
  };

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

  tcm_permit_handle_t phA{nullptr};
  uint32_t pA_concurrency{0}, eA_concurrency = uint32_t(total_number_of_threads / 2);

  tcm_permit_t pA = make_void_permit(&pA_concurrency),
                eA = make_active_permit(&eA_concurrency);

  tcm_permit_request_t rA = make_request(total_number_of_threads/4, (int32_t)eA_concurrency);

  r = tcmRequestPermit(clid, rA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit regular") &&
        check_permit(eA, pA)))
    return clear_state();

  // Request that shouldn't be renegotiated in active state
  tcm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  tcm_permit_request_t rS = make_request(0, total_number_of_threads, /*constraints*/nullptr,
                                          /*size*/0, TCM_REQUEST_PRIORITY_NORMAL,
                                          rigid_concurrency_flags);

  // Permit S won't negotiate with the permit A since its minimum is satisfied.
  uint32_t pS_concurrency, eS_concurrency = total_number_of_threads - eA_concurrency;

  tcm_permit_t pS = make_void_permit(&pS_concurrency);
  tcm_permit_t eS = make_active_permit(&eS_concurrency);
  eS.flags = rigid_concurrency_flags;

  r = tcmRequestPermit(clid, rS, &phS, &phS, &pS);
  if (!(check_success(r, "tcmRequestPermit static") &&
        check_permit(eA, phA) && check_permit(eS, pS)))
  {
    tcmReleasePermit(phA);
    tcmReleasePermit(phS);
    return clear_state();
  }

  check(is_callback_invoked, "Check renegotiation for the regular permit.");

  is_callback_invoked = false;

  r = tcmReleasePermit(phA);
  if (!check_success(r, "tcmReleasePermit regular")) {
    tcmReleasePermit(phS);
    return clear_state();
  }

  if (!check(!is_callback_invoked, "Check rigid concurrency permit does not participate"
             " in the renegotiation while in active state."))
  {
    tcmReleasePermit(phS);
    return clear_state();
  }

  r = tcmIdlePermit(phS);
  eS.state = TCM_PERMIT_STATE_IDLE;
  if (!(check_success(r, "tcmIdlePermit static") && check_permit(eS, phS) &&
      check(!is_callback_invoked, "Callback not invoked for the rigid concurrency permit that "
            "switched to the idle state.")))
  {
    tcmReleasePermit(phS);
    return clear_state();
  }

  r = tcmDeactivatePermit(phS);
  eS.concurrencies[0] = 0; eS.state = TCM_PERMIT_STATE_INACTIVE;
  if (!(check_success(r, "tcmDeactivatePermit static") && check_permit(eS, phS) &&
      check(!is_callback_invoked, "Callback not invoked for the rigid concurrency permit that "
            "was deactivated.")))
  {
    tcmReleasePermit(phS);
    return clear_state();
  }

  r = tcmActivatePermit(phS);
  eS.concurrencies[0] = total_number_of_threads; eS.state = TCM_PERMIT_STATE_ACTIVE;
  if (!(check_success(r, "tcmActivatePermit static") && check_permit(eS, phS) &&
      check(!is_callback_invoked, "Callback not invoked for the rigid concurrency permit that "
            "was deactivated.")))
  {
    tcmReleasePermit(phS);
    return clear_state();
  }

  r = tcmReleasePermit(phS);
  if (!check_success(r, "tcmReleasePermit static"))
    return clear_state();

  r = tcmDisconnect(clid);
  if (!check_success(r, "tcmDisconnect"))
    return test_fail(test_name);

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

  tcm_permit_request_t reqA = make_request(total_number_of_threads / 2, total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads;
  r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check_success(r, "tcmRequestPermit for client A") && check_permit(eA, pA)))
    return test_fail(test_name);

  renegotiating_permits = {&phA};
  tcm_permit_request_t reqB = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads / 2;
  eB.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check_success(r, "tcmRequestPermit for client B")
        && check_permit(eA, phA) && check_permit(eB, pB))
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation"))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phA, &pA),
             "Reading data from permit " + std::to_string(uintptr_t(phA))))
    return test_fail(test_name);

  tcm_permit_request_t reqC = make_request(total_number_of_threads, total_number_of_threads);
  eC.concurrencies[0] = 0;
  eC.state = TCM_PERMIT_STATE_PENDING;
  r = tcmRequestPermit(clidC, reqC, &phC, &phC, &pC);
  if (!(check_success(r, "tcmRequestPermit for client C") && check_permit(eC, pC)))
    return test_fail(test_name);

  tcm_permit_request_t reqD = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eD.concurrencies[0] = 0;
  eD.state = TCM_PERMIT_STATE_PENDING;
  r = tcmRequestPermit(clidD, reqD, &phD, &phD, &pD);
  if (!(check_success(r, "tcmRequestPermit for client D") && check_permit(eA, phA)
      && check_permit(eB, phB) && check_permit(eC, phC) && check_permit(eD, pD)))
    return test_fail(test_name);

  renegotiating_permits = {&phA, &phC, &phD};
  eD.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eD.state = TCM_PERMIT_STATE_ACTIVE;
  r = tcmReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "tcmReleasePermit for client B")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phD, &pD),
             "Reading data from permit " + std::to_string(uintptr_t(phD))))
    return test_fail(test_name);

  renegotiating_permits = {&phC, &phD};
  reqA = make_request(total_number_of_threads, total_number_of_threads);
  eA.concurrencies[0] = 0; eA.state = TCM_PERMIT_STATE_PENDING;
  eD.concurrencies[0] = total_number_of_threads;
  r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "tcmRequestPermit for client A (re-requesting)")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  if (!check_success(tcmGetPermitData(phD, &pD),
                     "Reading data from permit " + std::to_string(uintptr_t(phD))))
    return test_fail(test_name);

  renegotiating_permits = {&phC, &phD};
  r = tcmReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check_success(r, "tcmReleasePermit for client A")
        && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return test_fail(test_name);

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;
  eC.state = TCM_PERMIT_STATE_ACTIVE;
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
