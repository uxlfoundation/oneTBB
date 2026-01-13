module;

#include <oneapi/tbb.h>

export module tbb;

export namespace tbb {
    inline namespace v1 {
        using detail::d1::parallel_for;
    }
    // template <typename Index, typename Function>
    // void parallel_for(Index, Index, const Function&);
}

// namespace tbb {
// export using TBB_runtime_interface_version;
// export using TBB_runtime_version;
    
// export using blocked_nd_range;
// export using blocked_range;
// export using blocked_range2d;
// export using blocked_range3d;

// export using split;
// export using proportional_split;

// export using parallel_for;
// export using parallel_for_each;
// export using feeder;
// export using parallel_invoke;
// export using parallel_pipeline;
// export using filter;
// export using make_filter;
// export using filter_mode;
// export using flow_control;
// export using parallel_reduce;
// export using parallel_deterministic_reduce;
// export using parallel_scan;
// export using pre_scan_tag;
// export using final_scan_tag;
// export using parallel_sort;
// export using auto_partitioner;
// export using simple_partitioner;
// export using static_partitioner;
// export using affinity_partitioner;

// export using collaborative_call_once;
// export using collaborative_once_flag;

// export using tbb_hash_compare;
// export using concurrent_hash_map;
// // export using concurrent_lru_cache; // preview API
// export using concurrent_map;
// export using concurrent_multimap;
// export using concurrent_set;
// export using concurrent_multiset;
// export using concurrent_unordered_map;
// export using concurrent_unordered_multimap;
// export using concurrent_unordered_set;
// export using concurrent_unordered_multiset;
// export using concurrent_vector;
// export using concurrent_priority_queue;
// export using concurrent_queue;
// export using concurrent_bounded_queue;

// export using mutex;
// export using rw_mutex;
// export using null_mutex;
// export using null_rw_mutex;
// export using queuing_mutex;
// export using queuing_rw_mutex;
// export using spin_mutex;
// export using spin_rw_mutex;
// export using speculative_spin_mutex;
// export using speculative_spin_rw_mutex;

// export using cache_aligned_allocator;
// export using cache_aligned_resource;
// export using scalable_allocator;
// export using scalable_memory_resource;
// export using tbb_allocator;
// // preview APIs
// // export using memory_pool_allocator;
// // export using memory_pool;
// // export using fixed_pool;

// export using combinable;
// export using enumerable_thread_specific;
// export using flattened2d;
// export using flatten2d;
// export using ets_key_usage_type;
// export using ets_key_per_instance;
// export using ets_no_key;
// export using ets_suspend_aware;

// export using global_control;
// export using attach;
// export using finalize;
// export using task_scheduler_handle;
// export using assertion_handler_type;
// export using set_assertion_handler;
// export using get_assertion_handler;

// export using suspend_point;
// export using resume;
// export using suspend;
// export using current_context;
// export using task_arena;
// export using create_numa_task_arenas;
// // export using is_inside_task; // preview API

// namespace this_task_arena {
// export using current_thread_index;
// export using max_concurrency;
// export using isolate;
// export using enqueue;
// // Preview APIs
// // export using start_parallel_phase;
// // export using end_parallel_phase;
// } // namespace this_task_arena

// export using task_group_context;
// export using task_group;
// // export using isolated_task_group; // preview API
// export using task_group_status;
// export using not_complete;
// export using complete;
// export using canceled;
// export using is_current_task_group_canceling;
// export using task_handle;
// // export using task_completion_handle; // preview API

// export using task_scheduler_observer;

// export using numa_node_id;
// export using core_type_id;

// namespace info {
// export using numa_nodes;
// export using core_types;
// export using default_concurrency;
// } // namespace info

// export using user_bort;
// export using bad_last_alloc;
// export using unsafe_wait;
// export using missing_wait;

// export using tick_count;

// namespace flow {
// export using receiver;
// export using sender;

// export using serial;
// export using unlimited;

// export using reset_flags;
// export using rf_reset_protocol;
// export using rf_reset_bodies;
// export using ef_clear_edges;

// export using graph;
// export using graph_node;
// export using continue_msg;

// export using input_node;
// export using function_node;
// export using multifunction_node;
// export using split_node;
// export using output_port;
// export using indexer_node;
// export using tagged_msg;
// export using cast_to;
// export using is_a;
// export using continue_node;
// export using overwrite_node;
// export using write_once_node;
// export using broadcast_node;
// export using buffer_node;
// export using queue_node;
// export using sequencer_node;
// export using priority_queue_node;
// export using limiter_node;
// export using join_node;
// export using input_port;
// export using copy_body;
// export using make_edge;
// export using remove_edge;
// export using tag_value;
// export using composite_node;
// export using async_node;
// export using node_priority_t;
// export using no_priority;
// export using flow_control;

// // Preview APIs
// export using follows;
// export using precedes;
// export using make_node_set;
// export using make_edges;

// // TODO: find API names;
// namespace graph_policy_namespace {

// } // namespace graph_policy_namespace
// } // namespace flow
// namespace profiling {
// export using set_name;
// export using event;
// } // namespace profiling

// } // namespace tbb