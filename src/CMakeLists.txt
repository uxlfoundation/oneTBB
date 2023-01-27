# Copyright (c) 2021-2022 Intel Corporation

cmake_minimum_required(VERSION 3.1)

if (EXISTS ${TCM_COMPILER_SETTINGS_FILE})
    include(${TCM_COMPILER_SETTINGS_FILE})
else()
    message(WARNING "TCM compiler settings not found ${TCM_COMPILER_SETTINGS_FILE}")
endif()

# --------------------------------------------------------------------------------------------------
# Find the HWLOC library
if (NOT TARGET HWLOC::hwloc)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules)
  find_package(HWLOC REQUIRED 2.5 VERSION)
endif()
# --------------------------------------------------------------------------------------------------

add_library(${PROJECT_NAME} SHARED tcm.cpp)

target_compile_options(${PROJECT_NAME}
  PRIVATE
  ${TCM_WARNING_LEVEL}
  ${TCM_WARNING_SUPPRESS}
  ${TCM_LIB_COMPILE_FLAGS}
  ${TCM_MMD_FLAG}
  ${TCM_DSE_FLAG}
  ${TCM_COMMON_COMPILE_FLAGS}
)
target_compile_definitions(${PROJECT_NAME} PRIVATE ${TCM_COMPILE_DEFINITIONS})

target_include_directories(${PROJECT_NAME} PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(${PROJECT_NAME}
  PUBLIC
  HWLOC::hwloc
  PRIVATE
  ${TCM_LIB_LINK_FLAGS}
)
target_compile_definitions(${PROJECT_NAME} PUBLIC $<$<CONFIG:DEBUG>:TCM_DEBUG>)

install(TARGETS ${PROJECT_NAME}
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
