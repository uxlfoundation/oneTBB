# Copyright (c) 2021-2023 Intel Corporation

find_package(TBB)
if (NOT TBB_FOUND)
    message(FATAL_ERROR "oneTBB was not found in CMake using 'find_package(TBB)'")
endif()

if (NOT CMAKE_CXX_COMPILER_ID MATCHES "IntelLLVM")
    return()
endif()

add_custom_target(tcm_benchmarks)

add_executable(composable_scenarios composable_scenarios.cpp)
set(TCM_OPENMP_FLAG -qopenmp)
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
    ${TCM_OPENMP_FLAG}
    -Wl,--disable-new-dtags
    -rpath ${TCM_CUSTOM_OPENMP_LIB_DIR}
)

target_link_libraries(composable_scenarios PUBLIC TBB::tbb)
add_test(NAME composable_scenarios COMMAND composable_scenarios)

add_dependencies(tcm_benchmarks composable_scenarios)

