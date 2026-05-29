.. _numa_interleaved_allocation:

API to allocate memory interleaved between NUMA nodes
=====================================================

.. note::
    To enable this feature, set the ``TBB_PREVIEW_NUMA_ALLOCATION`` macro to 1. When available and enabled,
    the feature-test macro ``TBB_HAS_NUMA_ALLOCATION`` is defined.

.. contents::
    :local:
    :depth: 2

Description
***********

A well-known method to improve performance on NUMA systems is to interleave memory between several NUMA
nodes. There are two parameters that control the interleaving: the set of NUMA nodes across which memory is
allocated and the chunk size used for interleaving. The first parameter allows users to select a subset of
NUMA nodes, which may be desirable if a parallel algorithm uses only part of the available NUMA nodes. The
second parameter controls the granularity of interleaving, which may be desirable to optimize for specific
access patterns. The allocation/deallocation functions call the OS directly. If some form of caching is
desirable, it can be implemented on top of this API.

Under Linux*, the API uses the ``libnuma`` library, which must be available at runtime. If the library is not
available, the allocation functions fall back to standard memory allocation. On Windows*, the API uses
functionality available starting from Microsoft* Windows* 10 / Microsoft* Windows* Server 2016; on older
versions of Microsoft* Windows*, the allocation functions also fall back to standard memory allocation.

.. note::
    By default, Docker environment blocks ``move_pages`` system call, which is used for interleaved memory
    allocation. For successive allocation, this syscall must be unblocked.

API
***

Header
------

.. code:: cpp

    #define TBB_PREVIEW_NUMA_ALLOCATION 1
    #include <oneapi/tbb/numa_allocation.h>

Synopsis
--------

.. code:: cpp

    namespace oneapi {
        namespace tbb {
            inline void* allocate_numa_interleaved(size_t bytes,
                                                   const std::vector<tbb::numa_node_id>& nodes,
                                                   size_t bytes_per_chunk = 0);

            inline void* allocate_numa_interleaved(size_t bytes, size_t bytes_per_chunk = 0);

            inline void deallocate_numa_interleaved(void* ptr, size_t bytes);
        } // namespace tbb
    } // namespace oneapi

Functions
---------

.. cpp:function:: void* allocate_numa_interleaved(size_t bytes, const std::vector<tbb::numa_node_id>& nodes, \
                  size_t bytes_per_chunk = 0)

    **Returns:** Allocated memory interleaved between specified NUMA ``nodes`` with interleaved chunk size of
    ``bytes_per_chunk``. In case of allocation failure or invalid arguments, returns ``nullptr``.

    If ``nodes`` contains duplicates, the memory load is proportional to the number of occurrences of each
    node. ``nodes`` must not be empty. ``bytes_per_chunk`` must be a multiple of the system page size. If
    ``bytes_per_chunk`` is zero, a system page size is used. `bytes` must be non-zero. Allocated memory
    contains zeros and is aligned to the system page size.
    

.. cpp:function:: void* allocate_numa_interleaved(size_t bytes, size_t bytes_per_chunk = 0)

    Same as the above but allocates memory from all available NUMA nodes.

.. cpp:function:: void deallocate_numa_interleaved(void* ptr, size_t bytes)

    Deallocates memory allocated by ``allocate_numa_interleaved``. The behavior is undefined if ``bytes`` is
    not the same as the one passed to the corresponding allocation function, ``ptr`` was not
    allocated by ``allocate_numa_interleaved`` or if the same pointer is deallocated more than once.

Example
*******

The code below provides a simple example with direct use of interleaved memory allocation as arrays.

.. literalinclude:: ./examples/allocate_numa_interleaved_basic.cpp
    :language: c++
    :start-after: /*begin_allocate_numa_interleaved_example*/
    :end-before: /*end_allocate_numa_interleaved_example*/

In the following example, interleaved memory is wrapped in ``tbb::memory_pool``. This allows to amortize
allocation overhead.

.. literalinclude:: ./examples/allocate_numa_interleaved_pool.cpp
    :language: c++
    :start-after: /*begin_allocate_numa_interleaved_pool_example*/
    :end-before: /*end_allocate_numa_interleaved_pool_example*/
