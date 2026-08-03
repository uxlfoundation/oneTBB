# Allocate memory interleaved between NUMA nodes: error handling
 
*Note:* This document is a sub-RFC of the [umbrella RFC about improving NUMA
support](README.md). 

## Returning vs termination

For the allocation function, there is a reasonable fallback path: a `malloc()` call, and there are
reasonable causes of run-time failure, for example hitting a quota on the maximum number of
mappings. Therefore, returning an error is a valid error-handling policy.

For deallocation, there is no reasonable fallback, and at least passing an invalid address to
`munmap()` is serious enough to justify terminating the application. This termination behavior
matches `free()`. The termination must be processed in a way customizable via a custom assertion
handler. Advantage over `std::terminate_handler` is the possibility to report parameters of a failed
function.

## Error returning policy should match existing practices

The common error-handling practice for `operator new` is to throw exceptions or, when explicitly
requested, return `nullptr`. Combining that with the high-level principle that requirements of
exotic customers should not worsen quality of life for ordinary customers, we get a function family
that throwing exceptions as the default, and a function family that returns `nullptr` plus a failure
description when explicitly requested.

## Which exception?

`std::bad_alloc` is a natural choice for allocation function, but it can’t provide information about
the exact reason for failure, which is useful for analysis. `std::invalid_argument` matches cases
where invalid arguments are passed to allocation functions. `std::system_error` matches cases
where an OS call returns an error. Both can carry a message that describes the failure, and the
latter also has room for error-code information such as errno.

It is unclear how error codes would be used beyond writing logs, so enum values have no clear
advantage over strings for indicating what went wrong.

## How to return “what went wrong” string for no-throwing functions

Those strings can be returned in several ways: a) as only function return value, with pointer
returned through a separate output parameter; b) as part of composite return value, for example
`std::pair`; c) through a separate output parameter, while keeping the pointer as the return value.

For the commonly-used `allocate_numa_interleaved` function it's natural to return a pointer as the
return value. It's better for the non-throwing function to be similar. And standard `new` and
`std::allocator<T>::allocate` return a pointer as a single return value. So, (c) is preferable.

std::nothrow_t is used to implement an overload of `operator new`. There is no clear advantage or
disadvantage of overloading `allocate_numa_interleaved` vs adding
`allocate_numa_interleaved_nothrow`, because allocate_numa_interleaved is not an operator. 

Below is a possible set of overloads. The number of overloads could be reduced by not placing
`error_msg` at the end, but keeping the output parameter last is natural.
```c++
void *allocate_numa_interleaved_nothrow(size_t bytes,
                                        const std::vector<tbb::numa_node_id>& nodes,
                                        size_t bytes_per_chunk = 0);
void *allocate_numa_interleaved_nothrow(size_t bytes,
                                        const std::vector<tbb::numa_node_id>& nodes,
                                        std::string &error_msg);
void *allocate_numa_interleaved_nothrow(size_t bytes,
                                        const std::vector<tbb::numa_node_id>& nodes,
                                        size_t bytes_per_chunk,
                                        std::string &error_msg);
void *allocate_numa_interleaved_nothrow(size_t bytes, size_t bytes_per_chunk = 0);
void *allocate_numa_interleaved_nothrow(size_t bytes, std::string &error_msg);
void *allocate_numa_interleaved_nothrow(size_t bytes, size_t bytes_per_chunk, std::string &error_msg);
```

## Implementation notes

TBB has 3 modes of exception support: default, terminate_on_exception enabled, and library build with
`TBB_USE_EXCEPTIONS=0` (the later is unsupported). For throwing functions, the last two modes call `abort`
when an exception is thrown, there is no need to turn them into non-throwing functions.

`throw` generates a lot of code, so it should not be used in the functions inlined to user code
without strong justification.
