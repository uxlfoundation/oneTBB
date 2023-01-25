/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#include "tcm/detail/_tcm_assert.h"
#include "common_tests.h"
#include "hwloc_test_utils.h"
#include "test_utils.h"

#include "tcm.h"

#include <hwloc/bitmap.h>
#include <iostream>
#include <memory>
#include <string>

// ============================================================================

//! TODO: Implement the relevant logic for the getting of the processor groups number
uint32_t get_num_proc_groups() {
    return 1;
}

// ============================================================================
// HWLOC mask utility

struct process_mask {
  zerm_cpu_mask_t operator()() {
    auto& topology_ptr = tcm_test::system_topology::instance();
    return topology_ptr.allocate_process_affinity_mask();
  }
};

struct first_core_mask {
  zerm_cpu_mask_t operator()() {
    auto& topology_ptr = tcm_test::system_topology::instance();
    auto& topology = topology_ptr.get_topology();

    uint32_t depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_CORE);
    // Get first core
    auto obj = hwloc_get_obj_by_depth(
      topology, depth, 0 /* hwloc_get_nbobjs_by_depth(topology, depth) - 1*/);
    if (obj) {
      // Get a copy of its cpuset that we may modify
      hwloc_cpuset_t cpuset = hwloc_bitmap_dup(obj->cpuset);
      return cpuset;
    }
    return nullptr;
  }
};

struct mask_deleter {
  void operator()(zerm_cpu_mask_t* mask) {
    hwloc_bitmap_free(*mask);
  }
};

// ============================================================================
// Single request

template <typename MaskGenerator>
struct test_one_constrained_request {
  std::string test_name;
  uint32_t concurrency = total_number_of_threads;
  MaskGenerator get_mask{};

