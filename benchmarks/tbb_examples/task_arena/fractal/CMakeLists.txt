# Copyright (C) 2020-2026 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

cmake_minimum_required(VERSION 3.5.0...3.31.3)

project(fractal CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

add_executable(fractal fractal.cpp main.cpp )

add_subdirectory(../../common/gui gui)

target_link_libraries(fractal PUBLIC TBB::tbb Threads::Threads UI_LIB_fractal)

if (NOT TCM_OPENMP_MISSING_LINK_FLAG)
    target_link_options(fractal
        PRIVATE
        ${TCM_OPENMP_FLAG}
    )
endif()

if (TCM_CUSTOM_OPENMP_LIB_DIR AND NOT WIN32)
    target_link_options(fractal
        PRIVATE
        -Wl,--disable-new-dtags
        -rpath ${TCM_CUSTOM_OPENMP_LIB_DIR}
    )
endif()

target_compile_definitions(fractal PRIVATE $<$<CONFIG:Debug>:${TCM_DEBUG_FLAG}>)
target_compile_options(fractal PRIVATE ${TBB_CXX_STD_FLAG} ${TCM_OPENMP_FLAG})

set(EXECUTABLE "$<TARGET_FILE:fractal>")
set(ARGS auto)
set(PERF_ARGS auto 1 1000000 silent)
set(LIGHT_ARGS auto 1 1000)

add_execution_target(run_fractal fractal ${EXECUTABLE} "${ARGS}")
add_execution_target(perf_run_fractal fractal ${EXECUTABLE} "${PERF_ARGS}")
add_execution_target(light_test_fractal fractal ${EXECUTABLE} "${LIGHT_ARGS}")

add_test(NAME fractal COMMAND fractal ${LIGHT_ARGS} threading-runtime=mix)
