# API to allocate memory interleaved between NUMA nodes

*Note:* This document is a sub-RFC of the [umbrella RFC about improving NUMA
support](README.md). 

## Motivation

There are two kinds of NUMA-related performance bottlenecks: latency increasing due to
access to a remote node and bandwidth-limited simultaneous access from different CPUs to
a single NUMA memory node. A well-known method to mitigate both is a distribution of
memory objects that are accessed from different CPUs to different NUMA nodes in such a way
that matches the access pattern. If the access pattern is complex enough, a simple
round-robin distribution can be good enough to address at least the second issue.
To address the first issue best, depending on the access pattern it might be necessary
to choose the set of NUMA nodes and/or the amount of memory mapped to a node.

The distribution can be achieved either by employing a first-touch policy of NUMA memory
allocation or via special platform-dependent API. Generally, the latter requires less overhead.

## The overall approach

A free stateless function, similar to malloc, is sufficient for the allocation of large
blocks of memory, contiguous in the address space. This function allocates whole
memory pages and does not employ internal caching. If smaller and repetitive allocations
are needed, then `std::pmr` or other solutions should be used.

To guide the mapping of memory across NUMA nodes, two additional parameters are proposed:
`interleaving step` and `the list of NUMA nodes to get the memory from`.
`interleaving step` is the size of the contiguous memory block from a particular NUMA node,
it has page granularity. Currently there are no clear use cases for granularity more than page size.

`list of nodes for allocation` is `std::vector<tbb::numa_node_id>` to be compatible with
the values returned from `tbb::info::numa_nodes()`. `libnuma` supports a subset of NUMA nodes
for allocation, but those nodes are loaded equally. Using `vector` allows us to express an
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
with `tbb/tbb_allocator.h` being probably better than others. We propose therefore to add
a new header, `tbb/numa_allocation.h`.

Other options were considered and rejected:

- a new header for a broader set of NUMA-related APIs, including that from other public headers
  such as `tbb/task_arena.h` and `tbb/info.h`;
- a new header for a broader set of memory-related APIs, including that from other public headers
  such as `tbb/tbb_allocator.h`.

### Namespace

As NUMA related features are added gradually and interoperate with the existing API,
we already have a few names containing `numa` in `namespace tbb`. It seems better to follow
this practice rather than to introduce a new nested namespace.

### Functions

With three possible arguments for the allocation, of which two might be optional, a single
allocation function does not seem sufficient. Therefore we propose two overloads for memory
allocation, as well as a deallocation function:

```c++
void *allocate_numa_interleaved (size_t bytes,
                                 const std::vector<tbb::numa_node_id>& nodes,
                                 size_t bytes_per_chunk = 0);
void *allocate_numa_interleaved (size_t bytes, size_t bytes_per_chunk = 0);
void deallocate_numa_interleaved (void *ptr, size_t bytes);
```

The default value of `0` for the interleaving step (`bytes_per_chunk`) indicates that
the implementation should choose one automatically. If specified, the step should be
a multiple of the memory page size.

The function that frees the memory takes an allocation address and the memory size,
which should match the allocated size at the given address. This follows the standard practice
for C++ memory allocators and memory resources, and it is needed for the implementation
to properly call system routines. Alternative variants to store the size somehow between
the calls (an internal map, an extra memory page, a shared pointer with custom deleter)
were considered and rejected, as those come with some kind of overhead or/and deviate from
the common C++ API paradigms to allocate/free memory.

The system APIs that will be used in the implementation (see below) zero-initialize
allocated memory. It makes sense for `allocate_numa_interleaved` to keep that guarantee.

While the above functions are seen as the minimally viable set, as an option, we also propose
function templates to allocate and free memory for objects of a certain type:

```c++
template <typename T>
T *allocate_numa_interleaved (size_t count,
                              const std::vector<tbb::numa_node_id>& nodes,
                              size_t count_per_chunk = 0);
template <typename T>
T *allocate_numa_interleaved (size_t count, size_t count_per_chunk = 0);
template <typename T>
void deallocate_numa_interleaved (T *ptr, size_t count);
```

Instead of raw size in bytes, these functions take a number of objects of the given type
to indicate the memory size and the interleaving step. The latter, `count_per_chunk`, should be
such that `count_per_chunk * sizeof(T)` is a multiple of the memory page size.

The template argument for `allocate_numa_interleaved` cannot be deduced and should always be provided
explicitly. The template argument for `deallocate_numa_interleaved` is deducible from the type of
the first argument (pointer). As a downside, an explicit pointer cast to `void*` is required
in order to call the non-typed deallocation function.

No constructors are called in the allocated memory. Unless the type is trivially constructible,
reading from the memory without object initialization results in an undefined behavior.
Similarly, deallocation does not call destructors.