  bool operator()() {
    test_prolog(test_name);
    zerm_client_id_t clid;

    ze_result_t r = zermConnect(nullptr, &clid);
    if (!check_success(r, "zermConnect"))
      return test_fail(test_name);

    auto r_mask = get_mask();
    auto e_mask = get_mask();
    auto p_mask = hwloc_bitmap_alloc();

    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> requested_mask(&r_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> expected_mask(&e_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> permit_mask(&p_mask);

    zerm_permit_handle_t ph{nullptr};
    uint32_t p_concurrency;
    zerm_permit_t p = make_void_permit(&p_concurrency, permit_mask.get(), /*size*/1);

    uint32_t e_concurrency = concurrency;
    zerm_permit_t e = make_active_permit(&e_concurrency, expected_mask.get(), /*size*/1);

    zerm_permit_request_t req = make_request(0, concurrency);
    req.constraints_size = 1;
    zerm_cpu_constraints_t cpu_mask = ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_mask.min_concurrency = 0;
    cpu_mask.max_concurrency = concurrency;
    cpu_mask.mask = *requested_mask.get();
    req.cpu_constraints = &cpu_mask;

    r = zermRequestPermit(clid, req, nullptr, &ph, &p);
    if (!(check_success(r, "zermRequestPermit") && check_permit(e, p)))
      return test_fail(test_name);

    r = zermReleasePermit(ph);
    if (!check_success(r, "zermReleasePermit"))
      return test_fail(test_name);

    r = zermDisconnect(clid);
    if (!check_success(r, "zermDisconnect"))
      return test_fail(test_name);

    return test_epilog(test_name);
  }
};

bool test_one_constrained_request_full_mask() {
  test_one_constrained_request<process_mask> test =
    {"test_one_constrained_request_full_mask"};
  return test();
}

bool test_one_constrained_request_first_core_mask() {
  test_one_constrained_request<first_core_mask> test =
    {"test_one_constrained_request_first_core_mask"};
  return test();
}

// ============================================================================
// Two requests

template <typename MaskGeneratorA, typename MaskGeneratorB>
struct test_two_constrained_requests {
  std::string test_name;
  uint32_t concurrencyA = total_number_of_threads / 2;
  uint32_t concurrencyB = total_number_of_threads - total_number_of_threads / 2;
  MaskGeneratorA get_maskA{};
  MaskGeneratorB get_maskB{};

  bool operator()() {
    test_prolog(test_name);
    zerm_client_id_t clidA;
    zerm_client_id_t clidB;

    ze_result_t r = zermConnect(client_renegotiate, &clidA);
    if (!check_success(r, "zermConnect for client A"))
      return test_fail(test_name);

    r = zermConnect(client_renegotiate, &clidB);
    if (!check_success(r, "zermConnect for client B"))
      return test_fail(test_name);

    auto rA_mask = get_maskA();
    auto rB_mask = get_maskB();
    auto eA_mask = get_maskA();
    auto eB_mask = get_maskB();
    auto pA_mask = hwloc_bitmap_alloc();
    auto pB_mask = hwloc_bitmap_alloc();

    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> requested_maskA(&rA_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> requested_maskB(&rB_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> expected_maskA(&eA_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> expected_maskB(&eB_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> permit_maskA(&pA_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> permit_maskB(&pB_mask);

    zerm_permit_handle_t phA{nullptr};
    zerm_permit_handle_t phB{nullptr};
    uint32_t pA_concurrency;
    uint32_t pB_concurrency;
    zerm_permit_t pA = make_void_permit(&pA_concurrency, permit_maskA.get(), 1);
    zerm_permit_t pB = make_void_permit(&pB_concurrency, permit_maskB.get(), 1);

    uint32_t eA_concurrency = concurrencyA;
    uint32_t eB_concurrency = concurrencyB;
    zerm_permit_t eA = make_active_permit(&eA_concurrency, expected_maskA.get());
    zerm_permit_t eB = make_active_permit(&eB_concurrency, expected_maskB.get());

    zerm_cpu_constraints_t cpu_maskA = ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskA.min_concurrency = 0;
    cpu_maskA.max_concurrency = concurrencyA;
    cpu_maskA.mask = *requested_maskA.get();
    zerm_permit_request_t reqA = make_request(0, int32_t(concurrencyA), &cpu_maskA, /*size*/1);

    zerm_cpu_constraints_t cpu_maskB = ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskB.min_concurrency = 0;
    cpu_maskB.max_concurrency = concurrencyB;
    cpu_maskB.mask = *requested_maskB.get();
    zerm_permit_request_t reqB = make_request(0, int32_t(concurrencyB), &cpu_maskB, /*size*/1);

    r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
    if (!(check_success(r, "zermRequestPermit for client A") && check_permit(eA, pA)))
      return test_fail(test_name);

    r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
    if (!(check_success(r, "zermRequestPermit for client B") && check_permit(eB, pB)))
      return test_fail(test_name);

    renegotiating_permits = {&phB};
    r = zermReleasePermit(phA);
    auto unchanged_permits = list_unchanged_permits({{&phB, &pB}});
    if (!check_success(r, "zermReleasePermit for client A") &&
        !check(renegotiating_permits == unchanged_permits,
               "Incorrect renegotiation during permit A release"))
      return test_fail(test_name);

    r = zermReleasePermit(phB);
    if (!check_success(r, "zermReleasePermit for client B"))
      return test_fail(test_name);

    r = zermDisconnect(clidA);
    if (!check_success(r, "zermDisconnect for client A"))
      return test_fail(test_name);

    r = zermDisconnect(clidB);
    if (!check_success(r, "zermDisconnect for client B"))
      return test_fail(test_name);

    return test_epilog(test_name);
  }
};

bool test_two_constrained_requests_full_overlapping_mask() {
  test_two_constrained_requests<process_mask, process_mask> test =
    {"test_two_constrained_requests_full_overlapping_mask"};
  return test();
}

template <typename MaskGeneratorA, typename MaskGeneratorB>
struct test_two_constrained_requests_oversubscribe {

  std::string test_name;
  uint32_t concurrencyA = tcm_oversubscription_factor;
  uint32_t concurrencyB = tcm_oversubscription_factor;
  MaskGeneratorA get_maskA{};
  MaskGeneratorB get_maskB{};

  bool operator()() {
    test_prolog(test_name);

    zerm_client_id_t clidA;
    zerm_client_id_t clidB;

    ze_result_t r = zermConnect(client_renegotiate, &clidA);
    if (!check_success(r, "zermConnect for client A"))
      return test_fail(test_name);

    r = zermConnect(client_renegotiate, &clidB);
    if (!check_success(r, "zermConnect for client B"))
      return test_fail(test_name);

    auto rA_mask = get_maskA();
    auto rB_mask = get_maskB();
    auto eA_mask = get_maskA();
    auto eB_mask = get_maskB();
    auto pA_mask = hwloc_bitmap_alloc();
    auto pB_mask = hwloc_bitmap_alloc();

    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> requested_maskA(&rA_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> requested_maskB(&rB_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> expected_maskA(&eA_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> expected_maskB(&eB_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> permit_maskA(&pA_mask);
    std::unique_ptr<zerm_cpu_mask_t, mask_deleter> permit_maskB(&pB_mask);

    zerm_permit_handle_t phA{nullptr};
    zerm_permit_handle_t phB{nullptr};
    uint32_t pA_concurrency;
    uint32_t pB_concurrency;
    zerm_permit_t pA = make_permit(&pA_concurrency, permit_maskA.get(), /*size*/1);
    zerm_permit_t pB = make_permit(&pB_concurrency, permit_maskB.get(), /*size*/1);

    uint32_t eA_concurrency = concurrencyA;
    uint32_t eB_concurrency = concurrencyB;
    zerm_permit_t eA = make_active_permit(&eA_concurrency, expected_maskA.get());
    zerm_permit_t eB = make_active_permit(&eB_concurrency, expected_maskB.get());

    zerm_permit_request_t reqA = ZERM_PERMIT_REQUEST_INITIALIZER;
    reqA.min_sw_threads = 0; reqA.max_sw_threads = int32_t(concurrencyA);
    zerm_cpu_constraints_t cpu_maskA = ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskA.min_concurrency = 0; cpu_maskA.max_concurrency = concurrencyA;
    cpu_maskA.mask = *requested_maskA.get();
    reqA.cpu_constraints = &cpu_maskA; reqA.constraints_size = 1;

    r = zermRequestPermit(clidA, reqA, &phA, &phA, &pA);
    if (!(check_success(r, "zermRequestPermit for client A") && check_permit(eA, pA)))
      return test_fail(test_name);

    zerm_permit_request_t reqB = ZERM_PERMIT_REQUEST_INITIALIZER;
    reqB.min_sw_threads = int32_t(concurrencyB); reqB.max_sw_threads = int32_t(concurrencyB);
    zerm_cpu_constraints_t cpu_maskB = ZERM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskB.min_concurrency = concurrencyB; cpu_maskB.max_concurrency = concurrencyB;
    cpu_maskB.mask = *requested_maskB.get();
    reqB.cpu_constraints = &cpu_maskB; reqB.constraints_size = 1;

    renegotiating_permits = {&phA};
    r = zermRequestPermit(clidB, reqB, &phB, &phB, &pB);
    if (!check_success(r, "zermRequestPermit for client B"))
      return test_fail(test_name);

    auto unchanged_permits = list_unchanged_permits({{&phA, &pA}});
    eA_concurrency = 0;
    if (!(check_permit(eB, pB) && check_permit(eA, phA) &&
          check(renegotiating_permits == unchanged_permits,
                "Renegotiation done for client A's permit.")))
      // TODO: instead of comparing "renegotiating_permits ==
      // unchanged_permits", add has_invoked_callback function
      return test_fail(test_name);

    // TODO: alter test expectations depending on test parameters (e.g.
    // MaskGenerator)
    renegotiating_permits = {&phB};
    r = zermReleasePermit(phA);
    if (!check_success(r, "zermReleasePermit for client A"))
      return test_fail(test_name);

    unchanged_permits = list_unchanged_permits({{&phB, &pB}});
    if (!check(renegotiating_permits == unchanged_permits,
               "Incorrect renegotiation during permit A release"))
      return test_fail(test_name);

    r = zermReleasePermit(phB);
    if (!check_success(r, "zermReleasePermit for client B"))
      return test_fail(test_name);

    r = zermDisconnect(clidA);
    if (!check_success(r, "zermDisconnect for client A"))
      return test_fail(test_name);

    r = zermDisconnect(clidB);
    if (!check_success(r, "zermDisconnect for client B"))
      return test_fail(test_name);

    return test_epilog(test_name);
  }
};

bool test_two_constrained_requests_oversubscribe_single_core(tcm_test::system_topology& /*tp*/) {
  test_two_constrained_requests_oversubscribe<first_core_mask, first_core_mask> test =
    {"test_two_constrained_requests_oversubscribe_first_core"};
  return test();
}

//bool test_two_constrained_request_not_overlapping_mask() {
//  test_two_constrained_request<first_core_mask> test =
//    {"test_single_constrained_request_single_core_mask"};
//  return test();
//}

int main() {
  bool res = true;

  // TODO: Consider making a topology instance global variable
  tcm_test::system_topology::construct(get_num_proc_groups());
  auto& tp = tcm_test::system_topology::instance();

  res &= test_one_constrained_request_full_mask();
  res &= test_one_constrained_request_first_core_mask();
  res &= test_two_constrained_requests_full_overlapping_mask();
  res &= test_two_constrained_requests_oversubscribe_single_core(tp);

  tcm_test::system_topology::destroy();

  const bool has_failed = (false == res);
  return int(has_failed);
}
