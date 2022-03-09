# Copyright (c) 2021-2022 Intel Corporation

# --------------------------------------------------------------------------------------------------
# Find the HWLOC library
if (NOT TARGET HWLOC::hwloc)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules)
  find_package(HWLOC REQUIRED 2.5 VERSION)
endif()
# --------------------------------------------------------------------------------------------------

add_library(tcm SHARED tcm.cpp)

target_include_directories(tcm PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_link_libraries(tcm PUBLIC HWLOC::hwloc)
target_compile_definitions(tcm PUBLIC $<$<CONFIG:DEBUG>:TCM_DEBUG>)

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
