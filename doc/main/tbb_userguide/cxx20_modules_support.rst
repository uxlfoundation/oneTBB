.. _cxx20_modules_support:

C++20 Modules Support
=====================

.. note::
    Support for C++20 modules is experimental and subject to change in future releases.

|short_name| provides a C++20 module interface unit, ``tbb.cppm``, that lets you use |short_name| via
``import tbb;`` instead of ``#include <oneapi/tbb.h>``. The module is installed as a
source file under ``<install-prefix>/include/oneapi/tbb.cppm`` and must be compiled as
part of your own build target.

CMake* Integration
******************

To add the ``tbb`` module to your CMake* target, locate ``tbb.cppm`` using the
``INTERFACE_INCLUDE_DIRECTORIES`` property of the ``TBB::tbb`` target and register it as
a ``CXX_MODULES`` file set:

.. note::
    To support C++20 modules, CMake 3.28 or later and a compiler and generator with C++20 modules
    support are required. Refer to the
    `CMake documentation <https://cmake.org/cmake/help/latest/manual/cmake-cxxmodules.7.html>`_
    for the list of supported compilers and generators.

.. code:: cmake

    cmake_minimum_required(VERSION 3.28)
    project(myapp CXX)

    set(CMAKE_CXX_STANDARD 20)

    find_package(TBB REQUIRED)

    add_executable(my_executable main.cpp)
    target_link_libraries(my_executable PRIVATE TBB::tbb)

    get_target_property(_tbb_include_dir TBB::tbb INTERFACE_INCLUDE_DIRECTORIES)
    target_sources(my_executable PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${_tbb_include_dir}
        FILES ${_tbb_include_dir}/oneapi/tbb.cppm
    )

An then in your C++ source files, you can import the module:

.. code:: cpp

    import tbb;

    int main() {
        tbb::parallel_for(0, 100, [](int i) { /* ... */ });
    }

.. note::
    Translation units built with ``import tbb;`` are ABI-compatible with those built using
    ``#include <oneapi/tbb.h>``. Because the exported API is attached to the global module
    fragment, the module name does not participate in symbol mangling, and both can be linked
    into the same program.

Usage Of Predefined Macros
**************************

C++20 modules do not export preprocessor macros. Macros defined in
``<oneapi/tbb/version.h>`` (such as ``TBB_VERSION`` or feature-test macros) are **not** available
after ``import tbb;``.

As a workaround, include the version header alongside the module import.

.. code:: cpp

    #define TBB_PREVIEW_FEATURE_X 1
    #include <oneapi/tbb/version.h>  // macros available here
    import tbb;

    static_assert(TBB_VERSION_MAJOR >= 2023, "Major version 2023 or later required");

    // Feature-test macros are also available
    #ifdef TBB_HAS_FEATURE_X
    // use feature X
    #endif

Enabling Preview Features
*************************

|short_name| preview features are gated by ``TBB_PREVIEW_*`` macros that are normally defined
before including TBB headers. With modules, macros defined by the consumer before
``import tbb;`` **cannot** affect the module's already-compiled interface.

To enable a preview feature, define the corresponding macro when **compiling**
``tbb.cppm`` itself. In CMake*, use ``target_compile_definitions``:

.. code:: cmake

    find_package(TBB REQUIRED)

    add_executable(my_executable main.cpp)
    target_link_libraries(my_executable PRIVATE TBB::tbb)

    get_target_property(_tbb_include_dir TBB::tbb INTERFACE_INCLUDE_DIRECTORIES)
    target_sources(my_executable PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${_tbb_include_dir}
        FILES ${_tbb_include_dir}/oneapi/tbb.cppm
    )

    # Enable preview features when compiling the module
    target_compile_definitions(my_executable PRIVATE TBB_PREVIEW_FEATURE_X=1)

After this, the preview API is included in the compiled module and available to all
translation units that use ``import tbb;``.
