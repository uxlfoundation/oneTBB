# Advanced Core Type Selection

## Introduction

Modern CPUs, especially those with hybrid architectures, present new challenges for thread affinity,
concurrency, and resource management. These systems can feature multiple core types - such as performance and
efficiency cores.

Currently, oneTBB provides mechanisms to constrain execution to a single core type. To better support
heterogeneous and hybrid processor architectures, we propose to introduce support for multiple core-type
constraints, improving the library's ability to identify and utilize available hardware resources accurately.
This enhancement is motivated by:

- The need for improved user experience when tuning for increasingly diverse processor designs.
- The opportunity for performance improvements through more precise control over thread placement.
- The desire to future-proof the library as hardware evolves.

This proposal builds on ongoing efforts to improve hardware topology awareness and affinity support in
oneTBB, as seen in RFCs addressing NUMA and task scheduling.

## Proposal

We propose the following key changes:

- **Expansion of constraints API:**
  The `constraints` structure would support specifying multiple core types, with new methods such as:
    - `set_core_types(const std::vector<core_type_id>& ids)`
    - `get_core_types() const`
    - `single_core_type() const`
- **Improved concurrency calculation:**
  The logic for default concurrency would sum across all specified core types, ensuring better utilization on
  CPUs featuring diverse core types.

### Usage Example

```cpp
tbb::task_arena::constraints c;
c.set_core_types({core_type_id::performance, core_type_id::efficiency});
arena.initialize(c, 0);
```
This example would allow an arena to support both performance and efficiency cores.

### Impact and Rationale

#### New Use Cases Supported

- More granular thread placement on hybrid CPUs
- Enables new high-level features and optimizations for scheduling and affinity

#### Performance Implications

- Potential for improved throughput and resource utilization, especially in workloads sensitive to core-type
  heterogeneity.

#### Compatibility

- The API would be backward compatible. Existing code specifying a single core type would remain valid.

- No ABI breakage; semantic versioning maintained.

#### Build System and Dependencies

- No changes to CMake or build configuration are anticipated.
- No new external dependencies introduced.

### Alternatives Considered

- **Single Core-Type Constraint:**
  Simpler API, but increasingly inadequate for modern hardware.
- **Automatic core type grouping:**
  Might hide necessary details from users and reduce control.

#### Pros and Cons

| Approach                       | Pros                    | Cons                               |
|--------------------------------|---------------------------------------|----------------------|
| Multiple core-type constraints | Flexible, future-proof  | Slightly more complex API          |
| Single core-type constraint    | Simpler, minimal change | Inadequate for hybrid systems      |
| Automatic grouping             | Ease of use             | Reduced transparency, less control |

### Testing

- Should be validated on a wide range of hardware, including hybrid CPUs and those with mixed cache
  hierarchies.
- Regression tests should confirm backward compatibility.

## Open Questions

1. How should user-specified constraints interact with automatic topology detection?
2. Are there edge cases in hardware topology that require further handling?
3. Should the API expose additional performance metrics for multi-core-type configurations?
4. Would users benefit from explicit APIs for cores with missing cache levels?
5. Is further abstraction needed for more complex hardware partitioning scenarios?
