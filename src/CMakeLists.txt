# Copyright (C) 2023 Intel Corporation
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

add_library(TCM::tcm ALIAS tcm)

target_compile_options(tcm
  PRIVATE
  ${TCM_WARNING_LEVEL}
  ${TCM_WARNING_SUPPRESS}
  ${TCM_LIB_COMPILE_FLAGS}
  ${TCM_MMD_FLAG}
  ${TCM_DSE_FLAG}
  ${TCM_COMMON_COMPILE_FLAGS}
)
target_compile_definitions(tcm PRIVATE ${TCM_COMPILE_DEFINITIONS})

target_include_directories(tcm PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(tcm
  PRIVATE
  HWLOC::hwloc
  ${TCM_LIB_LINK_FLAGS}
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
