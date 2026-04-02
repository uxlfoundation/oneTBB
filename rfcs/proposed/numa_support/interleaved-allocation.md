# API to allocate memory interleaved between NUMA nodes

*Note:* This document is a sub-RFC of the [umbrella RFC about improving NUMA
support](README.md). 

## Motivation

There are two kinds of NUMA-related performance bottlenecks: latency increasing due to
access to a remote node and bandwidth-limited simultaneous access from different CPUs to
a single NUMA memory node. A well-known method to mitigate both is random distribution of
memory objects that are accessed from different CPUs. This can be achieved either by
employing a first-touch policy of NUMA memory allocation or via special platform-dependent
API. Generally, the latter requires less overhead.

## Requirements to public API

To perform allocation of large blocks of special kind of memory there is no need to keep
state, so malloc-like function fits well. There are two possible parameters: `interleaving
step` and `list of NUMA nodes to perform allocations on`. The function serves as a
provider of memory blocks with at least page granularity and doesn't employ internal
caching. So, to support high-performance, smaller and repetitive allocations `std::pmr` or
other solutions should be used.

`interleaving step` has page granularity. Currently there are no clear use cases for
granularity more than page size.

`list of nodes for allocation` is conceptually a set of `tbb::numa_node_id`. However,
because `tbb::numa_nodes()` returns `std::vector` and creating a `std::set` from it
requires allocation, `vector` can be used. Because semantics of `tbb::numa_node_id` is
not defined, we can't use it to construct e.g., a bit mask. Allocation that is unbalanced
between NUMA nodes doesn't seem to have useful applications, so repeated elements in `list
of nodes` is an error.

One use case for `list of nodes` argument is the desire to run parallel activity on subset of
nodes and so get memory only from those nodes.

Most common usage of the allocation function is expected only with `size` parameter.

```c++
void *tbb::numa::alloc_interleaved(size_t size, size_t interleaving_step = 0,
                                   const std::vector<tbb::numa_node_id> *nodes = nullptr);
void tbb::numa::free_interleaved(void *ptr, size_t size);
```

## Implementation details

Under Linux, only allocations with default interleaving can be supported via HWLOC. Other
interleaving steps require direct libnuma usage, that creates yet another run-time
dependency. It's possible to implement allocation with constant number of system call wrt
allocation size.

Under Windows, starting Windows 10 and WS 2016, `VirtualAlloc2(MEM_REPLACE_PLACEHOLDER)`
can be used to provide desired interleaving, but number of system calls is proportional to
allocation size. For older Windows, either fallback to `VirtualAlloc` or manual touching
from threads pre-pinned to NUMA nodes can be used.

There is no NUMA memory support under macOS, so the implementation can only fall back to
`malloc`.

## Open Questions

When non-default `interleaving step` can be used?

`size` argument for `free_interleaved()` appeared because what we have is wrappers over
`mmap`/`munmap` and there is no place to put the size after memory is allocated. We can
put it in, say, an internal cumap. Is it look useful?
