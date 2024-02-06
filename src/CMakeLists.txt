# Copyright (C) 2023-2024 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

cmake_minimum_required(VERSION 3.1)

# --------------------------------------------------------------------------------------------------
# Find the HWLOC library
if (NOT TARGET HWLOC::hwloc)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules)
  find_package(HWLOC REQUIRED 2.5 VERSION)
endif()
# --------------------------------------------------------------------------------------------------

add_library(tcm SHARED
  tcm.cpp
  version.cpp
)

if (WIN32)
    string(TIMESTAMP tcm_build_year "%Y" UTC)
    if (NOT tcm_build_year)
        message(FATAL_ERROR "Cannot determine the current year to substitute it in resource file.")
    endif()
    # Copy resource file template tcm.rc.in to CMAKE_CURRENT_BINARY_DIR substituting defined
    # variables
    configure_file(tcm.rc.in tcm.rc)
    unset(tcm_build_year)
    target_sources(tcm PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/tcm.rc)
endif()

add_library(TCM::tcm ALIAS tcm)

if (TCM_PROFILE STREQUAL "api-durations")
  set(TCM_PROFILE_FLAGS -D__TCM_ENABLE_TIME_TRACER=1)
endif()

target_compile_options(tcm
  PRIVATE
  ${TCM_WARNING_LEVEL}
  ${TCM_WARNING_SUPPRESS}
  ${TCM_LIB_COMPILE_FLAGS}
  ${TCM_MMD_FLAG}
  ${TCM_DSE_FLAG}
  ${TCM_COMMON_COMPILE_FLAGS}
  ${TCM_VISIBILITY_INLINES_HIDDEN_FLAG}
  ${TCM_PROFILE_FLAGS}
)

string(TIMESTAMP tcm_build_time "%Y-%m-%d %H:%M:%S UTC" UTC)
target_compile_definitions(tcm PRIVATE
  ${TCM_COMPILE_DEFINITIONS}
  __TCM_BUILD_TIME="${tcm_build_time}"
  $<$<BOOL:${TCM_COMMIT_ID}>:__TCM_COMMIT_ID="${TCM_COMMIT_ID}">
)

target_include_directories(tcm PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Prefer using target_link_options instead of target_link_libraries to specify link options because
# target_link_libraries may incorrectly handle some options (on Windows, for example).
if (COMMAND target_link_options)
    target_link_options(tcm
        PRIVATE
        ${TCM_LIB_LINK_FLAGS}
    )
else()
    target_link_libraries(tcm
        PRIVATE
        ${TCM_LIB_LINK_FLAGS}
    )
endif()

target_link_libraries(tcm
  PRIVATE
  HWLOC::hwloc
  ${TCM_COMMON_LINK_LIBS}
)
target_compile_definitions(tcm PUBLIC $<$<CONFIG:DEBUG>:TCM_DEBUG>)

set_target_properties(tcm PROPERTIES
  VERSION ${_tcm_ver_major}.${_tcm_ver_minor}.${_tcm_ver_patch}
  SOVERSION ${_tcm_ver_major}
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)

install(TARGETS tcm
  EXPORT ${PROJECT_NAME}Targets
  LIBRARY
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME
    DESTINATION ${CMAKE_INSTALL_BINDIR}
  ARCHIVE
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
  PUBLIC_HEADER
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

# if (WIN32)
#   install(IMPORTED_RUNTIME_ARTIFACTS HWLOC::hwloc
#     RUNTIME
#       DESTINATION ${CMAKE_INSTALL_BINDIR}
#   )
# else()
#   get_target_property(hwloc_lib HWLOC::hwloc IMPORTED_LOCATION)
#   get_filename_component(hwloc_lib_dir "${hwloc_lib}" DIRECTORY)
#   file(GLOB hwloc_binaries ${hwloc_lib_dir}/libhwloc.so*)
#   install(FILES ${hwloc_binaries} DESTINATION ${CMAKE_INSTALL_LIBDIR})
# endif()
