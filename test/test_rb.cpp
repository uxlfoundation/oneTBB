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

int32_t compute_concurrency(int32_t total_demand, uint32_t demand) {
  int32_t distributed_threads = total_number_of_threads;
  if (total_demand < total_number_of_threads)
    distributed_threads = total_demand;
  int32_t numerator = distributed_threads * demand;
  int32_t permit = numerator / total_demand;
  int32_t remainder = numerator % total_demand;

  if (remainder >= total_demand - remainder)
    permit += 1;
  return permit;
}

// Given a set of demands the function returns a set of permits (no order
// implied) that should be given by the Thread Composability Manager.
std::vector<uint32_t> compute_concurrencies(const std::vector<int32_t> demands,
                                            int32_t available_threads = total_number_of_threads)
{
  uint32_t total_demand = 0;
  for(std::size_t i = 0; i < demands.size(); ++i) {
    total_demand += demands[i];
  }
  std::vector<uint32_t> concurrencies(demands.size(), 0);
  for (std::size_t i = 0; i < demands.size(); ++i) {
    int32_t concurrency = compute_concurrency(total_demand, demands[i]);
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
    permits[i] = zerm_permit_t{&permits_concurrencies[i], nullptr};
  }

  int i = 0;
  std::vector<uint32_t> missing;

  for (const auto& ph : phs) {
    result &= check(zermGetPermitData(ph, &permits[i]) == ZE_RESULT_SUCCESS,
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
  check(true, "\n\nbegin test_nested_clients");

  renegotiating_permits = {};    // no renegotiation is expected
  zerm_client_id_t clidA,  clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads;

  zerm_permit_t pA{&pA_concurrency, nullptr}, pB{&pB_concurrency, nullptr},
              eA{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A") && check_permit(eA, pA)))
    return check(false, "end test_nested_clients");

  r = zermRegisterThread(phA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread A") &&
        check_permit(eA, phA)))
    return check(false, "end test_nested_clients");

  renegotiating_permits = {&phA};
  zerm_permit_t eB = {nullptr, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rB = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return check(false, "end test_nested_clients");

  r = zermRegisterThread(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        check_permit(eA, phA, skip_concurrenency_check)))
    return check(false, "end test_nested_clients");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        check_permit(eA, phA, skip_concurrenency_check)))
    return check(false, "end test_nested_clients");

  renegotiating_permits = {&phA};
  eA.concurrencies[0] = total_number_of_threads;

  r = zermReleasePermit(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B") &&
        check_permit(eA, phA) && renegotiating_permits.size() == 0))
    return check(false, "end test_nested_clients");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread A") &&
        check_permit(eA, phA)))
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
           eA_concurrency = total_number_of_threads/2;

  zerm_permit_t pA{&pA_concurrency, nullptr}, pB{&pB_concurrency, nullptr},
              eA{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rA = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermRegisterThread(phA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread A") && check_permit(eA, phA)))
    return check(false, "end test_nested_clients_partial_consumption");

  renegotiating_permits = {&phA};
  zerm_permit_t eB = {nullptr, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rB = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B all threads") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermRegisterThread(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRegisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check)))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread B") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check)))
    return check(false, "end test_nested_clients_partial_consumption");

  renegotiating_permits = {&phA};
  if (!check(zermGetPermitData(phA, &pA) == ZE_RESULT_SUCCESS,
            "Reading permit " + std::to_string(uintptr_t(phA)))) {
    return check(false, "end test_nested_clients_partial_consumption");
  }
  eA.concurrencies[0] = total_number_of_threads/2;

  r = zermReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B") &&
        check_permit(eA, phA) && renegotiating_permits == unchanged_permits))
    return check(false, "end test_nested_clients_partial_consumption");

  r = zermUnregisterThread();
  if (!(check(r == ZE_RESULT_SUCCESS, "zermUnregisterThread A")) && check_permit(eA, phA))
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

  renegotiating_permits = {};   // no renegotiation is expected
  zerm_client_id_t clidA, clidB;

  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect A"))
    return false;

  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect B"))
    return false;

  zerm_permit_handle_t phA = nullptr, phB = nullptr;

  uint32_t pA_concurrency, pB_concurrency,
           eA_concurrency = total_number_of_threads;

  zerm_permit_t pA{&pA_concurrency, nullptr}, pB{&pB_concurrency, nullptr},
              eA{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A all threads") &&
        check_permit(eA, pA)))
    return check(false, "end test_overlapping_clients");

  renegotiating_permits = {&phA};
  uint32_t eB_concurrency;
  zerm_permit_t eB = {&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rB = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B all threads") &&
        check_permits_concurrencies({phA, phB}, {rA.max_sw_threads, rB.max_sw_threads}) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, pB, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return check(false, "end test_overlapping_clients");

  renegotiating_permits = {&phB};
  eB.concurrencies[0] = total_number_of_threads;

  r = zermReleasePermit(phA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit A") &&
        check_permit(eB, phB) && renegotiating_permits.size() == 0))
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

  renegotiating_permits = {};   // no renegotiation is expected
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
           eA_concurrency = total_number_of_threads/2;

  zerm_permit_t pA{&pA_concurrency, nullptr},
              pB{&pB_concurrency, nullptr},
              pC{&pC_concurrency, nullptr},
              eA{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rA = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit A half threads") &&
        check_permit(eA, pA)))
    return check(false, "end test_overlapping_clients_two_callbacks");

  uint32_t eB_concurrency = total_number_of_threads/2;
  zerm_permit_t eB{&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rB = make_request(0, total_number_of_threads / 2);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit B half threads") &&
        check_permit(eA, phA) && check_permit(eB, pB)))
    return check(false, "end test_overlapping_clients_two_callbacks");

  renegotiating_permits = {&phA, &phB};
  std::vector<int32_t> demands = {
    total_number_of_threads/2, total_number_of_threads/2, total_number_of_threads
  };

  uint32_t eC_concurrency;
  zerm_permit_t eC = {&eC_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rC = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidC, rC, &phC, &phC, &pC);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phB, &pB}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit C all threads") &&
        check_permits_concurrencies({phA, phB, phC}, demands) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check) &&
        check_permit(eC, phC, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return check(false, "end test_overlapping_clients_two_callbacks");

  // When A is released, we should get a callback for others only if their
  // permits has changed.
  renegotiating_permits = {&phB, &phC};
  check(zermGetPermitData(phB, &pB) == ZE_RESULT_SUCCESS, "Reading permit phB");
  check(zermGetPermitData(phC, &pC) == ZE_RESULT_SUCCESS, "Reading permit phC");

  demands = {total_number_of_threads/2, total_number_of_threads};
  r = zermReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phB, &pB}, {&phC, &pC}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit A") &&
        check_permits_concurrencies({phB, phC}, demands) &&
        check_permit(eB, phB, skip_concurrenency_check) &&
        check_permit(eC, phC, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return check(false, "end test_overlapping_clients_two_callbacks");

  // When B is released, we should get a second callback for pC:
  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;

  r = zermReleasePermit(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit B") &&
        check_permit(eC, phC) && renegotiating_permits.size() == 0))
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

  renegotiating_permits = {};   // no renegotiation is expected

  zerm_client_id_t clidA;
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA{&pA_concurrency, nullptr},
              eA{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);

  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return check(false, "end test_partial_release");

  // Release some of the resources by re-requesting for less
  eA.concurrencies[0] = rA.max_sw_threads = total_number_of_threads/2;
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (re-request client A)") &&
        check_permit(eA, pA)))
    return check(false, "end test_partial_release");

  zerm_client_id_t clidB;
  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client B)"))
    return false;

  zerm_permit_handle_t phB = nullptr;
  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;

  zerm_permit_t pB{&pA_concurrency, nullptr},
              eB{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rB = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client B)") &&
        check_permit(eA, phA) && check_permit(eB, phB)))
    return check(false, "end test_partial_release");

  // Renegotiation should not happen for permit A since it was fully satisfied
  // already

  r = zermReleasePermit(phB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit (client B)") &&
      check_permit(eA, phA))
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
  zerm_client_id_t clidA;
  ze_result_t r = zermConnect(client_renegotiate, &clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client A)"))
    return false;

  zerm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads;

  zerm_permit_t pA{&pA_concurrency, nullptr},
              eA{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rA = make_request(0, total_number_of_threads);
  r = zermRequestPermit(clidA, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client A)") &&
        check_permit(eA, pA)))
    return check(false, "end test_permit_reactivation");

  eA.state = ZERM_PERMIT_STATE_INACTIVE;
  r = zermDeactivatePermit(phA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermDeactivatePermit (client A)") &&
        check_permit(eA, phA)))
    return check(false, "end test_permit_reactivation");

  zerm_client_id_t clidB;
  r = zermConnect(client_renegotiate, &clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermConnect (client B)"))
    return false;
  
  zerm_permit_handle_t phB = nullptr;
  uint32_t pB_concurrency, eB_concurrency = total_number_of_threads/2;

  zerm_permit_t pB{&pB_concurrency, nullptr},
              eB{&eB_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rB = make_request(0, total_number_of_threads/2);
  r = zermRequestPermit(clidB, rB, &phB, &phB, &pB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit (client B)") &&
        check_permit(eB, pB)))
    return check(false, "end test_permit_reactivation");

  // Activate previously deactivated request from client A. Since the amount of
  // previously held resources are not available, the renegotiation mechanism
  // should take place for client A, but its callback should not be invoked.
  // Since, however, the renegotiation should happen for client B as well,
  // its callback should be invoked.
  renegotiating_permits = {&phB};

  auto demands = {total_number_of_threads, total_number_of_threads/2};
  skip_checks_t skip_concurrenency_check = {};
  skip_concurrenency_check.concurrency = true;
  eA.state = ZERM_PERMIT_STATE_ACTIVE;

  r = zermActivatePermit(phA);
  auto unchanged_permits = list_unchanged_permits({{&phB, &pB}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermActivatePermit (client A)") &&
        check_permits_concurrencies({phA, phB}, demands) &&
        check_permit(eA, phA, skip_concurrenency_check) &&
        check_permit(eB, phB, skip_concurrenency_check) &&
        renegotiating_permits == unchanged_permits))
    return check(false, "end test_permit_reactivation");

  renegotiating_permits = {&phA};

  r = zermReleasePermit(phB);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit (client B)") &&
        check_permit(eA, phA) && renegotiating_permits.size() == 0))
    return check(false, "end test_permit_reactivation");

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit (client A)"))
    return check(false, "end test_permit_reactivation");

  r = zermDisconnect(clidA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect A"))
    return check(false, "end test_permit_reactivation");

  r = zermDisconnect(clidB);
  if (!check(r == ZE_RESULT_SUCCESS, "zermDisconnect B"))
    return check(false, "end test_permit_reactivation");

  return check(true, "end test_permit_reactivation");
}

