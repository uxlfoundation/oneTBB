.. _api_abi_changes:

API and ABI Change Log
======================

This document tracks API and ABI changes across oneTBB releases. Each release may introduce new APIs, modify existing ones, or add new symbols to the binary interface.

Summary Table
-------------

.. list-table::
   :header-rows: 1
   :widths: auto

   * - Library Version
     - Binary Version
     - Date
     - Release Notes
     - API Changes
     - ABI Changes

   * - 2023.0.0
     - 12.18
     - April 2026
     - *Release Notes TBD*
     - Yes
     - Layout changes

   * - 2022.3.0
     - 12.17
     - Oct 2025
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20223-release-notes>`_
     - Yes
     - New entry points

   * - 2022.2.0
     - 12.16
     - Jun 2025
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20222-release-notes>`_
     - No
     - No

   * - 2022.1.0
     - 12.15
     - Mar 2025
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20221-release-notes>`_
     - Yes
     - New entry points

   * - 2022.0.0
     - 12.14
     - Oct 2024
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20220-release-notes>`_
     - Yes
     - New entry points and layout changes

   * - 2021.13.0
     - 12.13
     - Jun 2024
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-202113-release-notes>`_
     - Yes
     - No

   * - 2021.12.0
     - 12.12
     - Apr 2024
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-202112-release-notes>`_
     - No
     - No

   * - 2021.11.0
     - 12.11
     - Nov 2023
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-202111-release-notes>`_
     - No
     - No

   * - 2021.10.0
     - 12.10
     - Jul 2023
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-202110-release-notes>`_
     - Yes
     - No

   * - 2021.9.0
     - 12.9
     - Apr 2023
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20219-release-notes>`_
     - Yes
     - No

   * - 2021.8.0
     - 12.8
     - Feb 2023
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20218-release-notes>`_
     - Yes
     - No

   * - 2021.7.0
     - 12.7
     - Oct 2022
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20217-release-notes>`_
     - No
     - No

   * - 2021.6.0
     - 12.6
     - Sep 2022
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20216-release-notes>`_
     - Yes
     - No

   * - 2021.5.0
     - 12.5
     - Dec 2021
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20215-release-notes>`_
     - Yes
     - No

   * - 2021.4.0
     - 12.4
     - Oct 2021
     - `Release Notes <https://github.com/uxlfoundation/oneTBB/blob/master/RELEASE_NOTES.md#onetbb-20214-release-notes>`_
     - Yes
     - New entry points

   * - 2021.3.0
     - 12.3
     - Jun 2021
     - `Release Notes <https://www.intel.com/content/www/us/en/developer/articles/release-notes/intel-oneapi-threading-building-blocks-release-notes.html>`_
     - Yes
     - New entry points

   * - 2021.2.0
     - 12.2
     - Apr 2021
     - `Release Notes <https://www.intel.com/content/www/us/en/developer/articles/release-notes/intel-oneapi-threading-building-blocks-release-notes.html>`_
     - Yes
     - New entry points

   * - 2021.1.1
     - 12.1
     - Dec 2020
     - `Release Notes <https://www.intel.com/content/www/us/en/developer/articles/release-notes/intel-oneapi-threading-building-blocks-release-notes.html>`_
     - Initial oneTBB API
     - Initial oneTBB ABI

Release Details
---------------

2023.0.0
~~~~~~~~

**API Changes:**

- `create set of NUMA bound arenas <https://github.com/uxlfoundation/oneTBB/blob/master/rfcs/supported/numa_support/create-numa-arenas.md>`_
- added additional deduction guides for flow graph and blocked_range_nd (link TBD)
- flow graph join_node and indexer_node now support 10 or more input ports (link TBD)
- `preview: wait for single task in task_group <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/proposed/task_group_wait_single_task>`_
- `preview: resource_limited_node and resource_limiter classes <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/proposed/flow_graph_serializers>`_
- `preview: advanced core-type selection <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/experimental/core_types>`_
- `preview: global control parameter for default block time behavior <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/experimental/parallel_phase_for_task_arena>`_

**ABI Changes:**

- ordered container layout changes for scalability improvements

**Notes:**

The ABI is backwards compatible but issues can arise for partial recomplilation cases, where objects are passed across compilation units built
with different versions of the library.

2022.3.0
~~~~~~~~

**API Changes:**

- `task_arena enqueue and wait_for specific task_group <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/proposed/task_arena_waiting>`_
- `custom assertion handler support <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/supported/assertion_handler>`_
- `preview of dynamic task graph <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/proposed/task_group_dynamic_dependencies>`_

**ABI Changes:**

- set/get_assertion_handler
- current_task_ptr

**Notes:**

set/get_assertion_handler symbols are used by custom assertion handler support, current_task_ptr is used by preview of task_group dependencies

2022.2.0
~~~~~~~~

No API or ABI changes in this release.

2022.1.0
~~~~~~~~

**API Changes:**

- `Added explicit deduction guides for blocked_nd_range <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/experimental/blocked_nd_range_ctad>`_
- `preview of parallel phase <https://github.com/uxlfoundation/oneTBB/tree/master/rfcs/experimental/parallel_phase_for_task_arena>`_

