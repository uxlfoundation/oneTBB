# Copyright (C) 2020-2026 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

cmake_minimum_required(VERSION 3.5...3.31.3)

add_library(UI_LIB_${PROJECT_NAME} STATIC ${CMAKE_CURRENT_LIST_DIR}/convideo.cpp)

target_compile_definitions(UI_LIB_${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:${TCM_DEBUG_FLAG}>)
target_compile_options(UI_LIB_${PROJECT_NAME} PRIVATE ${TBB_CXX_STD_FLAG})
