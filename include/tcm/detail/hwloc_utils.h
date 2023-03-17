/*
    Copyright (c) 2021-2023 Intel Corporation
*/

#ifndef __TCM_HWLOC_UTILS_HEADER
#define __TCM_HWLOC_UTILS_HEADER

#include <vector>
#include <thread>               // std::this_thread::yield()

#include "tcm/detail/_tcm_assert.h"

#if _MSC_VER && !__INTEL_COMPILER && !__clang__
#pragma warning( push )
#pragma warning( disable : 4100 )
#elif _MSC_VER && __clang__
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include <hwloc.h>
#if _WIN32 || _WIN64
#include <winbase.h>
#include <hwloc/windows.h>      // for hwloc_windows_get_nr_processor_groups
#endif
#if _MSC_VER && !__INTEL_COMPILER && !__clang__
#pragma warning( pop )
#elif _MSC_VER && __clang__
#pragma GCC diagnostic pop
#endif

#define __TCM_HWLOC_HYBRID_CPUS_INTERFACES_PRESENT                         \
  (HWLOC_API_VERSION >= 0x20400)

#define __TCM_HWLOC_TOPOLOGY_FLAG_RESTRICT_TO_CPUBINDING_PRESENT           \
  (HWLOC_API_VERSION >= 0x20500)

class hwloc_topology_loader_t {
public:
    hwloc_topology_loader_t() {
        if (hwloc_topology_init(&topology) != 0) {
          return;
        }
        // Setting these flags allows HWLOC to parse process mask correctly
        // with respect to process affinity set by user.
        // However, on Windows, it omits other processor groups
        // from topology parsing because HWLOC considers only
        // process mask of calling processor group.
        auto parsing_flags = HWLOC_TOPOLOGY_FLAG_IS_THISSYSTEM |
            HWLOC_TOPOLOGY_FLAG_RESTRICT_TO_CPUBINDING;
        if (get_num_proc_groups() == 1 && hwloc_topology_set_flags(topology, parsing_flags) != 0) {
            return;
        }
        if (hwloc_topology_load(topology) != 0) {
          hwloc_topology_destroy(topology);
          return;
        }

        is_initialized = true;
    }

    ~hwloc_topology_loader_t() {
        if (is_initialized) {
            while (spin_mutex.test_and_set()) { std::this_thread::yield(); }
            hwloc_topology_destroy(topology);
            is_initialized = false;
            spin_mutex.clear();
        }
    }

    hwloc_topology_t get_topology() {
        hwloc_topology_t new_topology{nullptr};

        while (spin_mutex.test_and_set()) { std::this_thread::yield(); }
        if (is_initialized) {
            hwloc_topology_dup(&new_topology, topology);
        }
        spin_mutex.clear();

        return new_topology;
    }

private:
    uint32_t get_num_proc_groups() {
#if _WIN32 || _WIN64
    return GetActiveProcessorGroupCount();
#else
    return 1;
#endif
    }

    bool is_initialized{false};
    std::atomic_flag spin_mutex = ATOMIC_FLAG_INIT;
    hwloc_topology_t topology;
};

// Having the object in the global scope allows moving the overhead of HWLOC topology loading to the
// library load time.
static hwloc_topology_loader_t topology_loader;

class system_topology {
    friend class binding_handler;

    // Common topology members
    hwloc_topology_t topology{nullptr};
    hwloc_cpuset_t   process_cpu_affinity_mask{nullptr};
    hwloc_nodeset_t  process_node_affinity_mask{nullptr};
    std::size_t number_of_processors_groups{1};

    // NUMA API related topology members
    std::vector<hwloc_cpuset_t> numa_affinity_masks_list{};
    std::vector<int> numa_indexes_list{};
    int numa_nodes_count{0};

    // Hybrid CPUs API related topology members
    std::vector<hwloc_cpuset_t> core_types_affinity_masks_list{};
    std::vector<int> core_types_indexes_list{};

    enum init_stages { uninitialized,
                       started,
                       topology_allocated,
                       topology_loaded,
                       topology_parsed } initialization_state{uninitialized};