We may also want to add a function that queries the system memory page size, to make
the use of custom interleaving steps simpler and less error-prone.
Such a function should likely go into `info.h` and the `tbb::info` namespace.

### Error handling

Two types of run-time errors might appear in these functions:

- **Usage errors**: the provided arguments do not match the requirements. Note that some usage errors
  are non-verifiable, specifically the allocation size mismatch in `deallocate_numa_interleaved`.
- **System errors**: those returned by the system API used in the implementation.

Since the API does not allow to return any error code directly, the options for error handling
are:

1. Do nothing except returning `nullptr`; not even argument checks. The requirements to
   the arguments stem from those of the system API, which the arguments or their derivatives
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

3. Use assertions, possibly in both debug and release builds. The behavior of assertions
   is reasonably well defined in [the TBB specification](
   https://uxlfoundation.github.io/oneAPI-spec/spec/elements/oneTBB/source/configuration/enabling_debugging_features.html#tbb-use-assert-macro),
   and they can provide decent handling and diagnostics of user errors via
   [assertion handlers](https://uxlfoundation.github.io/oneTBB/main/reference/assertion_handler.html).

   However, there is no recovery from an assertion, so it does not fit well for handling system errors
   during memory allocation.
   
It seems there is no universally best approach, so we might need to use different ones,
depending on a specific error and/or via additional overloads (e.g., with a `nothrow` parameter).
The question remains open.

Meanwhile, the following options were considered and rejected:

- As a fallback, use regular allocation/memory mapping if the interleaved allocation fails.
  The problem with that is that then deallocation should somehow know whether the allocated block
  is interleaved, with the only feasible implementation being an internal map of allocations.

- Use `errno` to "return" an error code, such as `EINVAL` for bad arguments, `ENOMEM` for lack
  of memory, etc. The problems are that this approach is currently not used in TBB, and so can be
  viewed as a sign of inconsistent design. It is also not quite "authentic" for C++, and it
  does not have error messages for customized diagnostics.

- Embed an error code into the return value type. We considered several variants of that:

  a) return only the error code directly and use a separate function parameter for the memory address
  b) combine both into `std::tuple` or a simple custom class
  c) implement a custom analogue of `std::expected` and related APIs from C++23.

  and neither seems to be a good one. The common downside is again that the API would deviate
  to some degree from the common memory allocation practices known to C++ developers. It is also
  the only error handling option that requires error codes to be passed through the ABI entry points.

## ABI entry points

Since there is no memory management involved, the functionality belongs to the main TBB library,
not the `tbbmalloc` library.

The new ABI entry points should be stable and production-ready when added, so that we do not
need to change these later. These functions should operate with raw bytes. Overloads are
discouraged unless absolutely necessary. It is better to avoid the use of standard library types
in the signatures, as that could cause dependencies on specific standard library implementations
and/or versions and potentially require multiple sets of binaries for the same platform.

Given that, the following conceptual function signatures are proposed:

```c++
void *allocate_interleaved (size_t bytes, tbb::numa_node_id *nodes, size_t node_count,
                            size_t interleaving_step);
void deallocate_interleaved (void *ptr, size_t bytes);
```

Internal namespaces should be used as appropriate.

Note that the list of nodes is passed as a *{pointer, count}* pair of parameters instead of
a single pointer or a reference. This both avoids using `std::vector` directly and allows
changing/extending the public API with other contiguous storage types, e.g. `std::span`.

## Implementation details

Under Linux, only allocations with default interleaving can be supported via HWLOC. Other
interleaving steps require direct libnuma usage, that creates yet another run-time
dependency. Using `move_pages` it's possible to implement allocation with constant number
of system calls wrt allocation size.

Under Windows, starting Windows 10 and WS 2016, `VirtualAlloc2(MEM_REPLACE_PLACEHOLDER)`
can be used to provide desired interleaving, but the number of system calls is proportional to
allocation size. For older Windows, either fallback to `VirtualAlloc` or manual touching
from threads pre-pinned to NUMA nodes can be used.

There is no NUMA memory support under macOS, so the implementation can only fall back to
`malloc`.

## Next steps

Due to the remaining open questions and the desire to get better understanding of performance
impact, we propose to initially deliver the functionality for *preview*, with the following
key choices:

- Finalize the ABI as described;
- Only provide the 3 non-typed functions;
- Do nothing special for error handling, except for returning `nullptr`;

## Open Questions

The major open question still unresolved is the approach to error handling.

Do the type-aware function templates provide sufficient extra usability to be added?

Does it make sense to add a function to query the system page size now, or is it better
added on demand?

Semantics of even distribution of data between NUMA nodes is straightforward: to equally
balance work between the nodes. Why might someone want to distribute data unequally? Can
it be a form of fine-tuning “node 0 already loaded with access to static data, let’s
decrease the load a little”?
