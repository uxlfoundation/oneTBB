.. _reference:

|short_name| API Reference
==========================

For oneTBB API Reference, refer to `oneAPI Specification <https://github.com/uxlfoundation/oneAPI-spec>`_. The current supported
version of oneAPI Specification is 1.0.

Specification extensions
************************

|full_name| implements the `oneTBB specification <https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/nested-index>`_.
This document provides additional details or restrictions where necessary.
It also describes features that are not included in the oneTBB specification.

.. toctree::
    :titlesonly:

    parallel_for_each_semantics
    parallel_sort_ranges_extension
    scalable_memory_pools/malloc_replacement_log
    rvalue_reduce

Preview features
****************

A preview feature is a component of oneTBB introduced to receive early feedback from
users.

The key properties of a preview feature are:

- It is off by default and must be explicitly enabled.
- It is intended to have a high quality implementation.
- There is no guarantee of future existence or compatibility.
- It may have limited or no support in tools such as correctness analyzers, profilers and debuggers.


.. caution::
    A preview feature is subject to change in future. It might be removed or significantly
    altered in future releases. Changes to a preview feature do NOT require
    usual deprecation and removal process. Therefore, using preview features in production code
    is strongly discouraged.

.. toctree::
    :titlesonly:

    type_specified_message_keys
    scalable_memory_pools
    helpers_for_expressing_graphs
    concurrent_lru_cache_cls
    task_group_extensions
    custom_mutex_chmap
    try_put_and_wait
    parallel_phase_for_task_arena
    blocked_nd_range_ctad
