/*
    Copyright (c) 2019-2025 Intel Corporation

    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

        http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License.
*/

//! \file conformance_arena_constraints.cpp
//! \brief Test for [info_namespace scheduler.task_arena] specifications

#include "common/common_arena_constraints.h"
#include "common/test.h"
#include "oneapi/tbb/info.h"

#if __TBB_HWLOC_VALID_ENVIRONMENT

//! Testing all NUMA aware arenas can successfully execute tasks
//! \brief \ref interface \ref requirement
TEST_CASE("NUMA aware arenas task execution test") {
    system_info::initialize();
    for(auto& numa_index: oneapi::tbb::info::numa_nodes()) {
        oneapi::tbb::task_arena arena(oneapi::tbb::task_arena::constraints{numa_index});

        std::atomic<bool> task_done{false};
        arena.execute([&]{ task_done = true; });
        REQUIRE_MESSAGE(task_done, "Execute was performed but task was not executed.");

        task_done = false;
        arena.enqueue([&]{ task_done = true; });
        while(!task_done) { utils::yield(); }
    }
}

//! Testing NUMA topology traversal correctness
//! \brief \ref interface \ref requirement
TEST_CASE("Test NUMA topology traversal correctness") {
    system_info::initialize();
    std::vector<index_info> numa_nodes_info = system_info::get_numa_nodes_info();

    std::vector<oneapi::tbb::numa_node_id> numa_indexes = oneapi::tbb::info::numa_nodes();
    for (const auto& numa_id: numa_indexes) {
        auto pos = std::find_if(numa_nodes_info.begin(), numa_nodes_info.end(),
            [&](const index_info& numa_info){ return numa_info.index == numa_id; }
        );

        REQUIRE_MESSAGE(pos != numa_nodes_info.end(), "Wrong, extra or repeated NUMA node index detected.");
        numa_nodes_info.erase(pos);
    }

    REQUIRE_MESSAGE(numa_nodes_info.empty(), "Some available NUMA nodes indexes were not detected.");
}

#if __HYBRID_CPUS_TESTING
//! Testing NUMA topology traversal correctness
//! \brief \ref interface \ref requirement
TEST_CASE("Test core types topology traversal correctness") {
    system_info::initialize();
    std::vector<index_info> core_types_info = system_info::get_cpu_kinds_info();
    std::vector<tbb::core_type_id> core_types = tbb::info::core_types();

    REQUIRE_MESSAGE(core_types_info.size() == core_types.size(), "Wrong core types number detected.");
    for (unsigned i = 0; i < core_types.size(); ++i) {
        REQUIRE_MESSAGE(core_types[i] == core_types_info[i].index, "Wrong core type index detected.");
    }
}
#endif /*__HYBRID_CPUS_TESTING*/

//! Testing create_numa_task_arenas helper function correctness
//! \brief \ref interface \ref requirement
TEST_CASE("Test create_numa_task_arenas conformance correctness") {
  system_info::initialize();
  auto numa_indices = oneapi::tbb::info::numa_nodes();
  auto numa_task_arenas = oneapi::tbb::create_numa_task_arenas();
  REQUIRE_MESSAGE(numa_task_arenas.size() == numa_indices.size(),
      "create_numa_task_arenas returns the same number of NUMA nodes as tbb::info::numa_nodes()");
  // Test that arenas are not initialized
  for (auto& ta : numa_task_arenas) {
    REQUIRE_MESSAGE(!ta.is_active(),
        "create_numa_task_arenas must return a vector of non-initialized arenas");
  }

  for (std::size_t numa_i = 0; numa_i < numa_indices.size(); ++numa_i) {
    oneapi::tbb::task_arena::constraints c{numa_indices[numa_i]};
    auto constraint_concurrency = oneapi::tbb::info::default_concurrency(c);
    REQUIRE_MESSAGE(constraint_concurrency == numa_task_arenas[numa_i].max_concurrency(),
        "Maximum concurrency level of task_arena should be the same as for constraints");
  }
}

#else /*!__TBB_HWLOC_VALID_ENVIRONMENT*/

//! Testing NUMA support interfaces validity when HWLOC is not present on system
//! \brief \ref interface \ref requirement
TEST_CASE("Test validity of NUMA interfaces when HWLOC is not present on the system") {
    std::vector<oneapi::tbb::numa_node_id> numa_indexes = oneapi::tbb::info::numa_nodes();
    std::vector<oneapi::tbb::task_arena> numa_arenas = oneapi::tbb::create_numa_task_arenas();
#if __TBB_SELF_CONTAINED_TBBBIND
    // Do lvalue-to-rvalue conversion to not odr-use tbb::task_arena::automatic
    REQUIRE_MESSAGE(numa_indexes[0] != static_cast<tbb::numa_node_id>(tbb::task_arena::automatic),
        "Index of NUMA node must NOT be pinned to tbb::task_arena::automatic, since self-contained TBBBind is loaded.");
#else
    REQUIRE_MESSAGE(numa_indexes.size() == 1,
        "Number of NUMA nodes must be pinned to 1, if we have no HWLOC on the system.");
    REQUIRE_MESSAGE(numa_indexes[0] == static_cast<tbb::numa_node_id>(tbb::task_arena::automatic),
        "Index of NUMA node must be pinned to tbb::task_arena::automatic, if we have no HWLOC on the system.");
    REQUIRE_MESSAGE(oneapi::tbb::info::default_concurrency(numa_indexes[0]) == utils::get_platform_max_threads(),
        "Concurrency for NUMA node must be equal to default_num_threads(), if we have no HWLOC on the system.");
    REQUIRE_MESSAGE(numa_arenas.size() == 1,
        "Number of NUMA-bound task_arena objects must be one if we have no HWLOC on the system");
#endif
}

#endif /*__TBB_HWLOC_VALID_ENVIRONMENT*/
