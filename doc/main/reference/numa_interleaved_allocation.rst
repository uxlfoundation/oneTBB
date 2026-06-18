.. _numa_interleaved_allocation:

Allocate Memory Interleaved between NUMA Nodes
==============================================

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
access patterns.

Allocated memory is not split or cached. It's returned back immediately upon deallocation. Interleaving is
only a recommendation. Memory may be placed on different NUMA nodes or in a different order than requested.

Under Linux*, the API uses the ``libnuma`` library, which must be available at runtime. If the library is not
available, the allocation functions fall back to standard memory allocation. On Windows*, the API uses
functionality available starting from Microsoft* Windows* 10 / Microsoft* Windows* Server 2016; on older
versions of Microsoft* Windows*, the allocation functions also fall back to standard memory allocation.

.. note::
    By default, Docker environment blocks ``move_pages`` system call, which is used for interleaved memory
    allocation. For successful allocation, this syscall must be unblocked.

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

    **Returns:** A pointer to the allocated memory interleaved between the specified NUMA ``nodes`` in chunks
    of ``bytes_per_chunk``. In case of allocation failure or invalid arguments, returns ``nullptr``.

    **Requirements:** ``bytes`` must be non-zero, ``nodes`` must not be empty, and ``bytes_per_chunk``
    must be a multiple of the system page size.
    
    If ``nodes`` contains some NUMA node IDs more than once, each of these IDs independently
    participates in the interleaving order. That allows flexible load balancing between nodes.
    If ``bytes_per_chunk`` is zero, the system page size is used. The allocated memory contains zeros 
    and is aligned to the system page size.
    

.. cpp:function:: void* allocate_numa_interleaved(size_t bytes, size_t bytes_per_chunk = 0)

    Same as the above, but allocates memory interleaved across all available NUMA nodes.

.. cpp:function:: void deallocate_numa_interleaved(void* ptr, size_t bytes)

    Deallocates memory allocated by ``allocate_numa_interleaved``.
    
    **Requirements:** ``ptr`` must be previously allocated by ``allocate_numa_interleaved`` and not yet
    deallocated, and ``bytes`` must be the same as the corresponding value used to allocate the memory.
    Otherwise, the behavior is undefined.

Examples
********

The code below provides a simple example with direct use of the allocated memory as a NUMA-interleaved array.

.. literalinclude:: ./examples/allocate_numa_interleaved_basic.cpp
    :language: c++
    :start-after: /*begin_allocate_numa_interleaved_example*/
    :end-before: /*end_allocate_numa_interleaved_example*/

In the following example, interleaved memory is wrapped in ``tbb::memory_pool``. This allows to amortize
allocation overhead and construct a container that uses interleaved NUMA memory.

.. literalinclude:: ./examples/allocate_numa_interleaved_pool.cpp
    :language: c++
    :start-after: /*begin_allocate_numa_interleaved_pool_example*/
    :end-before: /*end_allocate_numa_interleaved_pool_example*/
