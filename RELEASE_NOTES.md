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

# Table of Contents <!-- omit in toc -->
- [oneTBB 2022.2.0](#onetbb-2022-2-0)
- [oneTBB 2022.1.0](#onetbb-2022-1-0)
- [oneTBB 2022.0.0](#onetbb-2022-0-0)

# oneTBB 2022.2.0

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

# oneTBB 2022.1.0

## :tada: New Features
- The oneTBB repository migrated to the new [UXL Foundation](https://github.com/uxlfoundation/oneTBB) organization.
- ``blocked_nd_range`` is now a fully supported feature.
- Introduced the ``ONETBB_SPEC_VERSION`` macro to specify the version of oneAPI specification implemented by the current version of the library.


## :rocket: Preview Features
- Added the explicit deduction guides to ``blocked_nd_range`` to support C++17 Class Template Argument Deduction.
- Extended ``task_arena`` API to select TBB workers leave policy and to hint the start and the end of parallel computations.


## :rotating_light: Known Limitations
- The ``oneapi::tbb::info`` namespace interfaces might unexpectedly change the process affinity mask on Windows* OS systems (see https://github.com/open-mpi/hwloc/issues/366 for details) when using hwloc version lower than 2.5.
- Using a hwloc version other than 1.11, 2.0, or 2.5 may cause an undefined behavior on Windows OS. See https://github.com/open-mpi/hwloc/issues/477 for details.
- The NUMA topology may be detected incorrectly on Windows* OS machines where the number of NUMA node threads exceeds the size of 1 processor group.
- On Windows OS on ARM64*, when compiling an application using oneTBB with the Microsoft* Compiler, the compiler issues a warning C4324 that a structure was padded due to the alignment specifier. Consider suppressing the warning by specifying /wd4324 to the compiler command line.
- C++ exception handling mechanism on Windows* OS on ARM64* might corrupt memory if an exception is thrown from any oneTBB parallel algorithm (see Windows* OS on ARM64* compiler issue: https://developercommunity.visualstudio.com/t/ARM64-incorrect-stack-unwinding-for-alig/1544293.
- When CPU resource coordination is enabled, tasks from a lower-priority ``task_arena`` might be executed before tasks from a higher-priority ``task_arena``.
- Using oneTBB on WASM* may cause applications to run in a single thread. See [Limitations of WASM Support](https://github.com/uxlfoundation/oneTBB/blob/master/WASM_Support.md#limitations).

> **_NOTE:_**  To see known limitations that impact all versions of oneTBB, refer to [oneTBB Documentation](https://uxlfoundation.github.io/oneTBB/main/intro/limitations.html).


## :hammer: Issues Fixed
- Fixed deadlock when using `tbb::concurrent_vector::grow_by()` (https://github.com/uxlfoundation/oneTBB/issues/1531).
- Fixed assertion in the Debug version of oneTBB on systems with multiple processor groups.
- Fixed issues with Flow Graph priorities when using limited concurrency nodes (https://github.com/uxlfoundation/oneTBB/issues/1595).
- Improved support of ``tbb::task_arena::constraints`` functionality on Windows* systems with multiple processor groups.
- Fixed ``concurrent_queue`` and ``concurrent_bounded_queue`` capacity preserving on copying, moving, and swapping (https://github.com/uxlfoundation/oneTBB/issues/1598).
- Fixed ``parallel_for_each`` compilation issues on GCC 9 in C++20 mode (https://github.com/uxlfoundation/oneTBB/issues/1552).


## :octocat: Open-Source Contributions Integrated
- Fixed linkage errors when the application is built with the hidden symbols visibility. Contributed by Vladislav Shchapov (https://github.com/uxlfoundation/oneTBB/pull/1114).
- On Linux* OS, for external thread, determined stack size using POSIX* API instead of relying on the stack size of a worker thread. Contributed by bongkyu7-kim (https://github.com/uxlfoundation/oneTBB/pull/1485).
- Added a CMake option to use relative paths instead of full paths in debug information. Contributed by Fang Xu (https://github.com/uxlfoundation/oneTBB/pull/1401).
- Improved OpenBSD* support by removing the use of direct syscalls. Contributed by Brad Smith (https://github.com/uxlfoundation/oneTBB/pull/1499).
- Fixed build issues on ARM64* when using Bazel. Contributed by snadampal (https://github.com/uxlfoundation/oneTBB/pull/1571).
- Suppressed deprecation warnings for CMake versions earlier than 3.10 when using the latest CMake. Contributed by Vladislav Shchapov (https://github.com/uxlfoundation/oneTBB/pull/1585).

# oneTBB 2022.0.0

## :tada: Preview Features
- Extended the Flow Graph receiving nodes with a new ``try_put_and_wait`` API that submits a message to the graph and waits for its completion.

## :rotating_light: Known Limitations
- The ``oneapi::tbb::info`` namespace interfaces might unexpectedly change the process affinity mask on Windows* OS systems (see https://github.com/open-mpi/hwloc/issues/366 for details) when using hwloc version lower than 2.5.
- Using a hwloc version other than 1.11, 2.0, or 2.5 may cause an undefined behavior on Windows OS. See https://github.com/open-mpi/hwloc/issues/477 for details.
- The NUMA topology may be detected incorrectly on Windows* OS machines where the number of NUMA node threads exceeds the size of 1 processor group.
- On Windows OS on ARM64*, when compiling an application using oneTBB with the Microsoft* Compiler, the compiler issues a warning C4324 that a structure was padded due to the alignment specifier. Consider suppressing the warning by specifying /wd4324 to the compiler command line.
- C++ exception handling mechanism on Windows* OS on ARM64* might corrupt memory if an exception is thrown from any oneTBB parallel algorithm (see Windows* OS on ARM64* compiler issue: https://developercommunity.visualstudio.com/t/ARM64-incorrect-stack-unwinding-for-alig/1544293.
- When CPU resource coordination is enabled, tasks from a lower-priority ``task_arena`` might be executed before tasks from a higher-priority ``task_arena``.
- Using oneTBB on WASM*, may cause applications to run in a single thread. See [Limitations of WASM Support](https://github.com/oneapi-src/oneTBB/blob/master/WASM_Support.md#limitations).

> **_NOTE:_**  To see known limitations that impact all versions of oneTBB, refer to [oneTBB Documentation](https://oneapi-src.github.io/oneTBB/main/intro/limitations.html).


## :hammer: Issues Fixed
- Fixed the missed signal for thread request for enqueue operation.
- Significantly improved scalability of ``task_group``, ``flow_graph``, and ``parallel_for_each``.
- Removed usage of ``std::aligned_storage`` deprecated in C++23 (Inspired by Valery Matskevich https://github.com/oneapi-src/oneTBB/pull/1394).
- Fixed the issue where ``oneapi::tbb::info`` interfaces might interfere with the process affinity mask on the Windows* OS systems with multiple processor groups.


## :octocat: Open-Source Contributions Integrated
- Detect the GNU Binutils version to determine WAITPKG support better. Contributed by Martijn Courteaux (https://github.com/oneapi-src/oneTBB/pull/1347).
- Fixed the build on non-English locales. Contributed by Vladislav Shchapov (https://github.com/oneapi-src/oneTBB/pull/1450).
- Improved Bazel support. Contributed by Julian Amann (https://github.com/oneapi-src/oneTBB/pull/1434).
