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

  int32_t size() {
    auto& topology_ptr = tcm_test::system_topology::instance();
    hwloc_bitmap_t mask = topology_ptr.allocate_process_affinity_mask();
    int32_t weight = hwloc_bitmap_weight(mask);
    hwloc_bitmap_free(mask);
    return weight;
  }
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
      weight = hwloc_bitmap_weight(mask);
    }
  }

  tcm_cpu_mask_t operator()() const { return hwloc_bitmap_dup(mask); }
  int32_t size() const { return weight; }
};

class first_parsed_numa_mask {
  int32_t weight;
  int32_t numa_id;
  hwloc_bitmap_t mask;

public:
  first_parsed_numa_mask(): weight(0), mask(nullptr) {
    int32_t numa_count{0}, type_count{0};
    int32_t* numa_indexes{nullptr};
    int32_t* type_indexes{nullptr};

    auto& topology_ptr = tcm_test::system_topology::instance();
    topology_ptr.fill_topology_information(numa_count, numa_indexes,
                                           type_count, type_indexes);

    numa_id = numa_indexes[0];
    mask = hwloc_bitmap_alloc();
    topology_ptr.fill_constraints_affinity_mask(
      mask,
      numa_id  /* first parsed numa index */,
      -1       /* no constraints by core type */,
      -1       /* no constraints by threads per core */);
    weight = hwloc_bitmap_weight(mask);
  }

  tcm_cpu_mask_t operator()() { return hwloc_bitmap_dup(mask); }
  int32_t size() { return weight; }
  int32_t id() { return numa_id; }
};

struct mask_deleter {
  void operator()(tcm_cpu_mask_t* mask) {
    hwloc_bitmap_free(*mask);
  }
};


