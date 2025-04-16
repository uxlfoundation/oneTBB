# oneTBB Versioning Policies

oneTBB follows a multi-level versioning approach. This document outlines the different types of versioning 
used in the oneTBB project and how they relate to compatibility and usage.

## Version Types

oneTBB uses the following versioning types:

| Version Type    | Purpose                          | Relevant Macros / Functions                               |
|-----------------|----------------------------------|------------------------------------------------------------|
| Specification   | Tracks [specification](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/nested-index) conformance          | `ONETBB_SPEC_VERSION`                                      |
| Library         | Identifies the release version   | `TBB_VERSION_MAJOR`, `TBB_VERSION_MINOR`, `TBB_VERSION_STRING`, `TBB_runtime_version()` |
| Interface       | Ensures binary compatibility     | `TBB_INTERFACE_VERSION_MAJOR`, `TBB_INTERFACE_VERSION_MINOR`, `TBB_INTERFACE_VERSION`, `TBB_runtime_interface_version()` |
| API (Namespace) | Maintains backwards compatibility | Namespaces: `dN` (headers), `rN` (runtime symbols)         |


The oneTBB project uses [semantic versioning](https://semver.org/), where:
 
* A **MAJOR** version change introduces breaking updates. 
* A **MINOR** version adds backwards-compatible updates.

As part of the [oneAPI Specification](https://oneapi.io/), oneTBB also follows 
the [oneAPI Compatibility Guidelines](https://www.intel.com/content/www/us/en/docs/oneapi/programming-guide/2024-1/oneapi-library-compatibility.html).


## Specification Version

oneTBB is governed by the [UXL foundation](https://uxlfoundation.org/) and has
[an open specification](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/nested-index).

* The `ONETBB_SPEC_VERSION` macro, defined in `oneapi/tbb/version.h`, indicates the latest oneTBB specification version fully
supported by the implementation. See [Version Information](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/configuration/version_information). 
* Specification versions are independent of implementation or interface versions.


## Library Version

The oneTBB library version identifies an implementation release. We tag the [GitHub* Releases](https://github.com/uxlfoundation/oneTBB/releases) using the library version. 

The following macros provide the library version that is being compiled against:
* `TBB_VERSION_MAJOR`
* `TBB_VERSION_MINOR`
* `TBB_VERSION_STRING`

See [Version Information](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/configuration/version_information) to learn more. 

To return the library version of the binary library at runtime, can use the `const char* TBB_runtime_version();` function. 

## Interface Version

The interface version defines the public contract for the oneTBB library, including function signatures and types in headers and symbols in the binary.

It enables the runtime compatibility between different **MINOR** versions with the same **MAJOR** version. 
Applications built with oneTBB version *X.Y* can safely run with binary version *X.Z* (where *Z* ≥ *Y*).

The following macros provide the interface version that is being compiled against:

* `TBB_INTERFACE_VERSION_MAJOR`
* `TBB_INTERFACE_VERSION_MINOR`
* `TBB_INTERFACE_VERSION` 

See [Version Information](https://oneapi-spec.uxlfoundation.org/specifications/oneapi/latest/elements/onetbb/source/configuration/version_information) to learn more. 

To return the interface version of the binary library at runtime, use the `int TBB_runtime_interface_version();` function. 

### Shared Library Conventions

#### Linux* OS

oneTBB uses a symlink system for binary libraries and explicitly set a soname that uses only the major version number.

 
* The shared library: ``libtbb.so.X.Y``
* Soname: ``libtbb.so.X``
* Symlinks:
  
  ```
  libtbb.so.X -> libtbb.so.X.Y
  libtbb.so -> libtbb.so.X
  ```

Therefore, whether you build against `libtbb.so`, `libtbb.so.X`, or `libtbb.so.X.Y`, your application has a dependency on `libtbb.so.X`.

**Development Best Practice:** 

* If you redistribute your version of the oneTBB binary, use the latest ``X.Z``. 
* If you rely on system libraries, build against the oldest expected version (e.g., ``X.1``) for maximum compatibility.

#### Windows* OS

On Windows* OS, the TBB binary library has only the major version number, ``tbb12.dll``.

## API Version to Maintain Backwards Compatibility

This section is targeted at oneTBB contributors. 

To preserve backwards compatibility across **MINOR** releases, oneTBB uses versioned namespaces:

* Header-level types use the `dN` namespace (e.g., `d1`, `d2`).
* Runtime library symbols use the `rN` namespace (e.g., `r1`, `r2`).

### Example
Below is an example of [a function exported](https://github.com/uxlfoundation/oneTBB/blob/45c2298727d09556a523d6aeaec84ef23872eccf/src/tbb/parallel_pipeline.cpp#L446)
by the oneTBB runtime library:

```
    namespace tbb {
    namespace detail {
    namespace r1 {
        void __TBB_EXPORTED_FUNC parallel_pipeline(d1::task_group_context& cxt,
                                                   std::size_t max_token,
                                                   const d1::filter_node& fn);
    }
    }
    }
```

Here, ``parallel_pipeline`` is exported under the `r1` runtime namespace, taking `d1` versions of `task_group_context` and 
`filter_node` classes as arguments. 


### Rules for Namespace Version

- If the layout of public class X is changed incompatibly, increment the ``d`` namespace number.
- Retain previous class versions in their original namespaces if existing entry points still use them.
