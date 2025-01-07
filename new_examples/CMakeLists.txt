cmake_minimum_required (VERSION 3.4)

project(tbb_tutorials LANGUAGES CXX)

find_package(TBB REQUIRED)
find_package(Threads REQUIRED)

include (CTest)
include (CheckIncludeFileCXX)
include (CheckCXXCompilerFlag)

# Check for SYCL support
set(FSYCL_OPTION "-fsycl")
check_cxx_compiler_flag(${FSYCL_OPTION} _cxx_compiler_has_fsycl_option)

if (_cxx_compiler_has_fsycl_option)
  CHECK_INCLUDE_FILE_CXX("sycl/sycl.hpp" _cxx_compiler_has_sycl_header ${FSYCL_OPTION})
  set(_cxx_compiler_supports_sycl BOOL(${cxx_compiler_has_sycl_header}))
else()
  set(_cxx_compiler_supports_sycl FALSE)
endif()

set(_regexp_exclude_current_binary_dir "(${CMAKE_CURRENT_BINARY_DIR})")
set(_regexp_cxx17_required "(parallel_for_each_fwd_substitution.cpp|graph_limiting_messages.cpp|graph_loops.cpp|graph_node_priorities.cpp|graph_reestablish_order.cpp|graph_small_nodes.cpp|graph_two_nodes_deduced.cpp|graph_with_join.cpp|migrate_parallel_do.cpp|migrate_recycling.cpp)")
set(_regexp_cxx20_required "(task_group_with_dependencies.cpp|migrate_bypass_tasks.cpp|intro_parallel_for_transform.cpp|migrate_tasks_adding_work.cpp)")
set(_regexp_sycl_required "(graph_async_sycl.cpp|resumable_tasks.cpp)")
set(_regexp_tbbmalloc_required "(memalloc)")
set(_regexp_tbbmalloc_windows_only "(memalloc/windows_proxy.cpp)")

macro(tbb_tutorials_add_example _example_source_file)
    if (NOT _example_source_file MATCHES "${_regexp_exclude_current_binary_dir}")
      if (CMAKE_CXX_STANDARD LESS 17 AND
          _example_source_file MATCHES "${_regexp_cxx17_required}")
        message(STATUS "Skip ${_example_source_file} since C++17 or later is required")
        continue()
      endif()

      if (CMAKE_CXX_STANDARD LESS 20 AND
          _example_source_file MATCHES "${_regexp_cxx20_required}")
        message(STATUS "Skip ${_example_source_file} since C++20 or later is required")
        continue()
      endif()

      if (NOT _cxx_compiler_supports_sycl AND
          _example_source_file MATCHES "${_regexp_sycl_required}")
        message(STATUS "Skip ${_example_source_file} since the compiler does not support SYCL")
        continue()
      endif()

      if (NOT CMAKE_HOST_WIN32 AND
          _example_source_file MATCHES "${_regexp_tbbmalloc_windows_only}")
        message(STATUS "Skip ${_example_source_file} since the OS is not supported by this example")
        continue()
      endif()

      get_filename_component(_example_name ${_example_source_file} NAME)
      string(REPLACE "\.cpp" "" _example_name ${_example_name})

      add_executable(${_example_name} ${_example_source_file})

      target_include_directories(${_example_name} PRIVATE ${TBB_INCLUDE_DIRS})
      target_link_libraries(${_example_name} TBB::tbb Threads::Threads)

      if (_example_source_file MATCHES "${_regexp_tbbmalloc_required}")
        target_link_libraries(${_example_name} TBB::tbbmalloc)
      endif()

      if (_example_source_file MATCHES "${_regexp_sycl_required}")
        target_compile_options(${_example_name} PRIVATE "${FSYCL_OPTION}")
        target_link_options(${_example_name} PRIVATE "${FSYCL_OPTION}")
      endif()

      target_include_directories(${_example_name} PRIVATE
          $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../common>)
      add_test(${_example_name} ${_example_name})
    endif()
endmacro()

if (BUILD_TESTING)
    enable_testing()

    file(GLOB_RECURSE EXAMPLE_SOURCE_FILES "*.cpp")
    foreach(_example_source_file IN LISTS EXAMPLE_SOURCE_FILES)
        tbb_tutorials_add_example(${_example_source_file})
    endforeach()
endif()