bool test_allow_mask_omitting_during_permit_copy(/*tcm_test::system_topology& tp*/) {
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

// ============================================================================
// Single request

struct one_request_config {
  std::string test_name;                // test name for logging
  uint32_t* exp_concurrency;             // expected concurrency for client
  int32_t min_concurrency;              // requested min_concurrency for client
  int32_t max_concurrency;              // requested max_concurrency for client
  uint32_t constraints_size;            // size of array of constraints requested
  tcm_cpu_mask_t* per_mask;             // permit mask allocation
  tcm_cpu_mask_t* exp_mask;             // expected cpu mask
  tcm_cpu_constraints_t* constraints;  // requested constraints
};

struct test_one_request {
  one_request_config config{};

  bool operator()() {
    test_prolog(config.test_name);
    tcm_client_id_t clid;

    tcm_result_t r = tcmConnect(nullptr, &clid);
    if (!check_success(r, "tcmConnect"))
      return test_fail(config.test_name);

    auto p_mask = config.per_mask;
    auto e_mask = config.exp_mask;

    tcm_permit_handle_t ph{nullptr};
    std::unique_ptr<uint32_t[]> p_concurrency{new uint32_t[config.constraints_size]};
    //tcm_permit_t p{p_concurrency.get(), p_mask, config.constraints_size};
    tcm_permit_t p = make_void_permit(p_concurrency.get(), p_mask, config.constraints_size);

    uint32_t* e_concurrency = config.exp_concurrency;
    tcm_permit_t e = make_active_permit(e_concurrency, e_mask, config.constraints_size);

    tcm_permit_request_t req = make_request(config.min_concurrency,
                                             config.max_concurrency);
    req.constraints_size = config.constraints_size;
    req.cpu_constraints = config.constraints;

    r = tcmRequestPermit(clid, req, nullptr, &ph, &p);
    if (!(check_success(r, "tcmRequestPermit") && check_permit(e, p)))
      return test_fail(config.test_name);

    r = tcmReleasePermit(ph);
    if (!check_success(r, "tcmReleasePermit"))
      return test_fail(config.test_name);

    r = tcmDisconnect(clid);
    if (!check_success(r, "tcmDisconnect"))
      return test_fail(config.test_name);

    return test_epilog(config.test_name);
  }
};

template <typename MaskGenerator>
struct test_one_request_low_level_constraints {
  bool operator()(const std::string& test_name) {
    MaskGenerator mask{};
    auto p_mask = hwloc_bitmap_alloc();
    auto e_mask = mask();
    auto r_mask = mask();
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask(&p_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_mask(&e_mask);
    std::unique_ptr<tcm_cpu_mask_t, mask_deleter> requested_mask(&r_mask);
    uint32_t concurrency = uint32_t(tcm_oversubscription_factor * mask.size());

    tcm_cpu_constraints_t cpu_constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_constraints.min_concurrency = 0;
    cpu_constraints.max_concurrency = concurrency;
    cpu_constraints.mask = *requested_mask.get();

    one_request_config test_config{};
    test_config.test_name = test_name;
    test_config.exp_concurrency = &concurrency;
    test_config.min_concurrency = 0;
    test_config.max_concurrency = concurrency;
    test_config.constraints_size = 1;
    test_config.per_mask = permit_mask.get();
    test_config.exp_mask = expected_mask.get();
    test_config.constraints = &cpu_constraints;

    return test_one_request{test_config}();
  }
};

bool test_one_request_process_mask() {
  std::string test_name = "test_one_request_process_mask";
  return test_one_request_low_level_constraints<process_mask>{}(test_name);
}

bool test_one_request_first_core_mask() {
  std::string test_name = "test_one_request_process_mask";
  return test_one_request_low_level_constraints<first_core_mask>{}(test_name);
}

bool test_one_request_first_parsed_numa_mask() {
  std::string test_name = "test_one_request_process_mask";
  return test_one_request_low_level_constraints<first_parsed_numa_mask>{}(test_name);
}

bool test_one_request_first_parsed_numa_id() {
  first_parsed_numa_mask mask{};
  auto p_mask = hwloc_bitmap_alloc();
  auto e_mask = mask();
  std::unique_ptr<tcm_cpu_mask_t, mask_deleter> permit_mask(&p_mask);
  std::unique_ptr<tcm_cpu_mask_t, mask_deleter> expected_mask(&e_mask);
  uint32_t concurrency = uint32_t(tcm_oversubscription_factor * mask.size());

  tcm_cpu_constraints_t cpu_constraints = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
  cpu_constraints.min_concurrency = 0;
  cpu_constraints.max_concurrency = concurrency;
  cpu_constraints.mask = nullptr;
  cpu_constraints.numa_id = mask.id();

  one_request_config test_config{};
  test_config.test_name = "test_one_request_first_parsed_numa_id";
  test_config.exp_concurrency = &concurrency;
  test_config.min_concurrency = 0;
  test_config.max_concurrency = concurrency;
  test_config.constraints_size = 1;
  test_config.per_mask = permit_mask.get();
  test_config.exp_mask = expected_mask.get();
  test_config.constraints = &cpu_constraints;

  return test_one_request{test_config}();
}

bool test_one_request_two_constraints_process_mask_no_oversubscription() {
  process_mask mask{};
  const uint32_t size = 2;

  uint32_t total_concurrency = uint32_t(tcm_oversubscription_factor * mask.size());
  uint32_t e_concurrency[size] = { total_concurrency / 2,
                                   total_concurrency - total_concurrency / 2 };
  tcm_cpu_mask_t permit_mask[size];
  tcm_cpu_mask_t expected_mask[size];
  tcm_cpu_constraints_t cpu_constraints[size];
  for (uint32_t i = 0; i < size; ++i) {
    permit_mask[i] = hwloc_bitmap_alloc();
    expected_mask[i] = mask();
    cpu_constraints[i] = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    cpu_constraints[i].min_concurrency = e_concurrency[i];
    cpu_constraints[i].max_concurrency = e_concurrency[i];
    cpu_constraints[i].mask = mask();
  }

  one_request_config test_config{};
  test_config.test_name =
    "test_one_request_two_constraints_process_mask_no_oversubscription";
  test_config.exp_concurrency = e_concurrency;
  test_config.min_concurrency = 0;
  test_config.max_concurrency = total_concurrency;
  test_config.constraints_size = 2;
  test_config.per_mask = permit_mask;
  test_config.exp_mask = expected_mask;
  test_config.constraints = cpu_constraints;

  bool result = test_one_request{test_config}();

  for (uint32_t i = 0; i < size; ++i) {
    hwloc_bitmap_free(permit_mask[i]);
    hwloc_bitmap_free(expected_mask[i]);
    hwloc_bitmap_free(cpu_constraints[i].mask);
  }

  return result;
}

// ============================================================================
// Two requests

struct two_requests_config {
  std::string test_name;            // test name for logging
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

  bool operator()() {
    test_prolog(config.test_name);

    tcm_client_id_t clidA;
    tcm_client_id_t clidB;

    tcm_result_t r = tcmConnect(config.callback, &clidA);
    if (!check_success(r, "tcmConnect for client A"))
      return test_fail(config.test_name);

    r = tcmConnect(config.callback, &clidB);
    if (!check_success(r, "tcmConnect for client B"))
      return test_fail(config.test_name);

    auto rA_mask = mask_generatorA();
    auto rB_mask = mask_generatorB();
    auto eA_mask = mask_generatorA();
    auto eB_mask = mask_generatorB();
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

    r = tcmRequestPermit(clidA, reqA, &phA, &phA, &pA);
    if (!(check_success(r, "tcmRequestPermit for client A") && check_permit(eA, pA)))
      return test_fail(config.test_name);

    r = tcmRequestPermit(clidB, reqB, &phB, &phB, &pB);
    if (!(check_success(r, "tcmRequestPermit for client B") && check_permit(eB, pB)))
      return test_fail(config.test_name);

    renegotiating_permits = {&phB};
    r = tcmReleasePermit(phA);
    eB.concurrencies[0] = config.new_concurrencyB;
    eB.state = config.new_stateB;
    auto unchanged_permits = list_unchanged_permits({{&phB, &pB}});

    if (!check_success(r, "tcmReleasePermit for client A") &&
        !check(renegotiating_permits == unchanged_permits,
               "Incorrect renegotiation during permit A release"))
      return test_fail(config.test_name);


    r = tcmReleasePermit(phB);
    if (!check_success(r, "tcmReleasePermit for client B"))
      return test_fail(config.test_name);

    r = tcmDisconnect(clidA);
    if (!check_success(r, "tcmDisconnect for client A"))
      return test_fail(config.test_name);

    r = tcmDisconnect(clidB);
    if (!check_success(r, "tcmDisconnect for client B"))
      return test_fail(config.test_name);

    return test_epilog(config.test_name);
  }
};

bool test_two_requests_process_mask_no_oversubscription() {
  uint32_t concurrencyA = total_number_of_threads / 2;
  uint32_t concurrencyB = total_number_of_threads - total_number_of_threads / 2;
  two_requests_config test_config{};
  test_config.test_name = "test_two_requests_process_mask_no_oversubscription";
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

  return test_two_requests<process_mask, process_mask>{test_config}();
}

bool test_two_requests_oversubscribe_first_core() {
  uint32_t concurrencyA = uint32_t(tcm_oversubscription_factor * first_core_mask{}.size());
  uint32_t concurrencyB = concurrencyA;
  two_requests_config test_config{};
  test_config.test_name = "test_two_requests_oversubscribe_first_core";
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

  return test_two_requests<first_core_mask, first_core_mask>{test_config}();
}

// ============================================================================
// Multiple requests

bool test_multiple_requests_all_numa_plus_one() {
  std::string test_name = "test_multiple_requests_all_numa_plus_one"; 
  test_prolog(test_name);

  // parse all numa nodes
  int32_t numa_count{0}, type_count{0};
  int32_t* numa_indexes{nullptr};
  int32_t* type_indexes{nullptr};
  auto& topology_ptr = tcm_test::system_topology::instance();
  topology_ptr.fill_topology_information(numa_count, numa_indexes,
                                         type_count, type_indexes);

  // connect all clients
  tcm_result_t r = TCM_RESULT_ERROR_UNKNOWN;
  std::vector<tcm_client_id_t> client_ids(numa_count + 1);
  for (int i = 0; i < numa_count + 1; ++i) {
    // TODO: Consider the use of actual callback
    r = tcmConnect(nullptr, &client_ids[i]);
    if (!check_success(r, "tcmConnect for client " + std::to_string(i)))
      return test_fail(test_name);
  }

  // make permit requests
  std::vector<tcm_permit_request_t> requests(numa_count + 1);
  std::vector<tcm_cpu_constraints_t> constraints(numa_count + 1);
  for (int i = 0; i < numa_count + 1; ++i) {
    constraints[i] = TCM_PERMIT_REQUEST_CONSTRAINTS_INITIALIZER;
    constraints[i].numa_id = tcm_any;
    requests[i] = TCM_PERMIT_REQUEST_INITIALIZER;
    // TODO: correct the number of requesting resources.
    requests[i].min_sw_threads = total_number_of_threads;
    requests[i].max_sw_threads = total_number_of_threads;
    requests[i].constraints_size = 1;
    requests[i].cpu_constraints = &constraints[i];
  }

  // request all permits
  std::vector<tcm_permit_handle_t> permit_handles(numa_count + 1, nullptr);
  std::vector<uint32_t> p_concurrencies(numa_count + 1, 0);
  std::vector<tcm_cpu_mask_t> masks(numa_count + 1, nullptr);
  std::vector<tcm_permit_t> permits(numa_count + 1);
  for (int i = 0; i < numa_count; ++i) {
    masks[i] = hwloc_bitmap_alloc();
    permits[i] = make_void_permit(&p_concurrencies[i], &masks[i]);
    r = tcmRequestPermit(
      client_ids[i], requests[i], /*callback_arg*/nullptr, &permit_handles[i], &permits[i]
    );
    // TODO: Check for ACTIVE state
    if (!check_success(r, "tcmRequestPermit for client " + std::to_string(i)))
      return test_fail(test_name);
  }

  // one additional permit request: one more than the number of NUMA-nodes
  {
    masks[numa_count] = hwloc_bitmap_alloc();
    permits[numa_count] = make_pending_permit(&p_concurrencies[numa_count], &masks[numa_count]);
    r = tcmRequestPermit(
      client_ids[numa_count], requests[numa_count], /*callback_arg*/nullptr,
      &permit_handles[numa_count], &permits[numa_count]
    );
    if (!check_success(r, "tcmRequestPermit for client " + std::to_string(numa_count)))
      return test_fail(test_name);
  }

  // check last permit granted (PENDING state and empty mask are expected)
  if (!(check(permits[numa_count].concurrencies[0] == 0, "Zero concurrency value") &&
        check(hwloc_bitmap_weight(permits[numa_count].cpu_masks[0]) > 0,
              "Non-zero weight for the permit CPU mask") &&
        check(permits[numa_count].state == TCM_PERMIT_STATE_PENDING,
              "Permit state is PENDING")))
    return test_fail(test_name);

  // release all permits and disconnect the clients
  for (int i = 0; i < numa_count + 1; ++i) {
    r = tcmReleasePermit(permit_handles[i]);
    if (!check_success(r, "tcmReleasePermit for client " + std::to_string(i)))
      return test_fail(test_name);

    r = tcmDisconnect(client_ids[i]);
    if (!check_success(r, "tcmDisconnect for client " + std::to_string(i)))
      return test_fail(test_name);

    // free memory for every permit cpu mask;
    hwloc_bitmap_free(*permits[i].cpu_masks);
  }

  return test_epilog(test_name);
}


int main() {
  bool res = true;

  // TODO: Consider making a topology instance global variable
  tcm_test::system_topology::construct(get_num_proc_groups());

  res &= test_allow_mask_omitting_during_permit_copy();

  res &= test_one_request_process_mask();
  res &= test_one_request_first_core_mask();
  res &= test_one_request_first_parsed_numa_mask();
  res &= test_one_request_first_parsed_numa_id();
  res &= test_one_request_two_constraints_process_mask_no_oversubscription();
  res &= test_two_requests_process_mask_no_oversubscription();
  res &= test_two_requests_oversubscribe_first_core();
  res &= test_multiple_requests_all_numa_plus_one();

  tcm_test::system_topology::destroy();

  return int(!res);
}
