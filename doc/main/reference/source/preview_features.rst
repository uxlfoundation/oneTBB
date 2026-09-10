.. _preview_features:

================
Preview Features
================
**[preview_features]**

A preview feature is a component of |short_name| introduced to gather early feedback
from users.

The key properties of a preview feature are:

- It is off by default and must be explicitly enabled.
- It aims to provide high implementation quality.
- There is no guarantee of future existence or compatibility.
- It may have limited or no support in tools such as correctness
  analyzers, profilers, and debuggers.
- ABI compatibility between translation units is guaranteed only
  when all are built with the same version of the library and the same
  set of enabled preview features.

.. caution::
    A preview feature is subject to change in the future. It might
    be removed or significantly altered in future releases. Changes to
    a preview feature do not require the usual deprecation and removal
    process. Therefore, using preview features in production code is
    strongly discouraged.

Unless explicitly stated in a preview feature page, enable a preview
feature by defining the corresponding ``TBB_PREVIEW_``-prefixed macro
before including any headers.
This requirement is strict because |short_name| may be included
indirectly through other headers.

Flow Graph
----------

.. list-table::
   :header-rows: 1
   :widths: 32 48 20

   * - Feature
     - Description
     - Enabling macro
   * - :doc:`flow_graph/type_specified_message_keys`
     - Enables ``join_node`` in key-matching mode to obtain keys directly from message types.
     - ``TBB_PREVIEW_FLOW_GRAPH_FEATURES``
   * - :doc:`flow_graph/helpers_for_expressing_graphs`
     - Adds ``make_edges``, ``make_node_set``, ``follows``, and ``precedes`` helper functions for building graphs.
     - ``TBB_PREVIEW_FLOW_GRAPH_FEATURES``
   * - :doc:`flow_graph/waiting_for_single_message`
     - Adds ``try_put_and_wait`` to submit a message and wait only for its associated work to complete.
     - ``TBB_PREVIEW_FLOW_GRAPH_TRY_PUT_AND_WAIT``
   * - :doc:`flow_graph/resource_limiting`
     - Coordinates node access to limited shared resources via ``resource_limiter`` and ``resource_limited_node``.
     - ``TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING``

Task Scheduler
--------------

.. list-table::
   :header-rows: 1
   :widths: 32 48 20

   * - Feature
     - Description
     - Enabling macro
   * - :doc:`task_scheduler/task_group/task_bypass`
     - Allows a ``task_group`` task body to return a ``task_handle`` to run next (scheduler bypass).
     - ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS``
   * - :doc:`task_scheduler/task_group/task_completion_handle`
     - Adds ``task_completion_handle`` to track completion of a submitted ``task_group`` task.
     - ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS``
   * - :doc:`task_scheduler/task_group/dynamic_dependencies`
     - Lets tasks add dependencies dynamically while a ``task_group`` runs.
     - ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS``
   * - :doc:`task_scheduler/task_group/wait_single_task`
     - Adds waiting for an individual ``task_group`` task through its handle.
     - ``TBB_PREVIEW_TASK_GROUP_EXTENSIONS``
   * - :doc:`task_scheduler/task_arena/parallel_phase`
     - Extends ``task_arena`` with an explicit ``parallel_phase`` to control worker participation.
     - ``TBB_PREVIEW_PARALLEL_PHASE``
   * - :doc:`task_scheduler/task_arena/core_type_selector`
     - Adds core-type constraints to ``task_arena`` for hybrid-CPU systems.
     - ``TBB_PREVIEW_TASK_ARENA_CORE_TYPE_SELECTOR``

Containers
----------

.. list-table::
   :header-rows: 1
   :widths: 32 48 20

   * - Feature
     - Description
     - Enabling macro
   * - :doc:`containers/concurrent_lru_cache`
     - Adds ``concurrent_lru_cache`` class for Least Recently Used cache with concurrent operations.  
   * - :doc:`containers/custom_mutex_chmap`
     - Adds a template parameter to choose the reader-writer mutex type for ``concurrent_hash_map``.
     - ``TBB_PREVIEW_CONCURRENT_HASH_MAP_EXTENSIONS``

Memory Allocation
-----------------

.. list-table::
   :header-rows: 1
   :widths: 32 48 20

   * - Feature
     - Description
     - Enabling macro
   * - :doc:`memory_allocation/scalable_memory_pools`
     - Provides ``memory_pool`` and ``fixed_pool`` classes for scalable pooled allocation.
     - ``TBB_PREVIEW_MEMORY_POOL``
   * - :doc:`memory_allocation/numa_interleaved_allocation`
     - Allocates memory interleaved across NUMA nodes.
     - ``TBB_PREVIEW_NUMA_ALLOCATION``
