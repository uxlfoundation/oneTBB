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

## The overall approach

A free stateless function, similar to malloc, is sufficient for the allocation of large
blocks of memory, contiguous in the address space. This function allocates whole
memory pages and does not employ internal caching. If smaller and repetitive allocations
are needed, then `std::pmr` or other solutions should be used.

To guide the mapping of memory across NUMA nodes, two additional parameters are proposed:
`interleaving step` and `the list of NUMA nodes to get the memory from`.
`interleaving step` is the size of the contiguous memory block from a particular NUMA node,
it has page granularity. Currently there are no clear use cases for granularity more than page size.

`list of nodes for allocation` is `std::vector<tbb::numa_node_id>` to be compatible with a
value returned from `tbb::numa_nodes()`. `libnuma` supports a subset of NUMA nodes for
allocation, but those nodes are loaded equally. Having `vector` allows us to express an
unbalanced load. Example: allocation over the list of nodes [3, 0, 3] uses 2/3 memory from
node 3 and 1/3 from node 0.

One use case for `list of nodes` argument is the desire to run parallel activity on subset
of nodes and so get memory only from those nodes.

For the purpose of spreading memory access for better throughput it is sufficient to specify
only the allocation size. In this case, `interleaving step` defaults to the page size (in bytes)
and the memory is spread across all NUMA nodes.

## API proposal

### Header file

None of the existing public TBB headers is really a good fit for the NUMA allocation API,
with `tbb_allocator.h` being probably better than others.

If a new header is added, it could be aimed to contain either these APIs only (e.g.,
`numa_allocation.h`) or a broader set of NUMA-related APIs, both existing and added later
(`numa.h`). In the latter case the header should also include other public headers such as
`task_arena.h` and `info.h`.

The choice might depend on whether the API is initially delivered as a preview feature or not.
For the preview feature it seems fine to add it to an existing header, while for production
a new header seems better.

### Namespace

As NUMA related features are added gradually and interoperate with the existing API,
we already have a few names containing `numa` in `namespace tbb`. It seems better to follow
this practice rather than to introduce a new nested namespace.

### Functions

With three possible arguments for the allocation. of which two might be optional, a single
allocation function does not seem sufficient. Therefore two overloads are proposed:

```c++
void *allocate_numa_interleaved (size_t bytes,
                                 const std::vector<tbb::numa_node_id>& nodes,
                                 size_t bytes_per_chunk = 0);
void *allocate_numa_interleaved (size_t bytes, size_t bytes_per_chunk = 0);
```

The default value of `0` for the interleaving step (`bytes_per_chunk`) indicates that
the implementation should choose one automatically. If specified, the step should be
a multiple of the memory page size.

In addition, function templates are proposed to allocate memory for objects of a certain type:

```c++
template <typename T>
T *allocate_numa_interleaved (size_t count,
                              const std::vector<tbb::numa_node_id>& nodes,
                              size_t count_per_chunk = 0);
template <typename T>
T *allocate_numa_interleaved (size_t count, size_t count_per_chunk = 0);
```

Instead of raw size in bytes, these functions take a number of objects of the given type
to indicate the memory size and the interleaving step. The latter (`count_per_chunk`) should be
such that `count_per_chunk * sizeof(T)` is a multiple of the memory page size.

Last, the functions that free the previously allocated memory take an allocation address
and the memory size. The latter should match the allocated size at the given address.

```c++
void free_numa_interleaved (void *ptr, size_t bytes);
template <typename T>
void free_numa_interleaved (T *ptr, size_t count);
```

### Error handling

Two types of run-time errors might appear in these functions:

- **Usage errors**: the provided arguments do not match the requirements. Note that some usage errors
  are non-verifiable, such as allocation size mismatch in `free_numa_interleaved`.
- **System errors**: those returned by the system API used in the implementation.

Since the API does not allow to return any error code directly, the options for error handling
are:

1. Do nothing except returning `nullptr`; not even argument checks. The requirements to
   the arguments stem from those of the system API, where the arguments or their derivatives
   are passed to. Therefore usage errors may effectively convert to system errors and result
   in a failed allocation or deallocation.
   
   Together with declaring the behavior undefined if the requirements are not met, this is the simplest
   from the implementation viewpoint. On the downside, there is no way for users to known that
   `free_numa_interleaved` has failed (as it returns nothing), and no diagnostics for usage errors.

2. Throw an exception: `bad_alloc` or/and more specific types like `system_error` and
   `invalid_argument`. Note that `bad_alloc`, while typically used as a signal that memory
   could not be allocated, unfortunately does not allow to set a custom message, and also
   does not fit well semantically to deallocation.

   The downside is that we know some customers do not want to use exceptions in their codebases.
   Therefore this probably should not be the only approach, and a "fallback" is needed.

3. Use `errno` to "return" an error code, such as `EINVAL` for bad arguments, `ENOMEM` for lack
   of memory, etc. The downside is that this approach is currently not used in TBB, and can be
   seen as a sign of inconsistent design. It is also not quite "authentic" for C++, and it
   does not have error messages for customized diagnostics.

4. Use assertions, probably in both debug and release builds. The behavior of assertions
   is reasonably well defined in [the TBB specification](
   https://uxlfoundation.github.io/oneAPI-spec/spec/elements/oneTBB/source/configuration/enabling_debugging_features.html#tbb-use-assert-macro)
   and together with
   [assertion handlers](https://uxlfoundation.github.io/oneTBB/main/reference/assertion_handler.html)
   can be used for decent error handling and diagnostics.

   However, there is no recovery from an assertion, so it does not fit well for handling system errors
   during memory allocation.

The choice can again be different for preview vs. production. Also it seems there is no universally
best approach, so we might need to use different ones, depending on a specific error.

## ABI entry points

TODO


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
