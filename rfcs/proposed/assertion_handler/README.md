# Custom Assertion Handler

## Introduction

This proposal introduces a new mechanism for customizing assertion failure handling in oneTBB through a
`set_assertion_handler` API. Currently, oneTBB assertions use a fixed behavior that prints error messages to
stderr and terminates the program with `std::abort()`, which limits flexibility for different use cases.

The motivation for this feature includes:

- **Improved debugging experience**: Developers can integrate custom logging, breakpoint handling, or error
  reporting systems instead of the default stderr output and program termination.
- **Testing support**: Unit tests can capture assertion failures as exceptions instead of terminating the
  test process, enabling better test coverage and validation.
- **Production flexibility**: Applications can implement graceful degradation or recovery strategies rather
  than hard crashes.
- **Integration with existing error handling**: Applications can route oneTBB assertion failures through
  their existing error reporting infrastructure.

## Proposal

### API Interface

We propose adding the following interface to the oneTBB library for custom assertion handling:

```cpp
namespace tbb {
    // Function pointer type for assertion handlers
    using assertion_handler_type = void(*)(const char* location, int line,
                                           const char* expression, const char* comment);

    // Set custom assertion handler, returns previous handler
    assertion_handler_type set_assertion_handler(assertion_handler_type new_handler);
}
```

### Proposed Implementation Details

#### 1. Handler Storage and Management
- **Global Handler Storage**: A static global variable will store the current assertion handler
- **Previous Handler Return**: `set_assertion_handler` will return the previously set handler for
  restoration
- **Default Handler**: `nullptr` assertion handler means the default implementation is used

#### 2. Assertion Execution Flow
Use the assertion handler if defined or fall back to the default implementation.

#### 3. New Library Entry Point
For in-library assertion handler usage, a new entry point will be added for `set_assertion_handler`.

### Use Cases Supported

#### 1. Custom Logging Integration

```cpp
tbb::set_assertion_handler([](const char *location, int line, const char *expression, const char *comment) {
    my_logger.error("TBB assertion failed in {} at line {}: {}", location, line, expression);
    if (comment) {
        my_logger.error("Details: {}", comment);
    }
});
```

#### 2. Exception-Based Error Handling

```cpp
tbb::set_assertion_handler([](const char *location, int line, const char *expression, const char *comment) {
    throw std::runtime_error(std::string("Assertion failed: ") + expression + " in " + location);
});
```

#### 3. Unit Testing Support

```cpp
tbb::set_assertion_handler([](const char *location, int line, const char *expression, const char *comment) {
    // Convert assertion failure into a test failure
    FAIL() << "Assertion failed: " << expression << " in " << location << " at line " << line;
});
```

```cpp
std::string captured_assertion;
auto test_handler = [&](const char *location, int line, const char *expression, const char *comment) {
    captured_assertion = std::string(expression) + " in " + location;
    throw std::runtime_error("test assertion");
};
auto old_handler = tbb::set_assertion_handler(test_handler);
// ... test code that should trigger assertion ...
tbb::set_assertion_handler(old_handler);
```

### Performance Implications

- **Minimal overhead**: Handler lookup will be a simple pointer check with no heap allocation
- **No runtime dependencies**: Uses C-style function pointers to avoid C++ runtime overhead

### API and ABI Backward Compatibility

The proposed implementation will maintain backward compatibility:
- **Stable function signatures**: Handler signature has remained unchanged across versions
- **C-style interface**: Uses function pointers instead of `std::function` for ABI stability
- **No breaking changes**: Existing code will continue to work without modification

### Dependencies and Support Matrix

#### Minimal Dependencies
- **No C++ runtime dependency**: Critical for tbbmalloc integration

#### Supported Configurations
- **All oneTBB-supported platforms**: Windows, Linux, macOS, etc.
- **Debug and release builds**: Full functionality in debug; can be used in release with `TBB_USE_ASSERT`
- **tbbmalloc integration**: Works in memory allocator context without C++ runtime

### Design Alternatives Considered

#### 1. std::function vs Function Pointers
**Proposed**: Function pointers
- **Pros**: No C++ runtime dependency, ABI stable, minimal overhead
- **Cons**: Less flexible than std::function

**Alternative**: std::function
- **Pros**: More flexible, supports lambda captures, type-safe
- **Cons**: C++ runtime dependency, ABI instability, higher overhead

#### 2. Thread-Local vs Global Handler
**Proposed**: Global handler
- **Pros**: Simple implementation, consistent behavior across threads
- **Cons**: Cannot customize per-thread behavior

**Alternative**: Thread-local handlers
- **Pros**: Per-thread customization possible
- **Cons**: Complex implementation, unclear semantics for assertion origins

#### 3. Handler Chaining vs Single Handler
**Proposed**: Single handler replacement
- **Pros**: Simple, predictable behavior
- **Cons**: Cannot compose multiple handlers

**Alternative**: Handler chaining
- **Pros**: Composable behavior, multiple observers
- **Cons**: Complex implementation, unclear failure handling

### Testing Aspects

The assertion handler mechanism supports comprehensive testing:

```cpp
// Test helper to verify assertion behavior
class AssertionCapture {
    std::string captured_message;
    assertion_handler_type old_handler;

public:
    AssertionCapture() {
        old_handler = tbb::set_assertion_handler(
            [this](const char *location, int line, const char *expression, const char *comment) {
                captured_message = std::string(expression) + " in " + location;
                throw std::runtime_error("captured assertion");
            });
    }
    ~AssertionCapture() { tbb::set_assertion_handler(old_handler); }

    const std::string &get_message() const { return captured_message; }
};
```

## Open Questions

1. **Handler Composition**: Should the API support chaining multiple handlers for different use cases?

2. **Handler Scoping**: Should there be mechanisms for scoped handler installation (RAII-style)?
