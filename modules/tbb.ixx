module;

#include <oneapi/tbb.h>

export module tbb;

export namespace oneapi {
namespace tbb = ::tbb;
} // export namespace oneapi

export using ::TBB_runtime_interface_version;
export using ::TBB_runtime_version;

export namespace tbb {
    using tbb::split;
    using tbb::proportional_split;

    using tbb::blocked_range;
    using tbb::blocked_range2d;
    using tbb::blocked_range3d;
    using tbb::blocked_nd_range;

    using tbb::parallel_for;
    using tbb::parallel_for_each;
    using tbb::feeder;
    using tbb::parallel_invoke;
    using tbb::parallel_pipeline;
    using tbb::filter;
    using tbb::make_filter;
    using tbb::filter_mode;
    using tbb::flow_control;
    using tbb::parallel_reduce;
    using tbb::parallel_deterministic_reduce;
    using tbb::parallel_scan;
    using tbb::pre_scan_tag;
    using tbb::final_scan_tag;
    using tbb::parallel_sort;
    using tbb::auto_partitioner;
    using tbb::simple_partitioner;
    using tbb::static_partitioner;
    using tbb::affinity_partitioner;

    using tbb::collaborative_call_once;
    using tbb::collaborative_once_flag;

    using tbb::tbb_hash_compare;
    using tbb::concurrent_hash_map;

#if TBB_PREVIEW_CONCURRENT_LRU_CACHE
    using tbb::concurrent_lru_cache;
#endif

    using tbb::concurrent_map;
    using tbb::concurrent_multimap;
    using tbb::concurrent_set;
    using tbb::concurrent_multiset;
    using tbb::concurrent_unordered_map;
    using tbb::concurrent_unordered_multimap;
    using tbb::concurrent_unordered_set;
    using tbb::concurrent_unordered_multiset;
    using tbb::concurrent_vector;
    using tbb::concurrent_priority_queue;
    using tbb::concurrent_queue;
    using tbb::concurrent_bounded_queue;

    using tbb::mutex;
    using tbb::rw_mutex;
    using tbb::null_mutex;
    using tbb::null_rw_mutex;
    using tbb::queuing_mutex;
    using tbb::queuing_rw_mutex;
    using tbb::spin_mutex;
    using tbb::spin_rw_mutex;
    using tbb::speculative_spin_mutex;
    using tbb::speculative_spin_rw_mutex;

    using tbb::cache_aligned_allocator;
    using tbb::cache_aligned_resource;
    using tbb::scalable_allocator;
    using tbb::scalable_memory_resource;
    using tbb::tbb_allocator;
#if TBB_PREVIEW_MEMORY_POOL
    using tbb::memory_pool_allocator;
    using tbb::memory_pool;
    using tbb::fixed_pool;
#endif

    using tbb::combinable;
    using tbb::enumerable_thread_specific;
    using tbb::flattened2d;
    using tbb::flatten2d;
    using tbb::ets_key_usage_type;
    using tbb::ets_key_per_instance;
    using tbb::ets_no_key;
    using tbb::ets_suspend_aware;

    using tbb::global_control;
    using tbb::attach;
    using tbb::finalize;
    using tbb::task_scheduler_handle;
#if !__TBB_DISABLE_SPEC_EXTENSIONS
    namespace ext {
        using tbb::ext::assertion_handler_type;
        using tbb::ext::set_assertion_handler;
        using tbb::ext::get_assertion_handler;
    } // namespace ext
#endif

    namespace task {
        using tbb::task::suspend_point;
        using tbb::task::resume;
        using tbb::task::suspend;
        using tbb::task::current_context;
    } // namespace task

    using tbb::task_arena;
    using tbb::create_numa_task_arenas;
#if TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    using tbb::is_inside_task;
#endif

    namespace this_task_arena {
        using tbb::this_task_arena::current_thread_index;
        using tbb::this_task_arena::max_concurrency;
        using tbb::this_task_arena::isolate;
        using tbb::this_task_arena::enqueue;
#if TBB_PREVIEW_PARALLEL_PHASE
        using tbb::this_task_arena::start_parallel_phase;
        using tbb::this_task_arena::end_parallel_phase;
#endif
    } // namespace this_task_arena

    using tbb::task_group_context;
    using tbb::task_group;
#if TBB_PREVIEW_ISOLATED_TASK_GROUP
    using tbb::isolated_task_group;
#endif
    using tbb::task_group_status;
    using tbb::not_complete;
    using tbb::complete;
    using tbb::canceled;
    using tbb::is_current_task_group_canceling;
    using tbb::task_handle;
#if TBB_PREVIEW_TASK_GROUP_EXTENSIONS
    using tbb::task_completion_handle;
#endif
    using tbb::task_scheduler_observer;

    using tbb::numa_node_id;
    using tbb::core_type_id;

    namespace info {
        using tbb::info::numa_nodes;
        using tbb::info::core_types;
        using tbb::info::default_concurrency;
    } // namespace info

    using tbb::user_abort;
    using tbb::bad_last_alloc;
    using tbb::unsafe_wait;
    using tbb::missing_wait;

    using tbb::tick_count;

    namespace flow {
        // TODO: should abstract APIs be part of module
        using tbb::flow::receiver;
        using tbb::flow::sender;
        using tbb::flow::graph_node;

        using tbb::flow::reset_flags;
        using tbb::flow::rf_reset_protocol;
        using tbb::flow::rf_reset_bodies;
        using tbb::flow::rf_clear_edges;

        using tbb::flow::continue_msg;
        using tbb::flow::input_node;
        using tbb::flow::multifunction_node;
        using tbb::flow::split_node;
        using tbb::flow::output_port;
        using tbb::flow::indexer_node;
        using tbb::flow::tagged_msg;
        using tbb::flow::cast_to;
        using tbb::flow::is_a;
        using tbb::flow::continue_node;
        using tbb::flow::overwrite_node;
        using tbb::flow::write_once_node;
        using tbb::flow::broadcast_node;
        using tbb::flow::buffer_node;
        using tbb::flow::queue_node;
        using tbb::flow::sequencer_node;
        using tbb::flow::priority_queue_node;
        using tbb::flow::limiter_node;
        using tbb::flow::join_node;
        using tbb::flow::input_port;
        using tbb::flow::copy_body;
        using tbb::flow::make_edge;
        using tbb::flow::remove_edge;
        using tbb::flow::tag_value;
        using tbb::flow::composite_node;
        using tbb::flow::async_node;
        using tbb::flow::node_priority_t;
        using tbb::flow::no_priority;
#if TBB_PREVIEW_FLOW_GRAPH_EXTENSIONS
        using tbb::flow::follows;
        using tbb::flow::precedes;
        using tbb::flow::make_node_set;
        using tbb::flow::make_edges;
#endif
        using tbb::flow::rejecting;
        using tbb::flow::reserving;
        using tbb::flow::queueing;
        using tbb::flow::lightweight;
        using tbb::flow::key_matching;
        using tbb::flow::tag_matching;
        using tbb::flow::queueing_lightweight;
        using tbb::flow::rejecting_lightweight;
    } // namespace flow

    namespace profiling {
        using tbb::profiling::set_name;
        using tbb::profiling::event;
    } // namespace profiling
} // export namespace tbb
