/*
    Copyright (C) 2023-2026 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#include "tcm/detail/_tcm_assert.h"
#include "common_tests.h"
#include "hwloc_test_utils.h"
#include "test_utils.h"

#include "tcm.h"

#include <hwloc/bitmap.h>

#include <memory>
#include <algorithm>

// ============================================================================
// HWLOC mask utility

struct process_mask {
  tcm_cpu_mask_t operator()() {
    auto& topology_ptr = tcm_test::system_topology::instance();
    return topology_ptr.allocate_process_affinity_mask();
  }

  int32_t size() { return platform_hardware_concurrency(); }
};

class first_core_mask {
  int32_t weight;
  tcm_cpu_mask_t mask;

public:
  first_core_mask(): weight(0), mask(nullptr) {
    auto& topology_ptr = tcm_test::system_topology::instance();
    auto& topology = topology_ptr.get_topology();

    uint32_t depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_CORE);
    // Get first core
    auto obj = hwloc_get_obj_by_depth(
      topology, depth, 0 /* hwloc_get_nbobjs_by_depth(topology, depth) - 1*/);
    if (obj) {
      // Get a copy of its cpuset that we may modify
      mask = obj->cpuset;
      weight = hardware_concurrency(mask);
    }
    __TCM_ASSERT(obj, "Failed to get the HWLOC object representing the first core. "
                 "Has test been run on HW subset?");
    __TCM_ASSERT(weight > 0, "Invalid mask of the first core. Has test been run on a HW subset?");
  }

  tcm_cpu_mask_t operator()() const {
    tcm_cpu_mask_t m = allocate_cpu_mask();
    copy(m, mask);
    return m;
  }
  int32_t size() const { return weight; }
};

class first_parsed_numa_mask {
  int32_t weight;
  int32_t numa_id;
  tcm_cpu_mask_t mask;

public:
  first_parsed_numa_mask(): weight(0), mask(nullptr) {
    int32_t numa_count{0}, type_count{0};
    int32_t* numa_indexes{nullptr};
    int32_t* type_indexes{nullptr};

    auto& topology_ptr = tcm_test::system_topology::instance();
    topology_ptr.fill_topology_information(numa_count, numa_indexes,
                                           type_count, type_indexes);

    numa_id = numa_indexes[0];
    mask = allocate_cpu_mask();
    topology_ptr.fill_constraints_affinity_mask(
      mask,
      numa_id  /* first parsed numa index */,
      -1       /* no constraints by core type */,
      -1       /* no constraints by threads per core */);
    weight = hardware_concurrency(mask);
  }

  tcm_cpu_mask_t operator()() const {
    tcm_cpu_mask_t m = allocate_cpu_mask();
    copy(m, mask);
    return m;
  }

  int32_t size() { return weight; }
  int32_t id() { return numa_id; }
};

TEST("test_allow_mask_omitting_during_permit_copy") {
    tcm_client_id_t client_id = connect_new_client();

    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> req_mask_guard(&constraints.mask);
    constraints.mask = first_core_mask{}();
    auto req = make_request(tcm_automatic, tcm_automatic, &constraints, /*size*/1);

    tcm_permit_handle_t ph{nullptr};
    uint32_t p_concurrency;
    // Check that TCM does not require space for the mask when copying the permit data
    tcm_permit_t p = make_void_permit(&p_concurrency);

    // since constraint's max_concurrency will be inferred to the mask concurrency during the request
    uint32_t e_concurrency = tcm_concurrency(constraints.mask);
    tcm_permit_t eP = make_active_permit(&e_concurrency);
    tcm_result_t r = tcmRequestPermit(client_id, req, /*callback_arg*/nullptr, &ph, &p);
    check_success(r, "tcmRequestPermit succeeded");
    check_permit(eP, p);
    check(!p.cpu_masks, "The mask has not been allocated by TCM in tcmRequestPermit");

    r = tcmGetPermitData(ph, &p);
    check_success(r, "tcmGetPermitData for " + to_string(ph));
    check_permit(eP, p);
    check(!p.cpu_masks, "The mask has not been allocated by TCM in tcmGetPermitData");

    tcm_cpu_mask_t mask = allocate_cpu_mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask_guard(&mask);
    p.cpu_masks = &mask;
    check(!is_equal(mask, req.cpu_constraints->mask), "Just created and requested mask differs");

    r = tcmGetPermitData(ph, &p);
    eP.cpu_masks = &req.cpu_constraints->mask; // Expecting the requested mask
    check_success(r, "tcmGetPermitData for " + to_string(ph));
    check_permit(eP, p);

    r = tcmReleasePermit(ph);
    check_success(r, "tcmReleasePermit succeeded");

    disconnect_client(client_id);
}

// ============================================================================
// Single request

struct one_request_config {
  uint32_t* exp_concurrency;             // expected concurrency for client
  int32_t min_sw_threads;               // requested min_sw_threads for client
  int32_t max_sw_threads;               // requested max_sw_threads for client
  uint32_t constraints_size;            // size of array of constraints requested
  tcm_cpu_mask_t* per_mask;             // permit mask allocation
  tcm_cpu_mask_t* exp_mask;             // expected cpu mask
  tcm_cpu_constraints_t* constraints;  // requested constraints
};