**ABI Changes:**

- enter/exit_parallel_phase

**Notes:**

enter/exit_parallel_phase is only used by preview of parallel phase. **WARNING:** there was temporary, inadvertant change that made the `unsafe_wait <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/task_scheduler/scheduling_controls/task_scheduler_handle_cls>`_ exception local for this release only.

2022.0.0
~~~~~~~~

**API Changes:**

- `Preview of flow graph try_put_and_wait <https://github.com/uxlfoundation/oneTBB/pull/1513>`_

**ABI Changes:**

- get_thread_reference_vertex
- execution_slot

**Notes:**

`The layouts of task_group and flow::graph were changed to improve scalability. The binary library is backwards compatible 
but issues can arise for partial recomplilation cases (see linked discussion) <https://github.com/uxlfoundation/oneTBB/discussions/1371>`_. get_thread_reference_vertex and execution_slot added for scalability improvements.

2021.13.0
~~~~~~~~~

**API Changes:**

- `Better rvalues support for parallel_reduce and parallel_deterministic_reduce functional API <https://github.com/uxlfoundation/oneTBB/pull/1307>`_

**ABI Changes:**

No ABI changes in this release.

2021.12.0
~~~~~~~~~

No API or ABI changes in this release.

2021.11.0
~~~~~~~~~

No API or ABI changes in this release.

**Notes:**

Thread Composability Manager support introduced. It can be enabled by setting "TCM_ENABLE" environmental variable to 1

2021.10.0
~~~~~~~~~

**API Changes:**

- `parallel algorithms and Flow Graph nodes allowed to accept pointers to the member functions and member objects as the user-provided callables <https://github.com/uxlfoundation/oneTBB/pull/880>`_
- `Added missed member functions, such as assignment operators and swap function, to the concurrent_queue and concurrent_bounded_queue containers <https://github.com/uxlfoundation/oneTBB/pull/1033>`_

**ABI Changes:**

No ABI changes in this release.

2021.9.0
~~~~~~~~

**API Changes:**

- `Hybrid core type constraints are fully supported and no longer guarded by preview macro <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.2-rev-1/elements/onetbb/source/task_scheduler/task_arena/task_arena_cls#_CPPv4N11constraints9core_typeE>`_

**ABI Changes:**

No ABI changes in this release.

**Notes:**

Hybrid CPU support is now production features, including use of symbols introduced in 2021.2.0

2021.8.0
~~~~~~~~

**API Changes:**

- `Fixed concurrent_bounded_queue return type to match specification <https://github.com/uxlfoundation/oneTBB/issues/807>`_

**ABI Changes:**

No ABI changes in this release.

2021.7.0
~~~~~~~~

No API or ABI changes in this release.

2021.6.0
~~~~~~~~

**API Changes:**

- `Improved support and use of the latest C++ standards for parallel_sort that allows using this algorithm with user-defined and standard library-defined objects with modern semantics <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.2-rev-1/elements/onetbb/source/algorithms/functions/parallel_sort_func>`_
- The following features are now fully functional: `task_arena extensions <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.2-rev-1/elements/onetbb/source/task_scheduler/task_arena/task_arena_cls>`_, `collaborative_call_once <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/algorithms/functions/collaborative_call_once_func>`_, heterogeneous overloads for concurrent_hash_map, and `task_scheduler_handle <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.2-rev-1/elements/onetbb/source/task_scheduler/scheduling_controls/task_scheduler_handle_cls>`_

**ABI Changes:**

No ABI changes in this release.

2021.5.0
~~~~~~~~

**API Changes:**

- Preview of task_group interface with a new run_and_wait overload to accept task_handle

**ABI Changes:**

No ABI changes in this release.

2021.4.0
~~~~~~~~

**API Changes:**

- Preview of collaborative_call_once algorithm

**ABI Changes:**

- notify_waiters

2021.3.0
~~~~~~~~

**API Changes:**

- Extended the high-level task API to simplify migration from TBB to oneTBB
- Added mutex and rw_mutex that are suitable for long critical sections and resistant to high contention
- Added ability to customize the concurrent_hash_map mutex type
- Added heterogeneous lookup, erase, and insert operations to concurrent_hash_map

**ABI Changes:**

- enqueue(d1::task&, d1::task_group_context&, d1::task_arena_base*)
- is_writer for queuing_rw_mutex
- wait_on_address
- notify_by_address/address_all/address_one

2021.2.0
~~~~~~~~

**API Changes:**

- Three-way comparison operators for concurrent ordered containers and concurrent_vector
- Preview of Hybrid core type constraints

**ABI Changes:**

- core_type_count
- fill_core_type_indices
- constraints_threads_per_core
- constraints_default_concurrency

**Notes:**

New symbols used by preview of Hybrid CPU support (entered production in 2021.9).

2021.1.1
~~~~~~~~

**API Changes:**

- `Initial modernized oneTBB API <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/v1.0-rev-3/elements/onetbb/source/nested-index#onetbb-section>`_

**ABI Changes:**

- Initial oneTBB ABI
