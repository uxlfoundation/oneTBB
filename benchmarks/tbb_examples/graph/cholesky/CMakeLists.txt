# Copyright (C) 2024-2026 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

cmake_minimum_required(VERSION 3.5...3.31.3)

project(cholesky CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

# TODO: Consider using FindMKL module
find_library(MKL_RT_LIB mkl_rt PATHS ENV LIBRARY_PATH)
if(NOT MKL_RT_LIB)
    message(WARNING "Cannot find Intel(R) Math Kernel Library (Intel(R) MKL).")
    return()
endif()

add_executable(cholesky init.cpp cholesky.cpp)
target_link_libraries(cholesky
    ${MKL_RT_LIB}
    TBB::tbb
    Threads::Threads
)

target_compile_definitions(cholesky PRIVATE $<$<CONFIG:Debug>:${TCM_DEBUG_FLAG}>)
target_compile_options(cholesky PRIVATE ${TBB_CXX_STD_FLAG})

set(EXECUTABLE "$<TARGET_FILE:cholesky>")
set(LIGHT_PERF_ARGS 5000 250 5)
set(PERF_ARGS 10000 1000 5)
add_execution_target(light_run_cholesky cholesky ${EXECUTABLE} "${LIGHT_PERF_ARGS}")
add_execution_target(heavy_run_cholesky cholesky ${EXECUTABLE} "${HEAVY_PERF_ARGS}")
