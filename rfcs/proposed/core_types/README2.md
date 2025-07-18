# Advanced Core Type Selection

## Introduction

Currently, oneTBB provides basic support for core type constraints through the `task_arena::constraints`
struct, but allows specifying only one core type at a time. While that works with hybrid CPU archictectures
with two core types, future architectures could feature three or more. This proposal extends the existing
core type constraint system to provide a more intuitive and flexible API for selecting and managing core
types in hybrid CPU systems.

The motivation for this enhancement includes:
- **Improved performance on hybrid CPUs**: Allow applications to explicitly target high-performance
  cores for compute-intensive tasks and efficiency cores for background work
- **Power optimization**: Enable power-conscious applications to prefer efficiency cores when appropriate
- **Enhanced flexibility**: Support complex core type selection patterns

Code snippets showcasing the benefits:

```cpp
// Current approach (cannot constrain by more than one type)
tbb::task_arena::constraints constraints;
constraints.set_core_type(0); // Cannot specify, e.g., both 0 and 1

// Proposed approach (intuitive vector-based API)
tbb::task_arena::constraints constraints;
constraints.set_core_types({0, 1});
```

## Proposal

This proposal introduces several enhancements to the existing `task_arena::constraints` core type
functionality while maintaining full backward compatibility with the current API.

### 1. Enhanced Core Type Selection API

The existing `constraints` struct already supports core type specification, but only one core type is
allowed at a time. We propose extending this with:

```cpp
namespace oneapi {
namespace tbb {
class task_arena {
struct constraints {
// Existing methods (unchanged for backward compatibility)
constraints& set_core_type(core_type_id id);

// New proposed methods
constraints& set_core_types(const std::vector<core_type_id>& ids);
std::vector<core_type_id> get_core_types() const;
bool single_core_type() const;

// Upper 4 bits reserved for format marker (single vs multiple core types)
static constexpr size_t core_type_id_bits = sizeof(core_type_id) * CHAR_BIT - 4;
};
};
}}
```

### 2. Implementation Details

The core type selection mechanism uses a compact bit-field representation where:
- Upper 4 bits are reserved for format markers (distinguishing single vs multiple core types)
- Remaining bits represent individual core type IDs
- Bit position corresponds to core type ID
- Special value of -1 represents "any core type"

The 4-bit format marker is future-proof by allowing up to 2<sup>4</sup>-1=15 format versions (`1111`
is already taken by the special value of -1).

### 3. Usage Examples

#### Hybrid Workload Distribution

```cpp
// On a 3 core type system
auto cores = tbb::info::core_types();

// Create arena for high-performance work
tbb::task_arena::constraints comp_constraints;
comp_constraints.set_core_types({cores[1], cores[2]});
tbb::task_arena comp_arena(comp_constraints);

// Create arena for background work
tbb::task_arena::constraints bg_constraints;
bg_constraints.set_core_type(core[0]);
tbb::task_arena bg_arena(bg_constraints);

// Submit compute-intensive work
comp_arena.enqueue([]() {
    tbb::parallel_for(0, large_dataset_size, [](int i) {
        compute_intensive_operation(dataset[i]);
    });
});

// Submit background tasks
bg_arena.enqueue([]() {
    perform_background_maintenance();
});
```

### 4. Backward Compatibility

The proposed API maintains full backward compatibility:
- Existing `constraints` constructors and methods remain unchanged
- New methods are additive extensions
- Internal bit-field representation remains the same
- Applications using the current API continue to work without modification

### 5. Performance Implications

The enhancements are designed with performance in mind:
- Bit manipulation operations remain highly efficient
- Vector conversions only occur when explicitly requested by user code
- No additional runtime overhead for applications not using the new features

### 6. Testing Aspects

Comprehensive testing should cover:
- Core type detection on various hybrid CPU architectures
- Correct bit-field manipulation for multiple core types
- Performance benchmarks comparing different core type selections
- Thread affinity verification on actual hybrid hardware

## Open Questions

- Should we add helper methods to `constraints` for common patterns in addition to the all-at-once
  `set_core_types()`/`get_core_types()`, e.g.,
```cpp
constraints& add_core_type(core_type_id id);
constraints& remove_core_type(core_type_id id);
bool has_core_type(core_type_id id) const;
void clear_core_types();
```