    // Binding threads that locate in another Windows Processor groups
    // is allowed only if machine topology contains several Windows Processors groups
    // and process affinity mask wasn`t limited manually (affinity mask cannot violates
    // processors group boundaries).
    bool intergroup_binding_allowed(std::size_t groups_num) { return groups_num > 1; }

private:
    void topology_initialization() {
        initialization_state = started;

        topology = topology_loader.get_topology();
        if (!topology) {
            return;
        }
        initialization_state = topology_loaded;
        std::size_t groups_num = 1;

#if _WIN32 || _WIN64
        groups_num = hwloc_windows_get_nr_processor_groups(topology, 0);
#endif

        // Getting process affinity mask
        if ( intergroup_binding_allowed(groups_num) ) {
            process_cpu_affinity_mask  = hwloc_bitmap_dup(hwloc_topology_get_complete_cpuset (topology));
            process_node_affinity_mask = hwloc_bitmap_dup(hwloc_topology_get_complete_nodeset(topology));
        } else {
            process_cpu_affinity_mask  = hwloc_bitmap_alloc();
            process_node_affinity_mask = hwloc_bitmap_alloc();

            auto r = hwloc_get_cpubind(topology, process_cpu_affinity_mask, HWLOC_CPUBIND_PROCESS);
            __TCM_ASSERT_EX(r >= 0, "hwloc_get_cpubind() error.");
            r = hwloc_cpuset_to_nodeset(topology, process_cpu_affinity_mask,
                                        process_node_affinity_mask);
            __TCM_ASSERT_EX(r >= 0, "hwloc_cpuset_to_nodeset error.");
        }

        number_of_processors_groups = groups_num;
    }

    void numa_topology_parsing() {
        // Fill parameters with stubs if topology parsing is broken.
        if ( initialization_state != topology_loaded ) {
            numa_nodes_count = 1;
            numa_indexes_list.push_back(-1);
            return;
        }

        // If system contains no NUMA nodes, HWLOC 1.11 returns an infinitely filled bitmap.
        // hwloc_bitmap_weight() returns negative value for such bitmaps, so we use this check
        // to change way of topology initialization.
        numa_nodes_count = hwloc_bitmap_weight(process_node_affinity_mask);
        if (numa_nodes_count <= 0) {
            // numa_nodes_count may be empty if the process affinity mask is empty too (invalid case)
            // or if some internal HWLOC error occurred.
            // So we place -1 as index in this case.
            numa_indexes_list.push_back(numa_nodes_count == 0 ? -1 : 0);
            numa_nodes_count = 1;

            numa_affinity_masks_list.push_back(hwloc_bitmap_dup(process_cpu_affinity_mask));
        } else {
            // Get NUMA logical indexes list
            unsigned counter = 0;
            int i = 0;
            int max_numa_index = -1;
            numa_indexes_list.resize(numa_nodes_count);
            hwloc_obj_t node_buffer;
            hwloc_bitmap_foreach_begin(i, process_node_affinity_mask) {
                node_buffer = hwloc_get_numanode_obj_by_os_index(topology, i);
                numa_indexes_list[counter] = static_cast<int>(node_buffer->logical_index);

                if ( numa_indexes_list[counter] > max_numa_index ) {
                    max_numa_index = numa_indexes_list[counter];
                }

                counter++;
            } hwloc_bitmap_foreach_end();
            __TCM_ASSERT(max_numa_index >= 0, "Maximal NUMA index must not be negative");

            // Fill concurrency and affinity masks lists
            numa_affinity_masks_list.resize(max_numa_index + 1);
            int index = 0;
            hwloc_bitmap_foreach_begin(i, process_node_affinity_mask) {
                node_buffer = hwloc_get_numanode_obj_by_os_index(topology, i);
                index = static_cast<int>(node_buffer->logical_index);

                hwloc_cpuset_t& current_mask = numa_affinity_masks_list[index];
                current_mask = hwloc_bitmap_dup(node_buffer->cpuset);

                hwloc_bitmap_and(current_mask, current_mask, process_cpu_affinity_mask);
                __TCM_ASSERT(!hwloc_bitmap_iszero(current_mask), "hwloc detected unavailable NUMA node");
            } hwloc_bitmap_foreach_end();
        }
    }

    void core_types_topology_parsing() {
        // Fill parameters with stubs if topology parsing is broken.
        if ( initialization_state != topology_loaded ) {
            core_types_indexes_list.push_back(-1);
            return;
        }
#if __TCM_HWLOC_HYBRID_CPUS_INTERFACES_PRESENT
        __TCM_ASSERT(hwloc_get_api_version() >= 0x20400, "Hybrid CPUs support interfaces required HWLOC >= 2.4");
        // Parsing the hybrid CPU topology
        int core_types_number = hwloc_cpukinds_get_nr(topology, 0);
        bool core_types_parsing_broken = core_types_number <= 0;
        if (!core_types_parsing_broken) {
            core_types_affinity_masks_list.resize(core_types_number);
            int efficiency{-1};

            for (int core_type = 0; core_type < core_types_number; ++core_type) {
                hwloc_cpuset_t& current_mask = core_types_affinity_masks_list[core_type];
                current_mask = hwloc_bitmap_alloc();

                if (!hwloc_cpukinds_get_info(topology, core_type, current_mask, &efficiency, nullptr, nullptr, 0)
                    && efficiency >= 0
                ) {
                    hwloc_bitmap_and(current_mask, current_mask, process_cpu_affinity_mask);

                    if (hwloc_bitmap_weight(current_mask) > 0) {
                        core_types_indexes_list.push_back(core_type);
                    }
                    __TCM_ASSERT(hwloc_bitmap_weight(current_mask) >= 0, "Infinivitely filled core type mask");
                } else {
                    core_types_parsing_broken = true;
                    break;
                }
            }
        }
#else /*!__TCM_HWLOC_HYBRID_CPUS_INTERFACES_PRESENT*/
        bool core_types_parsing_broken{true};
#endif /*__TCM_HWLOC_HYBRID_CPUS_INTERFACES_PRESENT*/

        if (core_types_parsing_broken) {
            for (auto& core_type_mask : core_types_affinity_masks_list) {
                hwloc_bitmap_free(core_type_mask);
            }
            core_types_affinity_masks_list.resize(1);
            core_types_indexes_list.resize(1);

            core_types_affinity_masks_list[0] = hwloc_bitmap_dup(process_cpu_affinity_mask);
            core_types_indexes_list[0] = -1;
        }
    }

