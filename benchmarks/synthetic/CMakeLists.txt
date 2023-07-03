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

add_executable(composable_scenarios composable_scenarios.cpp)

target_compile_options(composable_scenarios
    PRIVATE
    ${TCM_WARNING_LEVEL}
    ${TCM_TEST_WARNING_FLAGS}
    ${TCM_WARNING_SUPPRESS}
    ${TCM_MMD_FLAG}
    ${TCM_DSE_FLAG}
    ${TCM_COMMON_COMPILE_FLAGS}
    ${TCM_OPENMP_FLAG}
)


target_link_options(composable_scenarios
    PRIVATE
    -Wl,--disable-new-dtags
    -rpath ${TCM_CUSTOM_OPENMP_LIB_DIR}
)

target_link_libraries(composable_scenarios PUBLIC TBB::tbb)
add_test(NAME composable_scenarios COMMAND composable_scenarios)
