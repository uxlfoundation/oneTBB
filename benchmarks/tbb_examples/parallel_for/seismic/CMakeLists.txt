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

project(seismic CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

add_executable(seismic universe.cpp seismic_video.cpp main.cpp)

add_subdirectory(../../common/gui gui)

target_link_libraries(seismic PUBLIC TBB::tbb Threads::Threads UI_LIB_seismic)

if (NOT TCM_OPENMP_NO_LINK_FLAG)
    target_link_options(seismic
        PRIVATE
        ${TCM_OPENMP_FLAG}
    )
endif()

if (TCM_CUSTOM_OPENMP_LIB_DIR)
    target_link_options(seismic
        PRIVATE
        -Wl,--disable-new-dtags
        -rpath ${TCM_CUSTOM_OPENMP_LIB_DIR}
    )
endif()

target_compile_options(seismic PRIVATE ${TBB_CXX_STD_FLAG} ${TCM_OPENMP_FLAG})

if (EXAMPLES_UI_MODE STREQUAL "con")
    target_compile_definitions(seismic PRIVATE _CONSOLE)
endif()

set(EXECUTABLE "$<TARGET_FILE:seismic>")
set(ARGS auto 0)
set(PERF_ARGS auto 10000 silent)
set(LIGHT_ARGS 1:2 100)

add_execution_target(run_seismic seismic ${EXECUTABLE} "${ARGS}")
add_execution_target(perf_run_seismic seismic ${EXECUTABLE} "${PERF_ARGS}")
add_execution_target(light_test_seismic seismic ${EXECUTABLE} "${LIGHT_ARGS}")

add_test(NAME seismic COMMAND seismic)