struct test_one_request {
  one_request_config config{};

  void operator()() {
    tcm_client_id_t clid = connect_new_client();

    auto p_mask = config.per_mask;
    auto e_mask = config.exp_mask;

    tcm_permit_handle_t ph{nullptr};
    std::unique_ptr<uint32_t[]> p_concurrency{new uint32_t[config.constraints_size]};
    //tcm_permit_t p{p_concurrency.get(), p_mask, config.constraints_size};
    tcm_permit_t p = make_void_permit(p_concurrency.get(), p_mask, config.constraints_size);

    uint32_t* e_concurrency = config.exp_concurrency;
    tcm_permit_t e = make_active_permit(e_concurrency, e_mask, config.constraints_size);

    tcm_permit_request_t req = make_request(config.min_sw_threads, config.max_sw_threads);
    req.constraints_size = config.constraints_size;
    req.cpu_constraints = config.constraints;

    tcm_result_t r = tcmRequestPermit(clid, req, nullptr, &ph, &p);
    check_success(r, "tcmRequestPermit");
    check_permit(e, p);

    r = tcmReleasePermit(ph);
    check_success(r, "tcmReleasePermit");

    disconnect_client(clid);
  }
};

template <typename MaskGenerator>
struct test_one_request_low_level_constraints {
  void operator()() {
    MaskGenerator mask{};
    auto p_mask = allocate_cpu_mask();
    auto e_mask = mask();
    auto r_mask = mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask(&p_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_mask(&e_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> requested_mask(&r_mask);
    uint32_t available = uint32_t(tcm_oversubscription_factor * platform_hardware_concurrency());
    uint32_t requested = uint32_t(tcm_oversubscription_factor * mask.size());
    uint32_t expected = std::min(requested, available);

    tcm_cpu_constraints_t cpu_constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_constraints.min_concurrency = 0;
    cpu_constraints.max_concurrency = requested;
    cpu_constraints.mask = *requested_mask.get();

    one_request_config test_config{};
    test_config.exp_concurrency = &expected;
    test_config.min_sw_threads = 0;
    test_config.max_sw_threads = requested;
    test_config.constraints_size = 1;
    test_config.per_mask = permit_mask.get();
    test_config.exp_mask = expected_mask.get();
    test_config.constraints = &cpu_constraints;

    test_one_request{test_config}();
  }
};

TEST("Constrained request with process mask as low level constraint") {
  test_one_request_low_level_constraints<process_mask>{}();
}

TEST("Constrained request with first core mask as low level constraint") {
  test_one_request_low_level_constraints<first_core_mask>{}();
}

TEST("Constrained request with first numa node as low level constraint") {
  test_one_request_low_level_constraints<first_parsed_numa_mask>{}();
}

TEST("Constrained request with first numa node as high level constraint") {
  first_parsed_numa_mask mask{};
  auto p_mask = allocate_cpu_mask();
  auto e_mask = mask();
  std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask(&p_mask);
  std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_mask(&e_mask);

  uint32_t available = uint32_t(tcm_oversubscription_factor * platform_hardware_concurrency());
  uint32_t requested = uint32_t(tcm_oversubscription_factor * mask.size());
  uint32_t expected = std::min(requested, available);

  tcm_cpu_constraints_t cpu_constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
  cpu_constraints.min_concurrency = 0;
  cpu_constraints.max_concurrency = requested;
  cpu_constraints.mask = nullptr;
  cpu_constraints.numa_id = mask.id();

  one_request_config test_config{};
  test_config.exp_concurrency = &expected;
  test_config.min_sw_threads = 0;
  test_config.max_sw_threads = requested;
  test_config.constraints_size = 1;
  test_config.per_mask = permit_mask.get();
  test_config.exp_mask = expected_mask.get();
  test_config.constraints = &cpu_constraints;

  test_one_request{test_config}();
}

TEST("Constrained request with two process masks as low level constraints without oversubscription")
{
  process_mask mask{};
  const uint32_t size = 2;

  uint32_t total_concurrency = uint32_t(tcm_oversubscription_factor * mask.size());
  uint32_t e_concurrency[size] = { total_concurrency / 2,
                                   total_concurrency - total_concurrency / 2 };
  tcm_cpu_mask_t permit_mask[size];
  tcm_cpu_mask_t expected_mask[size];
  tcm_cpu_constraints_t cpu_constraints[size];
  int32_t min_sw_threads = 0;
  for (uint32_t i = 0; i < size; ++i) {
    permit_mask[i] = allocate_cpu_mask();
    expected_mask[i] = mask();
    cpu_constraints[i] = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_constraints[i].min_concurrency = e_concurrency[i];
    cpu_constraints[i].max_concurrency = e_concurrency[i];
    cpu_constraints[i].mask = mask();
    min_sw_threads += cpu_constraints[i].min_concurrency;
  }

  one_request_config test_config{};
  test_config.exp_concurrency = e_concurrency;
  test_config.min_sw_threads = min_sw_threads;
  test_config.max_sw_threads = total_concurrency;
  test_config.constraints_size = 2;
  test_config.per_mask = permit_mask;
  test_config.exp_mask = expected_mask;
  test_config.constraints = cpu_constraints;

  test_one_request{test_config}();

  for (uint32_t i = 0; i < size; ++i) {
    free_cpu_mask(permit_mask[i]);
    free_cpu_mask(expected_mask[i]);
    free_cpu_mask(cpu_constraints[i].mask);
  }
}

// ============================================================================
// Two requests

struct two_requests_config {
  tcm_callback_t callback;         // clients callback
  uint32_t exp_concurrencyA;        // expected concurrency for client A
  uint32_t exp_concurrencyB;        // expected concurrency for client B
  int32_t min_concurrencyA;         // requested min_concurrency for client A
  int32_t max_concurrencyA;         // requested max_concurrency for client A
  int32_t min_concurrencyB;         // requested min_concurrency for client B
  int32_t max_concurrencyB;         // requested max_concurrency for client B
  uint32_t new_concurrencyB;        // new concurrency for for client B after renegotiation
  tcm_permit_state_t cur_stateB;    // expected state for client B
  tcm_permit_state_t new_stateB;    // new state for client B after renegotiation
};

template <typename MaskGeneratorA, typename MaskGeneratorB>
struct test_two_requests {
  two_requests_config config;
  MaskGeneratorA mask_generatorA{};
  MaskGeneratorB mask_generatorB{};

  void operator()() {
    tcm_client_id_t clidA = connect_new_client(config.callback);
    tcm_client_id_t clidB = connect_new_client(config.callback);

    auto rA_mask = mask_generatorA();
    auto rB_mask = mask_generatorB();
    auto eA_mask = mask_generatorA();
    auto eB_mask = mask_generatorB();
    auto pA_mask = allocate_cpu_mask();
    auto pB_mask = allocate_cpu_mask();

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

    tcm_permit_t pA = make_void_permit(&pA_concurrency, permit_maskA.get(), /*size*/1);
    tcm_permit_t pB = make_void_permit(&pB_concurrency, permit_maskB.get(), /*size*/1);

    uint32_t eA_concurrency = config.exp_concurrencyA;
    uint32_t eB_concurrency = config.exp_concurrencyB;
    tcm_permit_t eA = make_active_permit(&eA_concurrency, expected_maskA.get());
    tcm_permit_t eB = make_active_permit(&eB_concurrency, expected_maskB.get());
    eB.state = config.cur_stateB;

    tcm_cpu_constraints_t cpu_maskA = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskA.min_concurrency = config.min_concurrencyA;
    cpu_maskA.max_concurrency = config.max_concurrencyA;
    cpu_maskA.mask = *requested_maskA.get();

    tcm_permit_request_t reqA = make_request(config.min_concurrencyA, config.max_concurrencyA, &cpu_maskA, 1);

    tcm_cpu_constraints_t cpu_maskB = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_maskB.min_concurrency = config.min_concurrencyB;
    cpu_maskB.max_concurrency = config.max_concurrencyB;
    cpu_maskB.mask = *requested_maskB.get();
    tcm_permit_request_t reqB = make_request(config.min_concurrencyB, config.max_concurrencyB, &cpu_maskB, 1);

    tcm_result_t r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
    check_success(r, "tcmRequestPermit for client A");
    check_permit(eA, pA);

    r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
    check_success(r, "tcmRequestPermit for client B");
    check_permit(eB, pB);

    renegotiating_permits = {phB};
    r = tcmReleasePermit(phA);
    eB.concurrencies[0] = config.new_concurrencyB;
    eB.state = config.new_stateB;
    auto unchanged_permits = list_unchanged_permits({{phB, &pB}});

    check_success(r, "tcmReleasePermit for client A");
    check_permit(eB, phB);
    check(renegotiating_permits == unchanged_permits,
          "Incorrect renegotiation during permit A release");

    r = tcmReleasePermit(phB);
    check_success(r, "tcmReleasePermit for client B");

    disconnect_client(clidA);
    disconnect_client(clidB);
  }
};

TEST("Two constrained requests with process mask as low level constraints without oversubscription") {
  uint32_t concurrencyA = platform_tcm_concurrency() / 2;
  uint32_t concurrencyB = platform_tcm_concurrency() - platform_tcm_concurrency() / 2;
  two_requests_config test_config{};
  test_config.callback = client_renegotiate;
  test_config.exp_concurrencyA = concurrencyA;
  test_config.exp_concurrencyB = concurrencyB;
  test_config.min_concurrencyA = 0;
  test_config.max_concurrencyA = concurrencyA;
  test_config.min_concurrencyB = 0;
  test_config.max_concurrencyB = concurrencyB;
  test_config.new_concurrencyB = concurrencyB;
  test_config.cur_stateB = TCM_PERMIT_STATE_ACTIVE;
  test_config.new_stateB = TCM_PERMIT_STATE_ACTIVE;

  test_two_requests<process_mask, process_mask>{test_config}();
}

TEST("Two constrained requests oversubscribing first core") {
  uint32_t concurrencyA = uint32_t(tcm_oversubscription_factor * first_core_mask{}.size());
  uint32_t concurrencyB = concurrencyA;
  two_requests_config test_config{};
  test_config.callback = client_renegotiate;
  test_config.exp_concurrencyA = concurrencyA;
  test_config.exp_concurrencyB = 0;
  test_config.min_concurrencyA = concurrencyA;
  test_config.max_concurrencyA = concurrencyA;
  test_config.min_concurrencyB = concurrencyB;
  test_config.max_concurrencyB = concurrencyB;
  test_config.new_concurrencyB = concurrencyB;
  test_config.cur_stateB = TCM_PERMIT_STATE_PENDING;
  test_config.new_stateB = TCM_PERMIT_STATE_ACTIVE;

  test_two_requests<first_core_mask, first_core_mask>{test_config}();
}

// ============================================================================
// Multiple requests

TEST("Request any NUMA node until all nodes are distributed + one more, correct negotiation when "
     "one is released")
{
  const skip_checks_t skip_concurrency_and_mask_checks{
      /*size*/false, /*concurrency*/true, /*state*/false, /*flags*/false, /*mask*/true
  };

  // parse all numa nodes
  int32_t numa_count{0}, type_count{0};
  int32_t* numa_indexes{nullptr};
  int32_t* type_indexes{nullptr};
  auto& topology_ptr = tcm_test::system_topology::instance();
  topology_ptr.fill_topology_information(numa_count, numa_indexes, type_count, type_indexes);

  // connect all clients
  tcm_result_t r = TCM_RESULT_ERROR_UNKNOWN;
  std::vector<tcm_client_id_t> client_ids(numa_count + 1);
  for (int i = 0; i < numa_count + 1; ++i) {
    client_ids[i] = connect_new_client(/*client callback*/nullptr);
  }

  // make permit requests
  std::vector<tcm_permit_request_t> requests(numa_count + 1);
  std::vector<tcm_cpu_constraints_t> constraints(numa_count + 1);
  for (int i = 0; i < numa_count + 1; ++i) {
    requests[i] = TCM_PERMIT_REQUEST_INITIALIZER;
    // Specify automatic for desired concurrency so that it is inferred when some NUMA node is
    // chosen.
    requests[i].min_sw_threads = 1; // not automatic as we don't want it to be inferred as zero
    requests[i].max_sw_threads = tcm_automatic;
    requests[i].constraints_size = 1;
    requests[i].cpu_constraints = &constraints[i];
    constraints[i] = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    constraints[i].numa_id = tcm_any;
  }

  // request all permits
  std::vector<tcm_permit_handle_t> permit_handles(numa_count + 1, nullptr);
  std::vector<uint32_t> p_concurrencies(numa_count + 1, 0);
  tcm_cpu_mask_t* mask_array = new tcm_cpu_mask_t[numa_count + 1];
  std::unique_ptr<tcm_cpu_mask_t[], masks_guard_t> masks_ptr(mask_array, numa_count + 1);
  std::vector<tcm_permit_t> permits(numa_count + 1);
  uint32_t total_resources_given = 0;
  for (int i = 0; i < numa_count; ++i) {
    mask_array[i] = allocate_cpu_mask();
    permits[i] = make_void_permit(&p_concurrencies[i], mask_array + i);
    r = tcmRequestPermit(
      client_ids[i], requests[i], /*callback_arg*/nullptr, &permit_handles[i], &permits[i]
    );

    uint32_t expected_concurrency = 0;
    tcm_permit_t expected_permit = make_active_permit(&expected_concurrency);

    // TODO: check concurrency exactly and the masks
    const int mask_weight = hardware_concurrency(permits[i].cpu_masks[0]);
    const int process_hardware_concurrency = hardware_concurrency(process_affinity_mask);
    check_success(r, "tcmRequestPermit for client " + std::to_string(i));
    check_permit(expected_permit, permits[i], skip_concurrency_and_mask_checks);
    check(permits[i].concurrencies[0] > 0, "Concurrency was given", /*num_indents*/1);
    check(mask_weight > 0, "Some mask was given", /*num_indents*/1);
    check((numa_count == 1 && process_hardware_concurrency == mask_weight) ||
          mask_weight < process_hardware_concurrency, "Given mask is reasonable", /*num_indents*/1);
    total_resources_given += permits[i].concurrencies[0];
  }

  check(total_resources_given == uint32_t(platform_hardware_concurrency()),
        "All resources from the platform have been distributed");


  // one additional permit request: one more than the number of NUMA-nodes
  uint32_t mask_concurrency = 0, mask_oversubscribed_concurrency = 0;
  {
    mask_array[numa_count] = allocate_cpu_mask();
    permits[numa_count] = make_void_permit(&p_concurrencies[numa_count], mask_array + numa_count);
    r = tcmRequestPermit(
      client_ids[numa_count], requests[numa_count], /*callback_arg*/nullptr,
      &permit_handles[numa_count], &permits[numa_count]
    );
    check_success(r, "tcmRequestPermit for one more NUMA node, client " + std::to_string(numa_count));

    tcm_cpu_mask_t mask = permits[numa_count].cpu_masks[0];
    mask_concurrency = uint32_t(hardware_concurrency(mask));
    mask_oversubscribed_concurrency = uint32_t(tcm_concurrency(mask));
    const uint32_t delta = mask_oversubscribed_concurrency - mask_concurrency;

    uint32_t expected_concurrency =
        std::max(std::min(mask_concurrency, delta), uint32_t(requests[numa_count].min_sw_threads));
    tcm_permit_t expected_permit = make_active_permit(&expected_concurrency);
    skip_checks_t skip_mask_check; skip_mask_check.mask = true;
    check_permit(expected_permit, permits[numa_count], skip_mask_check);
    check(hardware_concurrency(permits[numa_count].cpu_masks[0]) > 0, "Mask was given",
          /*num_indents*/1);
  }

  // Find a rival with the last permit
  unsigned num_intersects = 0;
  int rival_idx = -1;
  for (int i = 0; i < numa_count; ++i) {
      if (is_intersect(permits[i].cpu_masks[0], permits[numa_count].cpu_masks[0])) {
          ++num_intersects;
          rival_idx = i;
      }
  }
  check(1 == num_intersects, "Only one permit intersects with the last one");

  check(permits[rival_idx].concurrencies[0] == std::min(mask_concurrency, (std::uint32_t)platform_tcm_concurrency()),
        "Rival permit occupies whole concurrency of the given mask");
  const uint32_t rival_previous_concurrency = permits[rival_idx].concurrencies[0];
  tcm_cpu_mask_t mask = allocate_cpu_mask();
  std::unique_ptr<tcm_cpu_mask_t, mask_deleter> cpu_mask(&mask);
  copy(mask, permits[rival_idx].cpu_masks[0]);
  r = tcmGetPermitData(permit_handles[rival_idx], &permits[rival_idx]);
  const uint32_t used_concurrency =
      permits[rival_idx].concurrencies[0] + permits[numa_count].concurrencies[0];
  check_success(r, "tcmGetPermitData for ph=" + to_string(permit_handles[rival_idx]));
  check(is_equal(mask, permits[rival_idx].cpu_masks[0]), "Masks compare equally", /*num_indents*/1);
  check(rival_previous_concurrency <= used_concurrency &&
        used_concurrency <= mask_oversubscribed_concurrency,
        "The resources of the NUMA node is shared between intersecting permits", /*num_indents*/1);

  uint32_t expected_concurrency = rival_previous_concurrency;
  tcm_permit_t expected_permit = make_active_permit(&expected_concurrency, &mask);
  r = tcmReleasePermit(permit_handles[rival_idx]);
  check_success(r, "tcmReleasePermit for client " + std::to_string(rival_idx));
  check_permit(expected_permit, permit_handles[numa_count]);

  // release all permits and disconnect the clients
  for (int i = 0; i < numa_count + 1; ++i) {
    if (rival_idx != i) {
      r = tcmReleasePermit(permit_handles[i]);
      check_success(r, "tcmReleasePermit for client " + std::to_string(i));
    }
    disconnect_client(client_ids[i]);
  }
}

TEST("Non-negotiable requests for any NUMA node until all nodes are distributed + one more, "
     "correct activation of pending when one is released")
{
  const skip_checks_t skip_concurrency_and_mask_checks{
      /*size*/false, /*concurrency*/true, /*state*/false, /*flags*/false, /*mask*/true
  };

  // parse all numa nodes
  int32_t numa_count{0}, type_count{0};
  int32_t* numa_indexes{nullptr};
  int32_t* type_indexes{nullptr};
  auto& topology_ptr = tcm_test::system_topology::instance();
  topology_ptr.fill_topology_information(numa_count, numa_indexes, type_count, type_indexes);

  // connect all clients
  tcm_result_t r = TCM_RESULT_ERROR_UNKNOWN;
  std::vector<tcm_client_id_t> client_ids(numa_count + 1);
  for (int i = 0; i < numa_count + 1; ++i) {
    client_ids[i] = connect_new_client(/*client callback*/nullptr);
  }

  // make permit requests
  std::vector<tcm_permit_request_t> requests(numa_count + 1);
  std::vector<tcm_cpu_constraints_t> constraints(numa_count + 1);
  for (int i = 0; i < numa_count + 1; ++i) {
    requests[i] = TCM_PERMIT_REQUEST_INITIALIZER;
    requests[i].min_sw_threads = tcm_automatic;
    requests[i].max_sw_threads = tcm_automatic;
    requests[i].constraints_size = 1;
    requests[i].cpu_constraints = &constraints[i];
    requests[i].flags.rigid_concurrency = true;
    constraints[i] = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    constraints[i].numa_id = tcm_any;
  }

  // request all permits
  std::vector<tcm_permit_handle_t> permit_handles(numa_count + 1, nullptr);
  std::vector<uint32_t> p_concurrencies(numa_count + 1, 0);
  tcm_cpu_mask_t* mask_array = new tcm_cpu_mask_t[numa_count + 1];
  std::unique_ptr<tcm_cpu_mask_t[], masks_guard_t> masks_ptr(mask_array, numa_count + 1);
  std::vector<tcm_permit_t> permits(numa_count + 1);
  uint32_t total_resources_given = 0;
  for (int i = 0; i < numa_count; ++i) {
    mask_array[i] = allocate_cpu_mask();
    permits[i] = make_void_permit(&p_concurrencies[i], mask_array + i);
    r = tcmRequestPermit(
      client_ids[i], requests[i], /*callback_arg*/nullptr, &permit_handles[i], &permits[i]
    );

    uint32_t expected_concurrency = 0;
    tcm_permit_t expected_permit = make_active_permit(&expected_concurrency);
    expected_permit.flags.rigid_concurrency = true;

    // TODO: check concurrency exactly and the masks
    const int mask_weight = hardware_concurrency(permits[i].cpu_masks[0]);
    const int process_hardware_concurrency = hardware_concurrency(process_affinity_mask);
    check_success(r, "tcmRequestPermit for client " + std::to_string(i));
    check_permit(expected_permit, permits[i], skip_concurrency_and_mask_checks) ;
    check(permits[i].concurrencies[0] > 0, "Concurrency was given", /*num_indents*/1) ;
    check(mask_weight > 0, "Some mask was given", /*num_indents*/1);
    check((numa_count == 1 && process_hardware_concurrency == mask_weight) ||
          mask_weight < process_hardware_concurrency, "Given mask is reasonable", /*num_indents*/1);
    total_resources_given += permits[i].concurrencies[0];
  }

  check(total_resources_given == uint32_t(platform_tcm_concurrency()),
        "All resources from the platform have been distributed");

  // one additional permit request: one more than the number of NUMA-nodes
  uint32_t mask_concurrency = 0, mask_oversubscribed_concurrency = 0;
  mask_array[numa_count] = allocate_cpu_mask();
  requests[numa_count].min_sw_threads = 1; // make it waiting for resources to become available
  permits[numa_count] = make_void_permit(&p_concurrencies[numa_count], mask_array + numa_count);
  r = tcmRequestPermit(
      client_ids[numa_count], requests[numa_count], /*callback_arg*/nullptr,
      &permit_handles[numa_count], &permits[numa_count]
  );
  check_success(r, "tcmRequestPermit for one more NUMA node, client " + std::to_string(numa_count));

  {
      tcm_cpu_mask_t mask = permits[numa_count].cpu_masks[0];
      mask_concurrency = uint32_t(hardware_concurrency(mask));
      mask_oversubscribed_concurrency = uint32_t(tcm_concurrency(mask));
  }
  const uint32_t delta = mask_oversubscribed_concurrency - mask_concurrency;

  uint32_t expected_concurrency = std::min(mask_concurrency, delta);
  const bool is_required_fits_expected =
      int32_t(expected_concurrency) >= requests[numa_count].min_sw_threads;
  tcm_cpu_mask_t mask = allocate_cpu_mask();
  std::unique_ptr<tcm_cpu_mask_t, mask_deleter> mask_guard(&mask);
  tcm_permit_t expected_permit = make_pending_permit(&expected_concurrency, &mask);
  if (is_required_fits_expected) {
      expected_permit.state = TCM_PERMIT_STATE_ACTIVE;
      copy(mask, permits[numa_count].cpu_masks[0]);
      check(hardware_concurrency(mask) > 0, "Some mask was given");
  }
  expected_permit.flags.rigid_concurrency = true;
  check_permit(expected_permit, permits[numa_count]);

  // Release one and expect the pending permit activates
  r = tcmReleasePermit(permit_handles[0]);
  bool result = false;
  if (is_required_fits_expected) {
      // Nothing should change since a rigid concurrency permit has been already activated
      result = check_permit(expected_permit, permit_handles[numa_count]);
  } else {
      // The pending rigid concurrency permit should now take the resources released
      result = check_permit(permits[0], permit_handles[numa_count]);
  }
  check_success(r, "tcmReleasePermit for client 0");
  check(result, "Resources were correctly distributed");

  // release all permits and disconnect the clients
  for (int i = 0; i < numa_count + 1; ++i) {
    if (0 != i) {
      r = tcmReleasePermit(permit_handles[i]);
      check_success(r, "tcmReleasePermit for client " + std::to_string(i));
    }
    disconnect_client(client_ids[i]);
  }
}

TEST("Single request with many low-level constraints single resource each") {
    tcm_test::system_topology& topology = tcm_test::system_topology::instance();
    tcm_cpu_mask_t process_cpu_mask = topology.allocate_process_affinity_mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> process_mask_guard(&process_cpu_mask);

    tcm_client_id_t client_id = connect_new_client();

    // One constraint per each PU
    const unsigned process_concurrency = unsigned(hardware_concurrency(process_cpu_mask));
    const unsigned constraints_size = process_concurrency;
    auto constraints_guard = std::make_unique<tcm_cpu_constraints_t[]>(constraints_size);
    tcm_cpu_constraints_t* constraints = constraints_guard.get();

    const unsigned num_masks = constraints_size;
    auto masks_guard = std::make_unique<tcm_cpu_mask_t[]>(num_masks);
    tcm_cpu_mask_t* expected_masks = masks_guard.get();

    int bit_index = -1; unsigned i = 0;
    while ((bit_index = hwloc_bitmap_next(process_cpu_mask, bit_index)) != -1) {
        constraints[i] = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
        tcm_cpu_mask_t mask = expected_masks[i] = constraints[i].mask = allocate_cpu_mask();
        hwloc_bitmap_set(mask, bit_index);
        ++i;
    }
    check(num_masks == i, "All process PUs enumerated");

    tcm_permit_request_t req = make_request(/*min_sw_threads*/tcm_automatic,
                                            /*max_sw_threads*/tcm_automatic,
                                            constraints, constraints_size);
    tcm_permit_handle_t ph = request_permit(client_id, req);

    std::vector<uint32_t> concurrencies(constraints_size);
    // Not all hardware resources may be available to the process
    std::fill(concurrencies.begin(), concurrencies.begin() + platform_tcm_concurrency(), 1);
    tcm_permit_t expected_permit = make_active_permit(concurrencies.data(), expected_masks,
                                                      constraints_size);
    check_permit<distribution_agnostic_concurrency_checker>(expected_permit, ph);

    // Test the union of masks from permit constitutes the process mask
    tcm_cpu_mask_t united_mask = allocate_cpu_mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> united_mask_ptr(&united_mask);
    tcm_permit_t actual_permit = expected_permit;
    get_permit_data(ph, actual_permit);
    tcm_cpu_mask_t* cpu_masks = actual_permit.cpu_masks;
    int err = 0;
    for (uint32_t j = 0; j < actual_permit.size; ++j)
        err += hwloc_bitmap_or(united_mask, united_mask, cpu_masks[j]);
    check(0 == err, "Union of masks was made successfully");
    check(process_concurrency == unsigned(hardware_concurrency(united_mask)),
          "Masks from permit form the mask of the process");

    release_permit(ph);
    disconnect_client(client_id);
}

TEST("Request with NUMA and core type within single constraint") {
    tcm_client_id_t client_id = connect_new_client();

    tcm_cpu_constraints_t c = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    c.numa_id = 0;
    c.core_type_id = 0;
    tcm_permit_request_t req = make_request(/*min_sw_threads*/tcm_automatic,
                                            /*max_sw_threads*/tcm_automatic, &c, /*size*/1);
    tcm_permit_handle_t ph = request_permit(client_id, req);

    tcm_cpu_mask_t expected_mask = allocate_cpu_mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_mask_guard (&expected_mask);
    tcm_test::system_topology& topology = tcm_test::system_topology::instance();
    topology.fill_constraints_affinity_mask(expected_mask, /*numa_id*/ 0, /*core_type_id*/ 0,
                                            /*max_threads_per_core*/tcm_automatic);
    uint32_t expected_concurrency = std::min(platform_tcm_concurrency(),
                                             hardware_concurrency(expected_mask));
    tcm_permit_t expected_permit = make_active_permit(&expected_concurrency, &expected_mask);
    check_permit(expected_permit, ph);

    release_permit(ph);
    disconnect_client(client_id);
}

void test_unconstrained_then_constrained(bool as_nested) {
    const int32_t full_concurrency = int32_t(platform_tcm_concurrency());
    tcm_client_id_t client_id = connect_new_client(/*callback*/ nullptr);

    tcm_permit_request_t unconstrained_ph_request = make_request(/*min_sw_threads*/ 1,
                                                                 /*max_sw_threads*/ tcm_automatic);

    tcm_permit_handle_t unconstrained_ph = request_permit(client_id, unconstrained_ph_request);
    permit_t unconstrained_permit = get_permit_data(unconstrained_ph);
    permit_t expected_permit = make_active_permit(full_concurrency);
    check_permit(expected_permit, unconstrained_permit);

    if (as_nested) register_thread(unconstrained_ph);

    // Making a constrained permit request that should negotiate with the unconstrained one
    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    tcm_cpu_mask_t mask = allocate_cpu_mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask_guard(&mask);
    extract_first_n_bits_from_process_affinity_mask(mask, /*n_threads*/full_concurrency / 2);
    constraints.mask = mask;
    permit_t expected_constrained_permit = make_active_permit(full_concurrency / 2, &mask);

    tcm_permit_request_t constrained_ph_request = make_request(/*min_sw_threads*/ full_concurrency / 2,
                                                               /*max_sw_threads*/ tcm_automatic,
                                                               &constraints, /*size*/ 1);

    tcm_permit_handle_t constrained_ph = request_permit(client_id, constrained_ph_request);
    permit_t constrained_permit = get_permit_data(constrained_ph, /*allocate mask*/ true);

    // Check constrained permit was granted and negotiation happened
    check_permit(expected_constrained_permit, constrained_permit);
    check(constrained_permit.cpu_mask() != mask,
          "The memory of requested mask is not reused for the mask in granted permit");
    get_permit_data(unconstrained_ph, unconstrained_permit);
    tcm_permit_t(expected_permit).concurrencies[0] =
        full_concurrency - constrained_permit.concurrency() + int(as_nested);
    check_permit(expected_permit, unconstrained_permit);

    int32_t total_grant = unconstrained_permit.concurrency() + constrained_permit.concurrency();
    check(total_grant == full_concurrency + int(as_nested), "Total resource grant converges");

    release_permit(constrained_ph);

    // Check resources of unconstrained permit were restored
    tcm_permit_t(expected_permit).concurrencies[0] = full_concurrency;
    check_permit(expected_permit, unconstrained_ph);

    if (as_nested) unregister_thread();

    release_permit(unconstrained_ph);

    assert_all_resources_available();
    disconnect_client(client_id);
}

void test_constrained_then_unconstrained(bool as_nested) {
    // Do same in opposite order
    const int32_t full_concurrency = int32_t(platform_tcm_concurrency());
    tcm_client_id_t client_id = connect_new_client(/*callback*/ nullptr);

    tcm_cpu_constraints_t constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    tcm_cpu_mask_t mask = allocate_cpu_mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask_guard(&mask);
    extract_first_n_bits_from_process_affinity_mask(mask, /*n_threads*/full_concurrency / 2);
    constraints.mask = mask;
    permit_t expected_constrained_permit = make_active_permit(full_concurrency / 2, &mask);

    tcm_permit_request_t constrained_ph_request = make_request(/*min_sw_threads*/ 1,
                                                               /*max_sw_threads*/ tcm_automatic,
                                                               &constraints, /*size*/ 1);

    const int32_t mask_concurrency = hardware_concurrency(mask);
    tcm_permit_handle_t constrained_ph = request_permit(client_id, constrained_ph_request);
    permit_t constrained_permit = get_permit_data(constrained_ph, /*allocate mask*/ true);
    tcm_permit_t(expected_constrained_permit).concurrencies[0] = mask_concurrency;
    check_permit(expected_constrained_permit, constrained_permit);

    if (as_nested) register_thread(constrained_ph);

    tcm_permit_request_t unconstrained_ph_request =
        make_request(/*min_sw_threads*/ full_concurrency - 2, /*max_sw_threads*/ tcm_automatic);

    uint32_t expected_inner_concurrency = std::max(unconstrained_ph_request.min_sw_threads,
                                                   full_concurrency - mask_concurrency);
    uint32_t expected_outer_concurrency = mask_concurrency;
    const bool will_negotiate = full_concurrency <
                                mask_concurrency + unconstrained_ph_request.min_sw_threads;
    if (will_negotiate) {
        expected_outer_concurrency = full_concurrency -
                                     unconstrained_ph_request.min_sw_threads +
                                     int(as_nested);
        expected_outer_concurrency = std::min(expected_outer_concurrency, uint32_t(mask_concurrency));
    } else {
        expected_inner_concurrency += int(as_nested);
    }

    permit_t expected_permit = make_active_permit(expected_inner_concurrency);
    tcm_permit_handle_t unconstrained_ph = request_permit(client_id, unconstrained_ph_request);
    permit_t unconstrained_permit = get_permit_data(unconstrained_ph);
    check_permit(expected_permit, unconstrained_permit);

    // Check unconstrained permit was granted and negotiation happened
    tcm_permit_t(expected_constrained_permit).concurrencies[0] = expected_outer_concurrency;
    get_permit_data(constrained_ph, constrained_permit);
    check_permit(expected_constrained_permit, constrained_permit);
    check(constrained_permit.cpu_mask() != mask,
          "The memory of requested mask is not reused for the mask in granted permit");

    int32_t total_grant = unconstrained_permit.concurrency() + constrained_permit.concurrency();
    check(total_grant == full_concurrency + int(as_nested), "Total resource grant converges");

    release_permit(unconstrained_ph);

    // Check resources of a constrained permit were restored
    tcm_permit_t(expected_constrained_permit).concurrencies[0] = mask_concurrency;
    get_permit_data(constrained_ph, constrained_permit);
    check_permit(expected_constrained_permit, constrained_permit);

    if (as_nested) unregister_thread();

    release_permit(constrained_ph);

    assert_all_resources_available();
    disconnect_client(client_id);
}

TEST("Unconstrained + constrained") {
    test_unconstrained_then_constrained(/*second request nested*/false);
}

TEST("Constrained + unconstrained") {
    test_constrained_then_unconstrained(/*second request nested*/false);
}

TEST("Unconstrained + nested constrained") {
    test_unconstrained_then_constrained(/*second request nested*/true);
}

TEST("Constrained + nested unconstrained") {
    test_constrained_then_unconstrained(/*second request nested*/true);
}
