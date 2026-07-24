.. _preview_features:

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
set of enabled preview features, including the case where all preview
features are disabled in all translation units.

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

.. toctree::
    :titlesonly:

    flow_graph/type_specified_message_keys.rst
    flow_graph/helpers_for_expressing_graphs.rst
    flow_graph/try_put_and_wait.rst
    flow_graph/resource_limiting.rst

Task Scheduler
--------------

.. toctree::
    :titlesonly:

    task_scheduler/task_group_extensions.rst
    task_scheduler/isolated_task_group.rst
    task_scheduler/parallel_phase_for_task_arena.rst
    task_scheduler/core_type_selector.rst

Containers
----------

.. toctree::
    :titlesonly:

    containers/custom_mutex_chmap.rst
    containers/concurrent_lru_cache.rst

Memory Allocation
-----------------
    
.. toctree::
    :titlesonly:

    memory_allocation/scalable_memory_pools.rst
    memory_allocation/numa_interleaved_allocation.rst