    void enforce_hwloc_2_5_runtime_linkage() {
        // Without the call of this function HWLOC 2.4 can be successfully loaded during the tbbbind_2_5 loading.
        // It is possible since tbbbind_2_5 don't use any new entry points that were introduced in HWLOC 2.5
        // But tbbbind_2_5 compiles with HWLOC 2.5 header, therefore such situation requires binary forward compatibility
        // which are not guaranteed by the HWLOC library. To enforce linkage tbbbind_2_5 only with HWLOC >= 2.5 version
        // this function calls the interface that is available in the HWLOC 2.5 only.
#if HWLOC_API_VERSION >= 0x20500
        auto some_core = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_CORE, nullptr);
        hwloc_get_obj_with_same_locality(topology, some_core, HWLOC_OBJ_CORE, nullptr, nullptr, 0);
#endif
    }

    void initialize() {
        if ( initialization_state != uninitialized )
            return;

        topology_initialization();
        numa_topology_parsing();
        core_types_topology_parsing();

        if (initialization_state != topology_loaded) {
            return;
        }
        initialization_state = topology_parsed;
        enforce_hwloc_2_5_runtime_linkage();
    }

    static system_topology* instance_ptr;
public:
    typedef hwloc_cpuset_t             affinity_mask;
    typedef hwloc_const_cpuset_t const_affinity_mask;

    bool is_topology_parsed() { return initialization_state == topology_parsed; }

    static void construct() {
        if (instance_ptr == nullptr) {
            instance_ptr = new system_topology();
            instance_ptr->initialize();
        }
    }

    static system_topology& instance() {
        __TCM_ASSERT(instance_ptr != nullptr, "Getting instance of non-constructed topology");
        return *instance_ptr;
    }

    static void destroy() {
        __TCM_ASSERT(instance_ptr != nullptr, "Destroying non-constructed topology");
        delete instance_ptr;
        instance_ptr = nullptr;
    }

    system_topology() = default;
    system_topology(const system_topology&) = delete;
    system_topology& operator=(const system_topology&) = delete;

    ~system_topology() {
        if ( is_topology_parsed() ) {
            for (auto& numa_node_mask : numa_affinity_masks_list) {
                hwloc_bitmap_free(numa_node_mask);
            }

            for (auto& core_type_mask : core_types_affinity_masks_list) {
                hwloc_bitmap_free(core_type_mask);
            }

            hwloc_bitmap_free(process_node_affinity_mask);
            hwloc_bitmap_free(process_cpu_affinity_mask);
        }

        if ( initialization_state >= topology_allocated ) {
            hwloc_topology_destroy(topology);
        }

        initialization_state = uninitialized;
    }

    unsigned int get_process_concurrency() const {
      return unsigned(hwloc_bitmap_weight(process_cpu_affinity_mask));
    }

    void fill_topology_information(int& _numa_nodes_count, int*& _numa_indexes_list,
                                   int& _core_types_count, int*& _core_types_indexes_list)
    {
        __TCM_ASSERT(is_topology_parsed(), "Trying to get access to uninitialized system_topology");

        _numa_nodes_count = numa_nodes_count;
        _numa_indexes_list = numa_indexes_list.data();

        _core_types_count = (int)core_types_indexes_list.size();
        _core_types_indexes_list = core_types_indexes_list.data();
    }

    void fill_constraints_affinity_mask(affinity_mask input_mask, int numa_node_index,
                                        int core_type_index, int max_threads_per_core)
    {
        __TCM_ASSERT(is_topology_parsed(), "Trying to get access to uninitialized system_topology");
        __TCM_ASSERT(numa_node_index < (int)numa_affinity_masks_list.size(), "Wrong NUMA node id");
        __TCM_ASSERT(core_type_index < (int)core_types_affinity_masks_list.size(), "Wrong core type id");
        __TCM_ASSERT(max_threads_per_core == -1 || max_threads_per_core > 0, "Wrong max_threads_per_core");

        hwloc_cpuset_t constraints_mask = hwloc_bitmap_alloc();
        // TODO: enable the use of unique_ptr (requires the header and shared mask_deleeter) or make
        // the masks persistent.
        // std::unique_ptr<hwloc_cpuset_t, mask_deleter> constraints_mask_guard(&constraints_mask);
        hwloc_cpuset_t core_mask = hwloc_bitmap_alloc();
        // std::unique_ptr<hwloc_cpuset_t, mask_deleter> core_mask_guard(&core_mask);

        hwloc_bitmap_copy(constraints_mask, process_cpu_affinity_mask);
        if (numa_node_index >= 0) {
            hwloc_bitmap_and(constraints_mask, constraints_mask, numa_affinity_masks_list[numa_node_index]);
        }
        if (core_type_index >= 0) {
            hwloc_bitmap_and(constraints_mask, constraints_mask, core_types_affinity_masks_list[core_type_index]);
        }
        if (max_threads_per_core > 0) {
            // clear input mask
            hwloc_bitmap_zero(input_mask);

            hwloc_obj_t current_core = nullptr;
            while ((current_core = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_CORE, current_core)) != nullptr) {
                hwloc_bitmap_and(core_mask, constraints_mask, current_core->cpuset);

                // fit the core mask to required bits number
                int current_threads_per_core = 0;
                for (int id = hwloc_bitmap_first(core_mask); id != -1; id = hwloc_bitmap_next(core_mask, id)) {
                    if (++current_threads_per_core > max_threads_per_core) {
                        hwloc_bitmap_clr(core_mask, id); // TODO: make use of the other threads
                                                         // (hyperthreads) if main ones are occupied
                    }
                }

                hwloc_bitmap_or(input_mask, input_mask, core_mask);
            }
        } else {
            hwloc_bitmap_copy(input_mask, constraints_mask);
        }

        hwloc_bitmap_free(core_mask);
        hwloc_bitmap_free(constraints_mask);
    }

    void fit_num_threads_per_core(affinity_mask result_mask, affinity_mask current_mask, affinity_mask constraints_mask) {
        hwloc_bitmap_zero(result_mask);
        hwloc_obj_t current_core = nullptr;
        while ((current_core = hwloc_get_next_obj_by_type(topology, HWLOC_OBJ_CORE, current_core)) != nullptr) {
            if (hwloc_bitmap_intersects(current_mask, current_core->cpuset)) {
                hwloc_bitmap_or(result_mask, result_mask, current_core->cpuset);
            }
        }
        hwloc_bitmap_and(result_mask, result_mask, constraints_mask);
    }

    int get_default_concurrency(int numa_node_index, int core_type_index, int max_threads_per_core) {
        __TCM_ASSERT(is_topology_parsed(), "Trying to get access to uninitialized system_topology");

        hwloc_cpuset_t constraints_mask = hwloc_bitmap_alloc();
        fill_constraints_affinity_mask(constraints_mask, numa_node_index, core_type_index, max_threads_per_core);

        int default_concurrency = hwloc_bitmap_weight(constraints_mask);
        hwloc_bitmap_free(constraints_mask);
        return default_concurrency;
    }

    affinity_mask allocate_process_affinity_mask() {
        if (is_topology_parsed())
          return hwloc_bitmap_dup(process_cpu_affinity_mask);
        return nullptr;
    }

    void free_affinity_mask( affinity_mask mask_to_free ) {
        hwloc_bitmap_free(mask_to_free); // If bitmap is nullptr, no operation is performed.
    }

    void store_current_affinity_mask( affinity_mask current_mask ) {
        auto result = hwloc_get_cpubind(topology, current_mask, HWLOC_CPUBIND_THREAD);
        __TCM_ASSERT_EX(result >= 0, "hwloc_get_cpubind failed.");

        hwloc_bitmap_and(current_mask, current_mask, process_cpu_affinity_mask);
        __TCM_ASSERT(!hwloc_bitmap_iszero(current_mask),
            "Current affinity mask must intersects with process affinity mask");
    }

    void set_affinity_mask( const_affinity_mask mask ) {
        if (hwloc_bitmap_weight(mask) > 0) {
          auto result = hwloc_set_cpubind(topology, mask, HWLOC_CPUBIND_THREAD);
          __TCM_ASSERT_EX(result >= 0, "hwloc_set_cpubind failed.");
        }
    }
};

system_topology* system_topology::instance_ptr{nullptr};

#endif /* __TCM_HWLOC_UTILS_HEADER */
