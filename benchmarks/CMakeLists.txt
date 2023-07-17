# Copyright (C) 2023 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

cmake_minimum_required(VERSION 3.11)

find_package(TBB)
if (NOT TBB_FOUND)
    message(FATAL_ERROR "oneTBB was not found in CMake using 'find_package(TBB)'")
endif()

set(TCM_OPENMP_FLAG -qopenmp)
if (NOT CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
  message(WARNING "C++ compiler ID does not match the IntelLLVM, "
    "the OpenMP library might be not aware of Thread Composability Manager")
  include(FindOpenMP)
  set(TCM_OPENMP_FLAG -fopenmp)
endif()

add_subdirectory(synthetic)
add_subdirectory(tbb_examples)

add_custom_target(tcm_benchmarks)
add_dependencies(tcm_benchmarks composable_scenarios seismic)
