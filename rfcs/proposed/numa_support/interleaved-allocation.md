# API to allocate memory interleaved between NUMA nodes

*Note:* This document is a sub-RFC of the [umbrella RFC about improving NUMA
support](README.md). 

## Motivation

There are two kinds of NUMA-related performance bottlenecks: latency increasing due to
access to a remote node and bandwidth-limited simultaneous access from different CPUs to
a single NUMA memory node. A well-known method to mitigate both is a distribution of
memory objects that are accessed from different CPUs to different NUMA nodes in such a way
that matches an access pattern. If the access pattern is complex enough, a simple
round-robin distribution can be good enough. The distribution can be achieved either by
employing a first-touch policy of NUMA memory allocation or via special platform-dependent
API. Generally, the latter requires less overhead.

## Requirements to public API

A free stateless function, similar to malloc, is sufficient for the allocation of large
blocks of memory, contiguous in the address space. To guide the mapping of memory
across NUMA nodes, two additional parameters are proposed: `interleaving step`
and `the list of NUMA nodes to get the memory from`. This function allocates whole
memory pages and does not employ internal caching. If smaller and repetitive allocations
are needed, then `std::pmr` or other solutions should be used.

`interleaving step` is the size of the contiguous memory block from a particular NUMA
node, it has page granularity. Currently there are no clear use cases for granularity more
than page size.

`list of nodes for allocation` is `std::vector<tbb::numa_node_id>` to be compatible with a
value returned from `tbb::numa_nodes()`. `libnuma` supports a subset of NUMA nodes for
allocation, but those nodes are loaded equally. Having `vector` allows us to express an
unbalanced load. Example: allocation over the list of nodes [3, 0, 3] uses 2/3 memory from
node 3 and 1/3 from node 0.

One use case for `list of nodes` argument is the desire to run parallel activity on subset
of nodes and so get memory only from those nodes.

Most common usage of the allocation function is expected only with `size` parameter.
In this case, `interleaving_step` defaults to the page size and memory is allocated on all
NUMA nodes.

The following functions are provided to illustrate the conceptual API, not yet as the
recommended new API.

```c++
void *alloc_interleaved(size_t size, size_t interleaving_step = 0,
                        const std::vector<tbb::numa_node_id> *nodes = nullptr);
void free_interleaved(void *ptr, size_t size);
```

## Implementation details

Under Linux, only allocations with default interleaving can be supported via HWLOC. Other
interleaving steps require direct libnuma usage, that creates yet another run-time
dependency. Using `move_pages` it's possible to implement allocation with constant number
of system calls wrt allocation size.

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

Semantics of even distribution of data between NUMA nodes is straightforward: to equally
balance work between the nodes. Why might someone want to distribute data unequally? Can
it be a form of fine-tuning “node 0 already loaded with access to static data, let’s
decrease the load a little”?
