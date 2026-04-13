# C++20 Modules Support for oneTBB

## Introduction

C++20 introduced modules as a modern alternative to the traditional header-based inclusion
model. Modules offer several advantages over headers:

- Modules are compiled once and their binary interface is reused, avoiding repeated parsing
  and preprocessing of header files.
- Modules provide better encapsulation — macros, internal declarations, and implementation
  details do not leak into the consumer's translation unit.

As the C++ ecosystem gradually adopts modules, oneTBB should provide a module interface to
allow users to use the library using `import tbb;` instead of `#include <oneapi/tbb.h>`.

## Proposal

### ABI non-breaking style with using-declaration

The proposed approach is to provide a wrapper module that includes the existing headers in
the global module fragment and re-exports public symbols.

The module interface unit (e.g., `tbb.cppm`) would follow this structure:

```cpp
module;

// Global module fragment — include all public headers
#include <oneapi/tbb.h>

export module tbb;

// Re-export public API via using-declarations
export namespace tbb {
    // Parallel algorithms
    using tbb::parallel_for;
    using tbb::parallel_reduce;
    using tbb::parallel_scan;
    using tbb::parallel_sort;
    using tbb::parallel_invoke;
    using tbb::parallel_pipeline;
    using tbb::parallel_for_each;

   // other TBB API
}
```

The proposed approach has several advantages over other alternatives:

- It does not require the modification of existing headers, as the module includes them as
  part of a global module fragment and then selectively exports the necessary `using`
  declarations. Thus, development can continue in headers.
- Since the API is declared as part of a global module fragment, the name of the module will
  not participate in name mangling, making translation units compiled with modules,
  compatible with the units compiled with regular headers.

The disadvantage of this approach is an obligation to update the module interface unit each time
new API is added.

### CMake integration

The project policy requires C++11 as the minimum standard, hence the module source cannot
be compiled as part of the oneTBB build because it would force C++20 standard on the library.
With the ABI non-breaking approach, the library built with C++11 can be used together with
the module interface unit compiled with C++20 or higher. Furthermore, a prebuilt module constrains
the consumer to the exact C++ standard version used during the module build. Thus, the `.cppm` file
is installed as a source file and compiled on the consumer side.

```cmake
find_package(TBB REQUIRED)

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE TBB::tbb)
# For example, tbb.cppm is located under include/modules
get_target_property(_tbb_include_dir TBB::tbb INTERFACE_INCLUDE_DIRECTORIES)
target_sources(myapp PRIVATE
   FILE_SET cxx_modules TYPE CXX_MODULES
   BASE_DIRS ${_tbb_include_dir}
   FILES ${_tbb_include_dir}/modules/tbb.cppm
)
```

In the future, `TBBConfig.cmake` could provide a helper function (e.g.,
`tbb_enable_cxx20_modules(myapp)`) to reduce this boilerplate.

The consumer compiles the `.cppm` as part of their own target with their own flags. Name
mangling is unchanged, so the consumer links against the same `libtbb.so` / `tbb.lib`
regardless of whether they use modules or headers.

## Alternatives Considered

### 1. ABI non-breaking style with language linkage block

Instead of re-exporting symbols via `using`-declarations, library headers are included
inside an `extern "C++"` language linkage block within the module purview, and all
third-party/system headers are included in the global module fragment.

```cpp
module;

// Global module fragment — system and third-party headers only
#include <cstddef>
#include <atomic>
#include <functional>
// ... all standard/third-party headers used by TBB

export module tbb;

#define __TBB_IN_MODULE_USE
extern "C++" {
    #include "oneapi/tbb.h"
}
```

Declarations within an `extern "C++"` block inside the module purview are actually attached to
the global module fragment by the standard. An important detail here is that TBB headers will need
to define some macro (e.g. `__TBB_CXX20_EXPORT`) if `__TBB_IN_MODULE_USE` is defined. The definition
of `__TBB_IN_MODULE_USE` would also mean that all inclusions of third-party headers should be
excluded from TBB headers.

**Pros:**
- No need to maintain explicit `using`-declarations for every public symbol.
- To export an API you simply add `__TBB_CXX20_EXPORT` to the declaration.
- ABI-compatible with header-based consumers.

**Cons:**
- Requires careful separation of system/third-party headers from TBB headers. If TBB header
  includes a system header that was not already in the GMF, it can cause compilation errors.
- Requires the modification of all TBB headers to add conditional `export` keyword to public
  declarations and stripping the inclusion of third-party headers from them.

### 2. ABI breaking style

Same header-inclusion approach, but library headers are included directly in the module
purview without an `extern "C++"` block. This causes the module name to participate in
symbol mangling.

```cpp
module;

// Global module fragment — system and third-party headers only
#include <cstddef>
#include <atomic>
#include <functional>
// ... all standard/third-party headers used by TBB

export module tbb;

#define __TBB_IN_MODULE_USE
#include <oneapi/tbb.h>
```

**Pros:**
- No need to maintain explicit `using`-declarations for every public symbol.
- To export an API you simply add `__TBB_CXX20_EXPORT` to the declaration.
- The library could be compiled to include symbols with and without module name mangled. That would
  require the consumer of the library to consistently use either TBB headers, or TBB module.

**Cons:**
- The previously mentioned advantage can be seen as disadvantage. The user must choose either modules
  or headers because they cannot coexist in the same program. It also might cause problems when the
  consumer's application uses TBB as module and other third-party that uses TBB, but as headers, or
  vice versa.
- Same cons as for ABI non-breaking style described above.

## Open Questions

1. Should the module be named `tbb`, `oneapi.tbb`, or `onetbb`?

2. Where should the module interface unit live? Options include `include/modules/tbb.cppm`,
   `src/tbb/tbb.cppm`.
  
3. What is the install destination for the module source file? Should it live under
   `share/tbb/modules/`, `include/modules/`, or next to `TBBConfig.cmake`?

4. Should the module be split into partitions (e.g., `tbb:algorithms`, `tbb:containers`,
   `tbb:flow_graph`) to organize the `using`-declarations?
   Partitions are internal to the module and not importable by consumers, but could
   improve maintainability of the module interface. Alternatively, should multiple
   fine-grained submodules (e.g., `tbb.flow_graph`, `tbb.containers`) be provided so
   that consumers can import only what they need?

5. How should module-based consumption be tested? Options include a dedicated test
   that uses `import tbb;` instead of `#include`, or running the existing test suite
   with module imports. Should whitebox tests be covered in the latter scenario?

6. How to provide preview functionality with modules? Should it be a separate `tbb.preview` module
   or should the `tbb` module be compiled with defined `TBB_PREVIEW_*` macros as needed?

7. How to support public macros such as `TBB_VERSION` or feature-test macros? Some of the options:
   - Replace them by inline variables where possible.
   - Resort to inclusion of `tbb/version.h` header.

## Exit Criteria

The following conditions should be met before this feature graduates from
experimental to fully supported:

1. The open questions above are resolved.
2. CMake and compiler support should improve to the point where modules support is not at
   experimental stage.