std::atomic<bool> allow_renegotiation{false};
std::atomic<bool> is_callback_invoked{false};
zerm_permit_handle_t phS{nullptr};

bool test_static_permit() {
  check(true, "\n\nbegin test_static_permit");
  zerm_client_id_t clid;

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

  zerm_permit_handle_t phA = nullptr;
  uint32_t pA_concurrency, eA_concurrency = total_number_of_threads/2;

  zerm_permit_t pA{&pA_concurrency, nullptr},
              eA{&eA_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, 0};

  zerm_permit_request_t rA = make_request(0, total_number_of_threads/2);

  r = zermRequestPermit(clid, rA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit regular") &&
        check_permit(eA, pA)))
    return check(false, "end test_static_permit");

  // Request that shouldn't be renegotiated in active state
  zerm_permit_flags_t rigid_concurrency_flags{};
  rigid_concurrency_flags.rigid_concurrency = true;
  zerm_permit_request_t rS = make_request(0, total_number_of_threads, rigid_concurrency_flags);

  auto c = compute_concurrency(total_number_of_threads*3/2,
                               total_number_of_threads);

  uint32_t pS_concurrency, eS_concurrency = c;

  zerm_permit_t pS{&pS_concurrency, nullptr},
                eS{&eS_concurrency, nullptr, 1, ZERM_PERMIT_STATE_ACTIVE, rigid_concurrency_flags};

  r = zermRequestPermit(clid, rS, &phS, &phS, &pS);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit static") &&
        check_permit(eS, pS)))
    return check(false, "end test_static_permit");

  check(is_callback_invoked, "Check renegotiation for the regular permit.");

  is_callback_invoked = false;

  r = zermReleasePermit(phA);
  if (!check(r == ZE_RESULT_SUCCESS, "zermReleasePermit regular"))
    return check(false, "end test_static_permit");

  check(!is_callback_invoked,
        "Check static permit does not participates in the renegotiation"
        " while in active state.");

  // Check that renegotiation takes place when the static permit transferred to
  // the IDLE state, but its callback is not invoked.
  allow_renegotiation = true;
  eS.concurrencies[0] = total_number_of_threads;
  eS.state = ZERM_PERMIT_STATE_IDLE;

  r = zermIdlePermit(phS);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermIdlePermit static") &&
        check_permit(eS, phS)))
    return check(false, "end test_static_permit");

  if (!check(!is_callback_invoked, "Callback shouldn't be invoked after "
             "renegotiating permit that switched to idle state."))
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

  zerm_permit_request_t reqA = make_request(total_number_of_threads / 2, total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client A") && check_permit(eA, pA)))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phA};
  zerm_permit_request_t reqB = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eA.concurrencies[0] = total_number_of_threads / 2;
  eB.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
  auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client B")
        && check_permit(eA, phA) && check_permit(eB, pB))
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation"))
    return check(false, "end test_support_for_pending_state");

  if (!check(zermGetPermitData(phA, &pA) == ZE_RESULT_SUCCESS,
             "Reading data from permit " + std::to_string(uintptr_t(phA))))
    return check(false, "end test_support_for_pending_state");

  zerm_permit_request_t reqC = make_request(total_number_of_threads, total_number_of_threads);
  eC.concurrencies[0] = 0;
  eC.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidC, reqC, &phC, &phC, &pC);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client C") && check_permit(eC, pC)))
    return check(false, "end test_support_for_pending_state");

  zerm_permit_request_t reqD = make_request(total_number_of_threads - total_number_of_threads / 2,
                                            total_number_of_threads);
  eD.concurrencies[0] = 0;
  eD.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidD, reqD, &phD, &phD, &pD);
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client D") && check_permit(eA, phA)
      && check_permit(eB, phB) && check_permit(eC, phC) && check_permit(eD, pD)))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phA, &phC, &phD};
  eD.concurrencies[0] = total_number_of_threads - total_number_of_threads / 2;
  eD.state = ZERM_PERMIT_STATE_ACTIVE;
  r = zermReleasePermit(phB);
  unchanged_permits = list_unchanged_permits({{&phA, &pA}, {&phC, &pC}, {&phD, &pD}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for client B")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return check(false, "end test_pending_permit_processing");

  if (!check(zermGetPermitData(phD, &pD) == ZE_RESULT_SUCCESS,
             "Reading data from permit " + std::to_string(uintptr_t(phD))))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phC, &phD};
  reqA = make_request(total_number_of_threads, total_number_of_threads);
  eA.concurrencies[0] = 0;
  eD.concurrencies[0] = total_number_of_threads;
  eA.state = ZERM_PERMIT_STATE_PENDING;
  r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermRequestPermit for client A (re-requesting)")
        && check_permit(eA, phA) && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return check(false, "end test_support_for_pending_state");

  if (!check(zermGetPermitData(phD, &pD) == ZE_RESULT_SUCCESS,
             "Reading data from permit " + std::to_string(uintptr_t(phD))))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phC, &phD};
  r = zermReleasePermit(phA);
  unchanged_permits = list_unchanged_permits({{&phC, &pC}, {&phD, &pD}});
  if (!(check(r == ZE_RESULT_SUCCESS, "zermReleasePermit for client A")
        && check_permit(eC, phC) && check_permit(eD, phD)
        && check(renegotiating_permits == unchanged_permits, "Check incorrect permit renegotiation")))
    return check(false, "end test_support_for_pending_state");

  renegotiating_permits = {&phC};
  eC.concurrencies[0] = total_number_of_threads;
  eC.state = ZERM_PERMIT_STATE_ACTIVE;
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
  res &= test_static_permit();
  res &= test_support_for_pending_state();

  return int(!res);
}
