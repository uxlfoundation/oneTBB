<!--
******************************************************************************
* 
* Licensed under the Apache License, Version 2.0 (the "License");
* you may not use this file except in compliance with the License.
* You may obtain a copy of the License at
*
*     http://www.apache.org/licenses/LICENSE-2.0
*
* Unless required by applicable law or agreed to in writing, software
* distributed under the License is distributed on an "AS IS" BASIS,
* WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
* See the License for the specific language governing permissions and
* limitations under the License.
*******************************************************************************/-->

# Release Notes <!-- omit in toc -->
This document contains changes of oneTBB compared to the last release.

## Table of Contents <!-- omit in toc -->
- [:tada: New Features](#tada-new-features)
- [:rotating\_light: Known Limitations](#rotating_light-known-limitations)
- [:octocat: Open-Source Contributions Integrated](#octocat-open-source-contributions-integrated)

## :tada: New Features
- Improved Hybrid CPU and NUMA Platforms API Support: Enhanced API availability for better compatibility with Hybrid CPU and NUMA platforms.
- Added support for verifying signatures of dynamic dependencies at runtime. To enable this feature, specify
``-DTBB_VERIFY_DEPENDENCY_SIGNATURE=ON`` when invoking CMake.
- Added support for printing warning messages about issues in dynamic dependency loading. To see these messages in the console, build the library with the ``TBB_DYNAMIC_LINK_WARNING`` macro defined.
- Added a Natvis file for custom visualization of TBB containers when debugging with Microsoft* Visual Studio.
- Refined Environment Setup: Replaced CPATH with ``C_INCLUDE_PATH and CPLUS_INCLUDE_PATH`` in environment setup to avoid unintended compiler warnings caused by globally applied include paths. 


## :rotating_light: Known Limitations
- The ``oneapi::tbb::info`` namespace interfaces might unexpectedly change the process affinity mask on Windows* OS systems (see https://github.com/open-mpi/hwloc/issues/366 for details) when using hwloc version lower than 2.5.
- Using a hwloc version other than 1.11, 2.0, or 2.5 may cause an undefined behavior on Windows OS. See https://github.com/open-mpi/hwloc/issues/477 for details.
- The NUMA topology may be detected incorrectly on Windows* OS machines where the number of NUMA node threads exceeds the size of 1 processor group.
- On Windows OS on ARM64*, when compiling an application using oneTBB with the Microsoft* Compiler, the compiler issues a warning C4324 that a structure was padded due to the alignment specifier. Consider suppressing the warning by specifying /wd4324 to the compiler command line.
- C++ exception handling mechanism on Windows* OS on ARM64* might corrupt memory if an exception is thrown from any oneTBB parallel algorithm (see Windows* OS on ARM64* compiler issue: https://developercommunity.visualstudio.com/t/ARM64-incorrect-stack-unwinding-for-alig/1544293.
- When CPU resource coordination is enabled, tasks from a lower-priority ``task_arena`` might be executed before tasks from a higher-priority ``task_arena``.
- Using oneTBB on WASM* may cause applications to run in a single thread. See [Limitations of WASM Support](https://github.com/uxlfoundation/oneTBB/blob/master/WASM_Support.md#limitations).

> **_NOTE:_**  To see known limitations that impact all versions of oneTBB, refer to [oneTBB Documentation](https://uxlfoundation.github.io/oneTBB/main/intro/limitations.html).


## :octocat: Open-Source Contributions Integrated
- Fixed a CMake configuration error on systems with non-English locales. Contributed by moritz-h (https://github.com/uxlfoundation/oneTBB/pull/1606).
- Made the install destination of import libraries on Windows* configurable. Contributed by Bora Yalçıner (https://github.com/uxlfoundation/oneTBB/pull/1613).
- Resolved an in-source CMake build error. Contributed by Dmitrii Golovanov (https://github.com/uxlfoundation/oneTBB/pull/1670).
- Migrated the build system to Bazel* version 8.1.1. Contributed by Julian Amann (https://github.com/uxlfoundation/oneTBB/pull/1694).
- Fixed build errors on MinGW* and FreeBSD*. Contributed by John Ericson (https://github.com/uxlfoundation/oneTBB/pull/1696).
- Addressed build errors on macOS* when using the GCC compiler. Contributed by Oleg Butakov (https://github.com/uxlfoundation/oneTBB/pull/1603).
