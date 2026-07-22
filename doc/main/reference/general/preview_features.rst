.. _preview_features

Preview Features
================
**[preview_features]**

A preview feature is a component of |short_name| introduced to receive
early feedback from users.

The key properties of a preview feature are:

- It is off by default and must be explicitly enabled.
- It is intended to have a high-quality implementation.
- There is no guarantee of future existence or compatibility.
- It may have limited or no support in tools such as correctness analyzers, profilers and debuggers.
- ABI compatibility between translation units is guaranteed only when all are built
  with the same set of enabled preview features, including the case where all
  preview features are disabled in all translation units.

.. caution::
    A preview feature is subject to change in the future. It might be removed or significantly
    altered in future releases. Changes to a preview feature do not require the
    usual deprecation and removal process. Therefore, using preview features in production code
    is strongly discouraged.

Unless explicitly stated in a preview feature page, enable a preview feature by
defining the corresponding ``TBB_PREVIEW_``-prefixed macro before including any headers.
This requirement is strict because |short_name| may be included indirectly through other headers.

.. TODO: correct the paths after adding the pages

.. TODO: should it be links instead?

.. toctree::
    :titlesonly:

    type_specified_message_keys
    scalable_memory_pools
    helpers_for_expressing_graphs
    concurrent_lru_cache_cls
    task_group_extensions
    isolated_task_group
    custom_mutex_chmap
    try_put_and_wait
    parallel_phase_for_task_arena
    fg_resource_limiting
    core_type_selector
    numa_interleaved_allocation
