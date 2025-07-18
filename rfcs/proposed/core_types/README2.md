# Advanced Core Type Selection

## Introduction

Modern hybrid CPU architectures feature multiple types of cores with different performance and power
characteristics. Intel's 12th generation processors and later include Performance cores (P-cores) and
Efficiency cores (E-cores), while ARM big.LITTLE architectures feature similar heterogeneous designs.
To effectively utilize these systems, applications need fine-grained control over which core types
execute specific workloads.

Currently, oneTBB provides basic support for core type constraints through the `task_arena::constraints`
class, but the API for specifying multiple core types is limited and requires understanding of low-level
bit manipulation. This proposal extends the existing core type constraint system to provide a more
intuitive and flexible API for selecting and managing core types in hybrid CPU systems.

The motivation for this enhancement includes:
- **Improved performance on hybrid CPUs**: Allow applications to explicitly target high-performance
  cores for compute-intensive tasks and efficiency cores for background work
- **Power optimization**: Enable power-conscious applications to prefer efficiency cores when appropriate
- **Simplified API**: Provide a more intuitive interface for specifying core type preferences without
  requiring bit-level manipulation
- **Enhanced flexibility**: Support complex core type selection patterns including multiple core types
  and dynamic core type discovery

Code snippets showcasing the benefits:

```cpp
// Current approach (complex bit manipulation required)
tbb::task_arena::constraints constraints;
constraints.set_core_type(0x3); // Set bits for core types 0 and 1

// Proposed approach (intuitive vector-based API)
tbb::task_arena::constraints constraints;
constraints.set_core_types({0, 1});

// Or even simpler for common cases
auto performance_cores = tbb::info::get_performance_core_types();
constraints.set_core_types(performance_cores);
```

## Proposal

This proposal introduces several enhancements to the existing `task_arena::constraints` core type
functionality while maintaining full backward compatibility with the current API.

### 1. Enhanced Core Type Selection API

The existing `constraints` class already supports core type specification, but the current implementation
requires understanding of internal bit manipulation. We propose extending this with more user-friendly
methods:

```cpp
class constraints {
public:
// Existing methods (unchanged for backward compatibility)
constraints& set_core_type(core_type_id id);

// New proposed methods
constraints& set_core_types(const std::vector<core_type_id>& ids);
std::vector<core_type_id> get_core_types() const;
bool single_core_type() const;

// Helper methods for common patterns
constraints& add_core_type(core_type_id id);
constraints& remove_core_type(core_type_id id);
bool has_core_type(core_type_id id) const;
void clear_core_types();

private:
static constexpr size_t core_type_id_bits = sizeof(core_type_id) * CHAR_BIT - 4;
// ... existing implementation details
};
```

### 2. Implementation Details

The core type selection mechanism uses a compact bit-field representation where:
- Upper 4 bits are reserved for format markers (distinguishing single vs multiple core types)
- Remaining bits represent individual core type IDs
- Bit position corresponds to core type ID
- Special value of -1 represents "any core type"

### 3. Usage Examples

#### Basic Core Type Selection

```cpp
auto available_cores = tbb::info::core_types();

// Create constraints for specific core types
tbb::task_arena::constraints constraints;
constraints.set_core_types({available_cores[0], available_cores[1]});

// Create arena with core type constraints
tbb::task_arena arena(constraints);

arena.execute([]() {
    // This work will run only on the specified core types
    tbb::parallel_for(0, 1000, [](int i) {
        // Compute-intensive work on selected cores
    });
});
```

#### Hybrid Workload Distribution

```cpp
auto perf_cores = tbb::info::get_performance_core_types();
auto eff_cores = tbb::info::get_efficiency_core_types();

// Create arena for high-performance work
tbb::task_arena::constraints perf_constraints;
perf_constraints.set_core_types(perf_cores);
tbb::task_arena perf_arena(perf_constraints);

// Create arena for background work
tbb::task_arena::constraints eff_constraints;
eff_constraints.set_core_types(eff_cores);
tbb::task_arena eff_arena(eff_constraints);

// Submit compute-intensive work to performance cores
perf_arena.enqueue([]() {
    tbb::parallel_for(0, large_dataset_size, [](int i) {
        compute_intensive_operation(dataset[i]);
    });
});

// Submit background tasks to efficiency cores
eff_arena.enqueue([]() {
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

## Considered Alternatives

### Alternative 1: Separate Core Type Constraint Class

Instead of extending the existing `constraints` class, create a dedicated `core_type_constraints` class.
This would provide cleaner separation but would complicate the API and require additional integration
work with `task_arena`.

**Pros:**
- Cleaner separation of concerns
- More focused API surface

**Cons:**
- Increased API complexity
- Additional integration overhead
- Potential for inconsistencies between constraint types

### Alternative 2: Enum-Based Core Type Selection

Use predefined enums like `core_type::performance` and `core_type::efficiency`. While more readable,
this approach is less flexible for systems with more than two core types or custom core type schemes.

**Pros:**
- More readable and self-documenting
- Type-safe selection

**Cons:**
- Less flexible for diverse hardware configurations
- Requires predefined categories that may not fit all systems

### Alternative 3: Callback-Based Core Type Selection

Allow users to provide callbacks for core type selection decisions. This would be more flexible but
significantly more complex to implement and use.

**Pros:**
- Maximum flexibility for custom selection logic
- Can adapt to changing system conditions

**Cons:**
- Significantly more complex to implement and use
- Potential performance overhead
- Harder to reason about and debug

## Open Questions

- Should we provide automatic core type detection and selection based on workload characteristics?
- Should we add performance monitoring integration to automatically adjust core type preferences based
  on runtime behavior?
- What should be the default behavior when no core type constraints are specified on hybrid systems?
- Should we provide integration with power management APIs to respect system power policies?
- How should thread migration between core types be handled when constraints change dynamically?
- Is there value in providing priority ordering for core type preferences when multiple types are
  specified?
