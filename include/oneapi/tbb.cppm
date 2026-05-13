/*
    Copyright (c) 2026 UXL Foundation Contributors

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

module;

#include <oneapi/tbb.h>

export module tbb;

export namespace oneapi {
namespace tbb = ::tbb;
} // export namespace oneapi

export using ::TBB_runtime_interface_version;
export using ::TBB_runtime_version;

export namespace tbb {
    // TODO: In general explicit v1 namespace is not needed because it is inline,
    // but for some reason MSVC fails to implicitly insert it, which results in ambiguous symbol errors

    // Parallel Algorithms
    using tbb::v1::split;
    using tbb::v1::proportional_split;

    using tbb::v1::blocked_range;
    using tbb::v1::blocked_range2d;
    using tbb::v1::blocked_range3d;
    using tbb::v1::blocked_nd_range;

    using tbb::v1::parallel_for;
    using tbb::v1::parallel_for_each;
    using tbb::v1::feeder;
    using tbb::v1::parallel_invoke;
    using tbb::v1::parallel_pipeline;
    using tbb::v1::filter;
    using tbb::v1::make_filter;
    using tbb::v1::filter_mode;
    using tbb::v1::flow_control;
    using tbb::v1::parallel_reduce;
    using tbb::v1::parallel_deterministic_reduce;
    using tbb::v1::parallel_scan;
    using tbb::v1::pre_scan_tag;
    using tbb::v1::final_scan_tag;
    using tbb::v1::parallel_sort;
    using tbb::v1::auto_partitioner;
    using tbb::v1::simple_partitioner;
    using tbb::v1::static_partitioner;
    using tbb::v1::affinity_partitioner;

    using tbb::collaborative_call_once;
    using tbb::collaborative_once_flag;

    // Concurrent Containers
    using tbb::v1::tbb_hash_compare;
    using tbb::v1::concurrent_hash_map;

#if TBB_PREVIEW_CONCURRENT_LRU_CACHE
    using tbb::v1::concurrent_lru_cache;
#endif

    using tbb::v1::concurrent_map;
    using tbb::v1::concurrent_multimap;
    using tbb::v1::concurrent_set;
    using tbb::v1::concurrent_multiset;
    using tbb::v1::concurrent_unordered_map;
    using tbb::v1::concurrent_unordered_multimap;
    using tbb::v1::concurrent_unordered_set;
    using tbb::v1::concurrent_unordered_multiset;
    using tbb::v1::concurrent_vector;
    using tbb::v1::concurrent_priority_queue;
    using tbb::v1::concurrent_queue;
    using tbb::v1::concurrent_bounded_queue;

    // Mutual Exclusion
    using tbb::v1::mutex;
    using tbb::v1::rw_mutex;
    using tbb::v1::null_mutex;
    using tbb::v1::null_rw_mutex;
    using tbb::v1::queuing_mutex;
    using tbb::v1::queuing_rw_mutex;
    using tbb::v1::spin_mutex;
    using tbb::v1::spin_rw_mutex;
    using tbb::v1::speculative_spin_mutex;
    using tbb::v1::speculative_spin_rw_mutex;

    // Memory Allocation
    using tbb::v1::cache_aligned_allocator;
    using tbb::v1::cache_aligned_resource;
    using tbb::v1::scalable_allocator;
    using tbb::v1::scalable_memory_resource;
    using tbb::v1::tbb_allocator;
#if TBB_PREVIEW_MEMORY_POOL
    using tbb::v1::memory_pool_allocator;
    using tbb::v1::memory_pool;
    using tbb::v1::fixed_pool;
#endif
#if TBB_PREVIEW_NUMA_ALLOCATION
    using tbb::v1::allocate_numa_interleaved;
    using tbb::v1::deallocate_numa_interleaved;
#endif

    // Thread Local Storage
    using tbb::v1::combinable;
    using tbb::v1::enumerable_thread_specific;
    using tbb::v1::flattened2d;
    using tbb::v1::flatten2d;
    using tbb::v1::ets_key_usage_type;
    using tbb::v1::ets_key_per_instance;
    using tbb::v1::ets_no_key;
    using tbb::v1::ets_suspend_aware;

    // Task Scheduler
    using tbb::v1::global_control;
    using tbb::v1::attach;
    using tbb::v1::finalize;
    using tbb::v1::task_scheduler_handle;
#if !__TBB_DISABLE_SPEC_EXTENSIONS
    namespace ext {
        using tbb::ext::v1::assertion_handler_type;
        using tbb::ext::v1::set_assertion_handler;
        using tbb::ext::v1::get_assertion_handler;
    } // namespace ext
#endif

    namespace task {
        using tbb::v1::task::suspend_point;
        using tbb::v1::task::resume;
        using tbb::v1::task::suspend;
        using tbb::v1::task::current_context;
    } // namespace task

    using tbb::v1::task_arena;
    using tbb::v1::create_numa_task_arenas;
#if TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    using tbb::v1::is_inside_task;
#endif

    namespace this_task_arena {
        using tbb::v1::this_task_arena::current_thread_index;
        using tbb::v1::this_task_arena::max_concurrency;
        using tbb::v1::this_task_arena::isolate;
        using tbb::v1::this_task_arena::enqueue;
#if TBB_PREVIEW_PARALLEL_PHASE
        using tbb::v1::this_task_arena::start_parallel_phase;
        using tbb::v1::this_task_arena::end_parallel_phase;
#endif
    } // namespace this_task_arena

    using tbb::v1::task_group_context;
    using tbb::v1::task_group;
#if TBB_PREVIEW_ISOLATED_TASK_GROUP
    using tbb::v1::isolated_task_group;
#endif
    using tbb::v1::task_group_status;
    using tbb::v1::not_complete;
    using tbb::v1::complete;
    using tbb::v1::canceled;
    using tbb::v1::is_current_task_group_canceling;
    using tbb::v1::task_handle;
#if TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    using tbb::v1::task_completion_handle;
    using tbb::v1::task_complete;
#endif
    using tbb::v1::task_scheduler_observer;

    // info Namespace
    using tbb::v1::numa_node_id;
    using tbb::v1::core_type_id;

    namespace info {
        using tbb::v1::info::numa_nodes;
        using tbb::v1::info::core_types;
        using tbb::v1::info::default_concurrency;
    } // namespace info

    // Exceptions
    using tbb::v1::user_abort;
    using tbb::v1::bad_last_alloc;
    using tbb::v1::unsafe_wait;
    using tbb::v1::missing_wait;

    // Timing
    using tbb::v1::tick_count;

    // Flow Graph
    namespace flow {
        // TODO: should abstract APIs be part of module
        using tbb::flow::v1::receiver;
        using tbb::flow::v1::sender;

        using tbb::flow::v1::serial;
        using tbb::flow::v1::unlimited;

        using tbb::flow::v1::reset_flags;
        using tbb::flow::v1::rf_reset_protocol;
        using tbb::flow::v1::rf_reset_bodies;
        using tbb::flow::v1::rf_clear_edges;

        using tbb::flow::v1::graph;
        using tbb::flow::v1::graph_node;
        using tbb::flow::v1::continue_msg;

        using tbb::flow::v1::input_node;
        using tbb::flow::v1::function_node;
        using tbb::flow::v1::multifunction_node;
        using tbb::flow::v1::split_node;
        using tbb::flow::v1::output_port;
        using tbb::flow::v1::indexer_node;
        using tbb::flow::v1::tagged_msg;
        using tbb::flow::v1::cast_to;
        using tbb::flow::v1::is_a;
        using tbb::flow::v1::continue_node;
        using tbb::flow::v1::overwrite_node;
        using tbb::flow::v1::write_once_node;
        using tbb::flow::v1::broadcast_node;
        using tbb::flow::v1::buffer_node;
        using tbb::flow::v1::queue_node;
        using tbb::flow::v1::sequencer_node;
        using tbb::flow::v1::priority_queue_node;
        using tbb::flow::v1::limiter_node;
        using tbb::flow::v1::join_node;
        using tbb::flow::v1::input_port;
        using tbb::flow::v1::copy_body;
        using tbb::flow::v1::make_edge;
        using tbb::flow::v1::remove_edge;
        using tbb::flow::v1::tag_value;
        using tbb::flow::v1::composite_node;
        using tbb::flow::v1::async_node;
        using tbb::flow::v1::node_priority_t;
        using tbb::flow::v1::no_priority;
        using tbb::flow::v1::rejecting;
        using tbb::flow::v1::reserving;
        using tbb::flow::v1::queueing;
        using tbb::flow::v1::lightweight;
        using tbb::flow::v1::key_matching;
        using tbb::flow::v1::tag_matching;
        using tbb::flow::v1::queueing_lightweight;
        using tbb::flow::v1::rejecting_lightweight;

    #if __TBB_PREVIEW_FLOW_GRAPH_NODE_SET
        using tbb::flow::v1::follows;
        using tbb::flow::v1::precedes;
        using tbb::flow::v1::make_node_set;
        using tbb::flow::v1::make_edges;
    #endif

    #if __TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING
        using tbb::flow::v1::resource_limiter;
        using tbb::flow::v1::resource_limited_node;
    #endif
    } // namespace flow

    namespace profiling {
        using tbb::profiling::set_name;
        using tbb::profiling::event;
    } // namespace profiling
} // export namespace tbb
