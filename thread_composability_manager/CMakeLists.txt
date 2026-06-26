# Copyright (c) 2023 Intel Corporation
# Copyright (c) 2026 UXL Foundation Contributors
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.5...3.31.3)

file(READ include/tcm/version.h _tcm_version_info)
string(REGEX REPLACE ".*#define TCM_VERSION_MAJOR ([0-9]+).*" "\\1" _tcm_ver_major "${_tcm_version_info}")
string(REGEX REPLACE ".*#define TCM_VERSION_MINOR ([0-9]+).*" "\\1" _tcm_ver_minor "${_tcm_version_info}")
string(REGEX REPLACE ".*#define TCM_VERSION_PATCH ([0-9]+).*" "\\1" _tcm_ver_patch "${_tcm_version_info}")

project(TCM LANGUAGES CXX VERSION ${_tcm_ver_major}.${_tcm_ver_minor}.${_tcm_ver_patch})

set(TCM_SOURCE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# TODO: Use PROJECT_IS_TOP_LEVEL to determine whether TCM is a standalone project
set(IS_TCM_STANDALONE FALSE) # Assume that TCM is a subdirectory of another project
if(TCM_SOURCE_ROOT_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(IS_TCM_STANDALONE TRUE)   # TCM is turned out to be the top-level project
endif()

option(TCM_TEST "Enable testing" ON)
option(TCM_STRICT "Treat compiler warnings as errors" ON)
option(TCM_FUZZ_TESTING "Enable FUZZ testing of TCM" OFF)

if (NOT CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_EXTENSIONS OFF) # use -std=c++... instead of -std=gnu++...

# Detect architecture (bitness).
if (CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(TCM_ARCH 32)
else()
    set(TCM_ARCH 64)
endif()

# Dealing with dependencies
# --------------------------------------------------------------------------------------------------
if (NOT DEFINED TCM_BUILD OR TCM_BUILD OR TCM_TEST)
  # Find the HWLOC library
  if (NOT TARGET HWLOC::hwloc)
    list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/modules)
    if (IS_TCM_STANDALONE OR TCM_BUILD)
      find_package(HWLOC REQUIRED 2.5 VERSION)
    else()
      find_package(HWLOC 2.5 QUIET)
    endif()
    if (HWLOC_FOUND)
      message(STATUS "Enabling build of Thread Composability Manager (TCM)")
    else()
      message(WARNING "Disabling build of Thread Composability Manager (TCM) "
                      "since HWLOC cannot be found.")
      return()
    endif()
  endif()
endif()
# --------------------------------------------------------------------------------------------------

set(TCM_PROFILE ${TCM_PROFILE} CACHE STRING "Enable profiling of TCM functionality" FORCE)

# include compile settings
set(TCM_COMPILER_SETTINGS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/compilers/${CMAKE_CXX_COMPILER_ID}.cmake)
if (EXISTS ${TCM_COMPILER_SETTINGS_FILE})
    include(${TCM_COMPILER_SETTINGS_FILE})
else()
    message(WARNING "TCM compiler settings not found ${TCM_COMPILER_SETTINGS_FILE}")
endif()

if (IS_TCM_STANDALONE OR NOT DEFINED CMAKE_DEBUG_POSTFIX)
  set(CMAKE_DEBUG_POSTFIX _debug)
endif()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
    message(STATUS "CMAKE_BUILD_TYPE is not specified. Using default: ${CMAKE_BUILD_TYPE}")
    # Possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if (NOT DEFINED TCM_OUTPUT_DIR_BASE)
     string(REGEX MATCH "^([0-9]+\.[0-9]+|[0-9]+)" _tcm_compiler_version_short ${CMAKE_CXX_COMPILER_VERSION})
     string(TOLOWER ${CMAKE_CXX_COMPILER_ID}_${_tcm_compiler_version_short}_cxx${CMAKE_CXX_STANDARD} TCM_OUTPUT_DIR_BASE)
     unset(_tcm_compiler_version_short)
endif()

foreach(output_type LIBRARY ARCHIVE PDB RUNTIME)
    if (CMAKE_BUILD_TYPE)
        string(TOLOWER ${CMAKE_BUILD_TYPE} _tcm_build_type_lower)
        if (NOT CMAKE_${output_type}_OUTPUT_DIRECTORY)
          set(CMAKE_${output_type}_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${TCM_OUTPUT_DIR_BASE}_${_tcm_build_type_lower})
        endif()
        unset(_tcm_build_type_lower)
    endif()

    if (CMAKE_CONFIGURATION_TYPES)
        foreach(suffix ${CMAKE_CONFIGURATION_TYPES})
            string(TOUPPER ${suffix} _tcm_suffix_upper)
            string(TOLOWER ${suffix} _tcm_suffix_lower)
            if (NOT CMAKE_${output_type}_OUTPUT_DIRECTORY_${_tcm_suffix_upper})
              set(CMAKE_${output_type}_OUTPUT_DIRECTORY_${_tcm_suffix_upper} ${CMAKE_BINARY_DIR}/${TCM_OUTPUT_DIR_BASE}_${_tcm_suffix_lower})
            endif()
        endforeach()
        unset(_tcm_suffix_lower)
        unset(_tcm_suffix_upper)
    endif()
endforeach()

# --------------------------------------------------------------------------------------------------
# Define Cpack variables + CMake installation related includes
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
string(TOLOWER ${PROJECT_NAME}-${CPACK_PACKAGE_VERSION}-${TCM_ARCH}-${CMAKE_BUILD_TYPE}-${CMAKE_SYSTEM_NAME} CPACK_PACKAGE_FILE_NAME)
set(CPACK_GENERATOR ZIP)

# Note: this is an internal non-documented variable set by CPack
if (NOT CPack_CMake_INCLUDED)
  include(CPack)
endif()
include(CMakePackageConfigHelpers)
include(CheckCXXCompilerFlag)
include(GNUInstallDirs)
# --------------------------------------------------------------------------------------------------

# --------------------------------------------------------------------------------------------------
# Enable CMake plug-in mechanism
file(GLOB FILES_WITH_EXTRA_TARGETS ${CMAKE_CURRENT_SOURCE_DIR}/cmake/*.cmake)
foreach(FILE_WITH_EXTRA_TARGETS ${FILES_WITH_EXTRA_TARGETS})
    include(${FILE_WITH_EXTRA_TARGETS})
endforeach()
# --------------------------------------------------------------------------------------------------
if (TCM_DIR)
  # Allow specifying external TCM to test with.
  # Do not add main targets and installation instructions in that case.
  message(STATUS "Using external TCM for testing")
  find_package(TCM REQUIRED)
elseif(IS_TCM_STANDALONE OR NOT DEFINED TCM_BUILD OR TCM_BUILD)
  add_subdirectory(src)
endif()

if (TCM_TEST)
  enable_testing()
  add_subdirectory(test)
endif()

add_subdirectory(cmake/post_install)
