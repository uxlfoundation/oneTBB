# Scalable Memory Pools

## Introduction

The oneTBB library provides comprehensive memory allocation support through two main approaches:

1. **Standard-compliant allocators** that model the requirements from the 
   [allocator.requirements] section of the ISO C++ standard
2. **Memory resource implementations** that implement the `std::pmr::memory_resource` abstract 
   interface described in the [mem.res.class] section of the ISO C++ standard

However, certain use cases require specialized memory management patterns that go beyond 
standard allocators, scenarios where applications need:
- Bulk memory deallocation (freeing all allocations at once)
- Allocation from pre-allocated, fixed-size buffers
- High-performance thread-safe allocation with custom underlying allocators

### The Scalable Memory Pools Feature

To address these needs, oneTBB introduced *scalable memory pools* in 2011 as a preview feature. 
This feature leverages the thread-safe, scalable allocation techniques of the TBB scalable 
allocator to provide specialized memory management capabilities.

The feature consists of three main components:

- **`memory_pool`**: A class template for extensible scalable allocation from an underlying 
  allocator
- **`fixed_pool`**: A class for high-performance allocation from pre-allocated, fixed-size 
  buffers  
- **`memory_pool_allocator`**: A class template that integrates memory pools with STL 
  containers

All memory pools support bulk deallocation—either automatically upon pool destruction or 
explicitly via a `recycle()` function. This makes them particularly useful for applications with 
frame-based or phase-based memory usage patterns, such as graphics rendering, scientific 
computing, or request processing systems.

### Current Status and Motivation for This RFC

These scalable memory pools have remained in preview status for over a decade, despite being 
actively used by the TBB community. The extended preview period occurred because:

1. **Evolving standards**: The ISO C++ standard was simultaneously developing its own memory 
   management facilities, particularly the memory resources introduced in C++17
2. **Feature overlap**: C++17 introduced pool-based memory resources like 
   `std::pmr::synchronized_pool_resource` and `std::pmr::monotonic_buffer_resource` that 
   address some of the same use cases
3. **Integration questions**: Uncertainty about how these features should relate to oneTBB's 
   production memory allocation offerings

### oneTBB's Current Production Memory Allocation Features

For context, oneTBB currently provides these production-ready memory allocation components:

**Standard Allocators:**
- [`tbb_allocator`](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/memory_allocation/tbb_allocator_cls)
- [`scalable_allocator`](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/memory_allocation/scalable_allocator_cls)  
- [`cache_aligned_allocator`](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/memory_allocation/cache_aligned_allocator_cls)

**Memory Resources:**
- [`cache_aligned_resource`](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/memory_allocation/cache_aligned_resource_cls): 
  Wraps another memory resource to provide cache-aligned allocations
- [`scalable_memory_resource`](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/memory_allocation/scalable_memory_resource_func): 
  Provides scalable memory allocation through the `std::pmr` interface

### Purpose of This RFC

This RFC provides a comprehensive analysis of the scalable memory pools feature, including:

- **Design documentation**: Detailed description of the current implementation and API
- **Comparative analysis**: How these features relate to C++17 memory resources and oneTBB's 
  production offerings
- **Open Questions and Exit Criteria**: Evaluation criteria for determining the future of this preview feature

The goal is to resolve the long-standing preview status by making an informed decision about 
the feature's future based on community needs, standards alignment, and technical merit.

## Design of Scalable Memory Pools

The Resource Management Layer, `rml`, implements the algorithms used by the oneTBB 
scalable memory allocator and can be set up to use different underlying fixed buffers or
allocators.The scalable memory pools inherit from `pool_base`, which contains a pointer to an
`rml::MemoryPool`. All public functions call corresponding functions in the `rml::MemoryPool`.
A scalable pool therefore reuse the same thread-safe, scalable functionality provided by
the oneTBB scalable allocator.

The class `pool_base` provides most of the public API:

```cpp
class pool_base {
public:
    void recycle() { rml::pool_reset(my_pool); }
    void *malloc(size_t size) { return rml::pool_malloc(my_pool, size); }
    void free(void* ptr) { rml::pool_free(my_pool, ptr); }
    void *realloc(void* ptr, size_t size) {
        return rml::pool_realloc(my_pool, ptr, size);
    }

protected:
    void destroy() { rml::pool_destroy(my_pool); }
    rml::MemoryPool *my_pool;
};
```

The class `memory_pool` receives an allocator in its constructor, configures
the `rml` layer to use that allocator, and then creates the `my_pool` used
by the `pool_base` functions.

```cpp
template <typename Alloc>
class memory_pool : public pool_base {
    Alloc my_alloc;
public:
    explicit memory_pool(const Alloc &src = Alloc());
    ~memory_pool() { destroy(); }
};

template <typename Alloc>
memory_pool<Alloc>::memory_pool(const Alloc &src) : my_alloc(src) {
    /* ... set up rml args to use my_alloc ... */
    rml::pool_create_v1(intptr_t(this), &args, &my_pool);
}
```

The class `fixed_pool` receives a buffer and size in its constructor, configures 
the `rml` layer to use that fixed-size buffer, and then creates the `my_pool` used
by the `pool_base` functions.

```cpp
class fixed_pool : public pool_base {
    void *my_buffer;
    size_t my_size;
public:
    inline fixed_pool(void *buf, size_t size);
    ~fixed_pool() { destroy(); }
};

inline fixed_pool::fixed_pool(void *buf, size_t size) 
    : my_buffer(buf), my_size(size) {
    /* ... set up rml args to use the fixed-sized buffer */
    rml::pool_create_v1(intptr_t(this), &args, &my_pool);
}
```
## Comparative Analysis

There is significant functional overlap between the scalable memory pool preview feature,
C++ standard features, and the oneTBB production features.

TBD

## Open Questions and Exit Criteria

### Open Questions

1. **C++17 Memory Resources Relationship**: Given the overlap with `std::pmr` memory resources:
   - Should scalable memory pools be reimplemented as `std::pmr::memory_resource` derivatives?
   - Is there sufficient differentiation to justify both approaches?
   - Can we provide seamless interoperability between the two models?
2. **Backward Compatibility**: For existing users of the preview feature:
    - What level of API compatibility should be maintained?
    - How should migration paths be provided if APIs change?
    - If the feature is deprecated, what timeline would be appropriate for removal?

### Exit Criteria
1. **Community Buy-in**: Positive feedback on decision to productize or deprecate
2. **Technical Quality**: If promoted, the feature meets oneTBB production standards
3. **Standards Alignment**: The outcome aligns with C++ standards evolution and best practices
