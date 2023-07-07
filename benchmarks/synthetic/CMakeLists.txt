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

function(add_synthetic_bench)
    set(oneValueArgs NAME ENVIRONMENT)
    set(multiValueArgs DEPENDENCIES)
    cmake_parse_arguments(_tcm_bench "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    add_executable(${_tcm_bench_NAME} ${_tcm_bench_NAME}.cpp)

    target_include_directories(${_tcm_bench_NAME} PRIVATE common)
    target_compile_options(${_tcm_bench_NAME}
        PRIVATE
        ${TCM_WARNING_LEVEL}
        ${TCM_TEST_WARNING_FLAGS}
        ${TCM_WARNING_SUPPRESS}
        ${TCM_MMD_FLAG}
        ${TCM_DSE_FLAG}
        ${TCM_COMMON_COMPILE_FLAGS}
        ${TCM_OPENMP_FLAG}
    )

    if (NOT TCM_OPENMP_NO_LINK_FLAG)
        target_link_options(${_tcm_bench_NAME}
            PRIVATE
            ${TCM_OPENMP_FLAG}
        )
    endif()

    if (TCM_CUSTOM_OPENMP_LIB_DIR)
        target_link_options(${_tcm_bench_NAME}
            PRIVATE
            -Wl,--disable-new-dtags
            -rpath ${TCM_CUSTOM_OPENMP_LIB_DIR}
        )
    endif()

    target_link_libraries(${_tcm_bench_NAME} PUBLIC TBB::tbb)

    add_test(NAME ${_tcm_bench_NAME} COMMAND ${_tcm_bench_NAME})

    set_tests_properties(${_tcm_bench_NAME} PROPERTIES ENVIRONMENT "${_tcm_bench_ENVIRONMENT}")
endfunction()

add_synthetic_bench(NAME composable_scenarios)
add_synthetic_bench(NAME ubench_sequenced)
add_synthetic_bench(NAME ubench_nested)
add_synthetic_bench(NAME ubench_concurrent)
