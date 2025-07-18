# Advanced Core Type Selection

## Introduction

Currently, oneTBB provides mechanisms to constrain execution to a single core type. To better support
heterogeneous and hybrid processor architectures, we propose to introduce support for multiple core-type
constraints, improving the library's ability to identify and utilize available hardware resources accurately.
This enhancement is motivated by:

- The need for improved user experience when tuning for increasingly diverse processor designs.
- The opportunity for performance improvements through more precise control over thread placement.
- The desire to future-proof the library as hardware evolves.

## Proposal

We propose the following key changes:

- **Expansion of constraints API:**
  The `constraints` structure would support specifying multiple core types, with new methods such as:
    - `set_core_types(const std::vector<core_type_id>& ids)`
    - `get_core_types() const`
    - `single_core_type() const`

### Usage Example

```cpp
auto core_types = tbb::info::core_types();
tbb::task_arena arena{tbb::task_arena::constraints{}.set_core_types({core_types.end() - 2, core_types.end()})};
```
This example would allow an arena to use the two most performant core types and exclude the others.

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

- **Storing core types in a new container data member instead of an integer:** Simpler implementation, but breaks the ABI.

#### Pros and Cons

| Approach                         | Pros                    | Cons                                   |
|----------------------------------|-------------------------|----------------------------------------|
| Reusing the existing `core_type` | ABI compatibility       | More complex implementation            |
| New container data member        | Simpler, fewer changes  | Breaks the ABI, major version increase |

### Testing

- Should be validated on a wide range of hardware, including CPUs with more than two core types.
- Should test all possible core type combinations.
- Regression tests should confirm backward compatibility.

## Open Questions

1. Should the API expose additional performance metrics for multi-core-type configurations?
2. Should `tbb::info::core_types()` also be modified, e.g, simplified to return a core type count?
