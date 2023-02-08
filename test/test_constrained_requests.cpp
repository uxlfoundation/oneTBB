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
  tcm_cpu_mask_t operator()() {
    auto& topology_ptr = tcm_test::system_topology::instance();
    return topology_ptr.allocate_process_affinity_mask();
  }
};

struct first_core_mask {
  tcm_cpu_mask_t operator()() {
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
  void operator()(tcm_cpu_mask_t* mask) {
    hwloc_bitmap_free(*mask);
  }
};

// ============================================================================
// Single request

template <typename MaskGenerator>
struct test_one_constrained_request {
  std::string test_name;
  uint32_t concurrency = static_cast<uint32_t>(total_number_of_threads);
  MaskGenerator get_mask{};

  bool operator()() {
    test_prolog(test_name);
    tcm_client_id_t clid;

    tcm_result_t r = tcmConnect(nullptr, &clid);
    if (!check_success(r, "tcmConnect"))
      return test_fail(test_name);

    auto r_mask = get_mask();
    auto e_mask = get_mask();
    auto p_mask = hwloc_bitmap_alloc();

    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> requested_mask(&r_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_mask(&e_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask(&p_mask);

    tcm_permit_handle_t ph{nullptr};
    uint32_t p_concurrency;
    tcm_permit_t p = make_void_permit(&p_concurrency, permit_mask.get(), /*size*/1);

    uint32_t e_concurrency = concurrency;
    tcm_permit_t e = make_active_permit(&e_concurrency, expected_mask.get(), /*size*/1);

    tcm_permit_request_t req = make_request(0, concurrency);
    req.constraints_size = 1;
    tcm_cpu_constraints_t cpu_mask = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_mask.min_concurrency = 0;
    cpu_mask.max_concurrency = concurrency;
    cpu_mask.mask = *requested_mask.get();
    req.cpu_constraints = &cpu_mask;

    r = tcmRequestPermit(clid, req, nullptr, &ph, &p);
    if (!(check_success(r, "tcmRequestPermit") && check_permit(e, p)))
      return test_fail(test_name);

    r = tcmReleasePermit(ph);
    if (!check_success(r, "tcmReleasePermit"))
      return test_fail(test_name);

    r = tcmDisconnect(clid);
    if (!check_success(r, "tcmDisconnect"))
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
  uint32_t concurrencyA = static_cast<uint32_t>(total_number_of_threads / 2);
  uint32_t concurrencyB = static_cast<uint32_t>(total_number_of_threads - total_number_of_threads / 2);
  MaskGeneratorA get_maskA{};
  MaskGeneratorB get_maskB{};

  bool operator()() {
    test_prolog(test_name);
    tcm_client_id_t clidA;
    tcm_client_id_t clidB;

    tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
    if (!check_success(r, "tcmConnect for client A"))
      return test_fail(test_name);

    r = tcmConnect(client_renegotiate, &clidB);
    if (!check_success(r, "tcmConnect for client B"))
      return test_fail(test_name);

    auto rA_mask = get_maskA();
    auto rB_mask = get_maskB();
    auto eA_mask = get_maskA();
    auto eB_mask = get_maskB();
    auto pA_mask = hwloc_bitmap_alloc();
    auto pB_mask = hwloc_bitmap_alloc();

    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> requested_maskA(&rA_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> requested_maskB(&rB_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_maskA(&eA_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_maskB(&eB_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_maskA(&pA_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_maskB(&pB_mask);

    tcm_permit_handle_t phA{nullptr};
    tcm_permit_handle_t phB{nullptr};
    uint32_t pA_concurrency;
    uint32_t pB_concurrency;
    tcm_permit_t pA = make_void_permit(&pA_concurrency, permit_maskA.get(), 1);
    tcm_permit_t pB = make_void_permit(&pB_concurrency, permit_maskB.get(), 1);

    uint32_t eA_concurrency = concurrencyA;
    uint32_t eB_concurrency = concurrencyB;
    tcm_permit_t eA = make_active_permit(&eA_concurrency, expected_maskA.get());
    tcm_permit_t eB = make_active_permit(&eB_concurrency, expected_maskB.get());

    tcm_cpu_constraints_t cpu_maskA = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskA.min_concurrency = 0;
    cpu_maskA.max_concurrency = concurrencyA;
    cpu_maskA.mask = *requested_maskA.get();
    tcm_permit_request_t reqA = make_request(0, int32_t(concurrencyA), &cpu_maskA, /*size*/1);

    tcm_cpu_constraints_t cpu_maskB = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskB.min_concurrency = 0;
    cpu_maskB.max_concurrency = concurrencyB;
    cpu_maskB.mask = *requested_maskB.get();
    tcm_permit_request_t reqB = make_request(0, int32_t(concurrencyB), &cpu_maskB, /*size*/1);

    r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
    if (!(check_success(r, "tcmRequestPermit for client A") && check_permit(eA, pA)))
      return test_fail(test_name);

    r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
    if (!(check_success(r, "tcmRequestPermit for client B") && check_permit(eB, pB)))
      return test_fail(test_name);

    renegotiating_permits = {&phB};
    r = tcmReleasePermit(phA);
    auto unchanged_permits = list_unchanged_permits({{&phB, &pB}});
    if (!check_success(r, "tcmReleasePermit for client A") &&
        !check(renegotiating_permits == unchanged_permits,
               "Incorrect renegotiation during permit A release"))
      return test_fail(test_name);

    r = tcmReleasePermit(phB);
    if (!check_success(r, "tcmReleasePermit for client B"))
      return test_fail(test_name);

    r = tcmDisconnect(clidA);
    if (!check_success(r, "tcmDisconnect for client A"))
      return test_fail(test_name);

    r = tcmDisconnect(clidB);
    if (!check_success(r, "tcmDisconnect for client B"))
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
  uint32_t concurrencyA = static_cast<uint32_t>(tcm_oversubscription_factor);
  uint32_t concurrencyB = static_cast<uint32_t>(tcm_oversubscription_factor);
  MaskGeneratorA get_maskA{};
  MaskGeneratorB get_maskB{};

  bool operator()() {
    test_prolog(test_name);

    tcm_client_id_t clidA;
    tcm_client_id_t clidB;

    tcm_result_t r = tcmConnect(client_renegotiate, &clidA);
    if (!check_success(r, "tcmConnect for client A"))
      return test_fail(test_name);

    r = tcmConnect(client_renegotiate, &clidB);
    if (!check_success(r, "tcmConnect for client B"))
      return test_fail(test_name);

    auto rA_mask = get_maskA();
    auto rB_mask = get_maskB();
    auto eA_mask = get_maskA();
    auto eB_mask = get_maskB();
    auto pA_mask = hwloc_bitmap_alloc();
    auto pB_mask = hwloc_bitmap_alloc();

    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> requested_maskA(&rA_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> requested_maskB(&rB_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_maskA(&eA_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_maskB(&eB_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_maskA(&pA_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_maskB(&pB_mask);

    tcm_permit_handle_t phA{nullptr};
    tcm_permit_handle_t phB{nullptr};
    uint32_t pA_concurrency;
    uint32_t pB_concurrency;
    tcm_permit_t pA = make_permit(&pA_concurrency, permit_maskA.get(), /*size*/1);
    tcm_permit_t pB = make_permit(&pB_concurrency, permit_maskB.get(), /*size*/1);

    uint32_t eA_concurrency = concurrencyA;
    uint32_t eB_concurrency = concurrencyB;
    tcm_permit_t eA = make_active_permit(&eA_concurrency, expected_maskA.get());
    tcm_permit_t eB = make_active_permit(&eB_concurrency, expected_maskB.get());

    tcm_permit_request_t reqA = TCM_PERMIT_REQUEST_INITIALIZER;
    reqA.min_sw_threads = 0; reqA.max_sw_threads = int32_t(concurrencyA);
    tcm_cpu_constraints_t cpu_maskA = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskA.min_concurrency = 0; cpu_maskA.max_concurrency = concurrencyA;
    cpu_maskA.mask = *requested_maskA.get();
    reqA.cpu_constraints = &cpu_maskA; reqA.constraints_size = 1;

    r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
    if (!(check_success(r, "tcmRequestPermit for client A") && check_permit(eA, pA)))
      return test_fail(test_name);

    tcm_permit_request_t reqB = TCM_PERMIT_REQUEST_INITIALIZER;
    reqB.min_sw_threads = int32_t(concurrencyB); reqB.max_sw_threads = int32_t(concurrencyB);
    tcm_cpu_constraints_t cpu_maskB = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskB.min_concurrency = concurrencyB; cpu_maskB.max_concurrency = concurrencyB;
    cpu_maskB.mask = *requested_maskB.get();
    reqB.cpu_constraints = &cpu_maskB; reqB.constraints_size = 1;

    renegotiating_permits = {&phA};
    r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
    if (!check_success(r, "tcmRequestPermit for client B"))
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
    r = tcmReleasePermit(phA);
    if (!check_success(r, "tcmReleasePermit for client A"))
      return test_fail(test_name);

    unchanged_permits = list_unchanged_permits({{&phB, &pB}});
    if (!check(renegotiating_permits == unchanged_permits,
               "Incorrect renegotiation during permit A release"))
      return test_fail(test_name);

    r = tcmReleasePermit(phB);
    if (!check_success(r, "tcmReleasePermit for client B"))
      return test_fail(test_name);

    r = tcmDisconnect(clidA);
    if (!check_success(r, "tcmDisconnect for client A"))
      return test_fail(test_name);

    r = tcmDisconnect(clidB);
    if (!check_success(r, "tcmDisconnect for client B"))
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

bool test_allow_mask_omitting_during_permit_copy(tcm_test::system_topology& /*tp*/) {
    const char* test_name = "test_allow_omitting_mask_in_permit_copy";
    test_prolog(test_name);

    tcm_client_id_t client_id;
    auto r = tcmConnect(nullptr, &client_id);
    if (!check_success(r, "tcmConnect succeeded"))
        return test_fail(test_name);

    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> req_mask_guard(&constraints.mask);
    constraints.mask = hwloc_bitmap_alloc();
    hwloc_bitmap_set(constraints.mask, 1);
    auto req = make_request(0, total_number_of_threads, &constraints, /*size*/1);

    tcm_permit_handle_t ph{nullptr};
    uint32_t p_concurrency;
    // Check that TCM does not require space for the mask when copying the permit data
    tcm_permit_t p = make_void_permit(&p_concurrency);

    // since constraint's max_concurrency will be inferred to the mask concurrency during the request
    uint32_t e_concurrency = hwloc_bitmap_weight(constraints.mask);
    tcm_permit_t eP = make_active_permit(&e_concurrency);
    r = tcmRequestPermit(client_id, req, /*callback_arg*/nullptr, &ph, &p);
    if (!(check_success(r, "tcmRequestPermit succeeded") && check_permit(eP, p) &&
          check(!p.cpu_masks, "The mask has not been allocated by TCM in tcmRequestPermit")))
        return test_fail(test_name);

    r = tcmGetPermitData(ph, &p);
    if (!(check_success(r, "tcmGetPermitData succeeded") && check_permit(eP, p) &&
          check(!p.cpu_masks, "The mask has not been allocated by TCM in tcmGetPermitData")))
        return test_fail(test_name);

    tcm_cpu_mask_t mask = hwloc_bitmap_alloc();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask_guard(&mask);
    p.cpu_masks = &mask;
    if (!check(hwloc_bitmap_compare(mask, req.cpu_constraints->mask) != 0,
               "Just created and requested mask differs"))
        return test_fail(test_name);

    r = tcmGetPermitData(ph, &p);
    eP.cpu_masks = &req.cpu_constraints->mask; // Expecting the requested mask
    if (!(check_success(r, "tcmGetPermitData succeeded") && check_permit(eP, p),
          "The copied mask is equal to the requested"))
        return test_fail(test_name);

    r = tcmReleasePermit(ph);
    if (!check_success(r, "tcmReleasePermit succeeded"))
        return test_fail(test_name);

    r = tcmDisconnect(client_id);
    if (!check_success(r, "tcmDisconnect succeeded"))
        return test_fail(test_name);

    return test_epilog(test_name);
}

int main() {
  bool res = true;

  // TODO: Consider making a topology instance global variable
  tcm_test::system_topology::construct(get_num_proc_groups());
  auto& tp = tcm_test::system_topology::instance();

  res &= test_one_constrained_request_full_mask();
  res &= test_one_constrained_request_first_core_mask();
  res &= test_two_constrained_requests_full_overlapping_mask();
  res &= test_two_constrained_requests_oversubscribe_single_core(tp);
  res &= test_allow_mask_omitting_during_permit_copy(tp);

  tcm_test::system_topology::destroy();

  const bool has_failed = (false == res);
  return int(has_failed);
}
