# High-level wrappers for interleaved memory allocation

The basic interface for interleaved memory allocation was designed to support the following use
cases:

1. Obtaining raw memory for memory pools. These pools can provide performance benefits such as
improved cache locality, reduced fragmentation, and efficient pooling.

2. Allocating large, long-lived arrays. These arrays are expected to be allocated at startup, or at
least when allocation performance is not critical.

## Pools out of interleaved memory

There is no common interface for memory resource between different implementation of pools. An
example of using interleaved memory with TBB memory pools has been added to the documentation. Raw
memory allocations can also be used with jemalloc arenas (its term for pools) or as a memory
resource for C++17 polymorphic memory resources (PMR). Pool APIs pass the allocation size to
deallocation callbacks, so their deallocation interfaces are formally compatible with the NUMA
allocation API.

A significant limitation for C++17 PMR is the potential mismatch between the requested and
acquired memory sizes. `tbb::allocate_numa_interleaved` allocates memory in page-sized units, but
there is no way to communicate the actual allocation size to
`std::pmr::unsynchronized_pool_resource`. As a result, the pool resource may request regions that
are significantly smaller than the allocated memory. Providing own *memory resource* implemenation
that supports general regions splitting/merging is far from trivial.

## Support of large typed arrays

Using typeless allocation for this use case has two major drawbacks:

1. These large arrays have a specific element type. Since typed allocation is standard in C++,
requiring users to cast the returned pointer is unintuitive.

2. The deallocation function requires the allocation size. Passing that size from the allocation
site to the deallocation site breaks encapsulation and may cause correctness issues, for example,
if the number of elements is confused with the size in bytes.

Another important consideration is how familiar the interface will be to different groups of C++
users.

There are three largely independent design choices:

1. Whether to use standalone allocation function templates or a class template with member
functions.

2. How to get the size of the allocated object during deallocation. That could be left entirely
to the user, or represented by `std::span<T>` or `std::unique_ptr<T[]>` with a custom deleter.

3. Whether to return uninitialized memory or to construct and destroy objects in it. The latter
requires passing constructor arguments to the allocation functions. Note that interleaved memory is
always filled with zeroes, which suits static arrays of fundamental types.

A class template with member functions is a natural place to store state shared across allocations.
There is currently no such state, but future extensions to the interface may require it. A class
template that returns raw pointers is similar to a C++ allocator, which may be familiar to some
users.

`std::span<T>` is a natural way to keep a pointer and its element count together and also iterate
over the memory in natural way. Since `std::span<T>` is unavailable before C++20, this approach
would require a compatibility layer for older C++ standards. In that respect, it is similar to
using `std::unique_ptr<T[]>` but without iteration possibility.

`std::span<T>` naturally combines a pointer and its element count, and supports iteration. However,
because it is available only in C++20 and later, using it would require a compatibility layer for
older C++ standards. `std::unique_ptr<T[]>` does not provide iteration support.

Not every combination may be equally intuitive to users. For example, for
`std::unique_ptr<T[]>` it's natural to expect destructor calling.

Standalone allocation function templates that return raw pointers are modeled after
`sycl::malloc<T>()`. With this approach, the API should make it difficult to confuse an element
count with a size in bytes.
