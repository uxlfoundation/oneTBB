# Allocate memory interleaved between NUMA nodes: error handling
 
*Note:* This document is a sub-RFC of the [umbrella RFC about improving NUMA
support](README.md). 

## Returning vs termination

For the allocation function, there is a reasonable fallback path: a `malloc()` call, and there are
reasonable causes of run-time failure, for example hitting a quota on the maximum number of
mappings. Therefore, returning an error is a valid error-handling policy.

For deallocation, there is no reasonable fallback, and at least passing an invalid address to
`munmap()` is serious enough to justify terminating the application. This termination behavior
matches `free()`. Instead of just using limited capabilities of `std::terminate_handler`,
termination must be processed in a way customizable via a custom assertion handler. Customization
allows the possibility to report the parameters of the failed function.

## Error returning policy should match existing practices

The common error-handling practice for `operator new` is to throw exceptions or, when explicitly
requested, return `nullptr`. Combining that with the high-level principle that the requirements of
exotic customers should not worsen the quality of life for ordinary customers, we get one function
family that throws exceptions by default and another that returns `nullptr` plus a failure
description when explicitly requested.

## Which exception?

`std::bad_alloc` is a natural choice for an allocation function, but it can’t provide information
about the exact reason for failure, which is useful for analysis. `std::invalid_argument` matches
cases where invalid arguments are passed to allocation functions. `std::system_error` matches cases
where an OS call returns an error. Both can carry a message that describes the failure, and the
latter also has room for error-code information such as `errno`.

It is unclear how error codes would be used beyond writing logs, so enum values have no clear
advantage over strings for indicating what went wrong.

## How to return “what went wrong” string for no-throwing functions

Those strings can be returned in several ways: a) as the only function return value, with the
pointer returned through a separate output parameter; b) as part of a composite return value, for
example `std::pair`; c) through a separate output parameter, while keeping the pointer as the return
value; d) via a separate function that returns the error description.

### Return the error description through an output parameter

For the commonly used `allocate_numa_interleaved` function it's natural to return a pointer as the
return value. It's better for the non-throwing function to be similar. And standard `new` and
`std::allocator<T>::allocate` return a pointer as a single return value. Among options (a), (b), and
(c), option (c) is preferable. However, it requires too many overloads.

`std::nothrow_t` is used to implement an overload of `operator new`. There is no clear advantage or
disadvantage of overloading `allocate_numa_interleaved` vs adding
`allocate_numa_interleaved_nothrow`, because `allocate_numa_interleaved` is not an operator.

Every overload should contain a `bytes` argument. Any combination of the list of NUMA nodes,
`bytes_per_chunk`, and `error_msg` is possible. We may want to put `error_msg` at the end, since
that's an output parameter. Below is a possible set of overloads.
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
The examples below show all 8 argument combinations.
```c++
    size_t obj_size = ...;
    const std::vector<tbb::numa_node_id> nodes = ...;
    size_t bytes_per_chunk = ...;
    std::string error_msg;

    tbb::allocate_numa_interleaved_nothrow(obj_size, nodes, bytes_per_chunk);
    tbb::allocate_numa_interleaved_nothrow(obj_size, nodes, bytes_per_chunk, error_msg);
    tbb::allocate_numa_interleaved_nothrow(obj_size, nodes);
    tbb::allocate_numa_interleaved_nothrow(obj_size, nodes, error_msg);
    tbb::allocate_numa_interleaved_nothrow(obj_size, bytes_per_chunk);
    tbb::allocate_numa_interleaved_nothrow(obj_size, bytes_per_chunk, error_msg);
    tbb::allocate_numa_interleaved_nothrow(obj_size);
    tbb::allocate_numa_interleaved_nothrow(obj_size, error_msg);
```

### Use a separate function that returns the error description

For each `tbb::allocate_numa_interleaved` overload, provide a corresponding
`tbb::allocate_numa_interleaved_nothrow` overload that returns `nullptr` if and only if the
allocation fails. The error description can be retrieved by calling
`const char* tbb::allocate_numa_interleaved_get_last_error() noexcept`.

The error description is stored per thread. Neither successful allocation calls nor calls to the
getter modify the stored description. The returned pointer remains valid, and the string contents
remain unchanged, until the next failed `allocate_numa_interleaved_nothrow` call in the same thread
or until that thread terminates. The user must not modify or free the returned string. Before the
first failed `allocate_numa_interleaved_nothrow` call in the current thread, the getter returns a
pointer to an empty string.

The semantics of `tbb::allocate_numa_interleaved_get_last_error()` are similar to those of Windows'
`GetLastError()`, so they should be familiar to users.

`const char*` was chosen as the return type because in some cases it is enough to return a pointer
to static memory without dynamic allocation. Furthermore, `const char*` matches the expected
common use case, i.e., writing an error to error logs.

## Implementation notes

Based on the analysis above, on error `allocate_numa_interleaved` should throw
`std::invalid_argument` for invalid input arguments (i.e., when `bytes` is zero or
`bytes_per_chunk` is not a multiple of the page size), and `std::system_error` when an underlying
syscall fails. Both exceptions contain a string describing the error.

On error, `allocate_numa_interleaved_nothrow` never throws and returns `nullptr`. The error
description can be retrieved as described in the "Use a separate function that returns the error
description" section.

`deallocate_numa_interleaved` uses `__TBB_ASSERT_RELEASE` to report an error.

TBB has 3 modes of exception support: default, terminate_on_exception enabled, and library build
with `TBB_USE_EXCEPTIONS=0` (the latter is currently unsupported). For the throwing overloads,
failures that would normally throw must trigger termination in the latter two configurations. There
is no need to turn them into non-throwing functions. The non-throwing overloads are not affected by
changes in exception support and return `nullptr` on failure in all 3 configurations.

Following the "best-effort" principle, the absence of NUMA-supporting libraries is not treated as
an error, non-interleaved memory is silently returned instead.

`throw` generates a lot of code, so it should not be used in the functions inlined to user code
without strong justification.
