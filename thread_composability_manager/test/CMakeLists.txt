# Copyright (c) 2023 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if (NOT MSVC) # no need to change options for MSVC
    # suppress extra warnings:
    # - suggest braces around initialization of subobject
    # - missing field initializer
    set(TCM_WARNING_LEVEL ${TCM_WARNING_LEVEL} -Wno-missing-braces)
endif()

# Find the HWLOC library
if (NOT TARGET HWLOC::hwloc)
  list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/modules)
  find_package(HWLOC REQUIRED 2.5 VERSION)
endif()

# Find Threads library (for example, pthread)
find_package(Threads REQUIRED)

function(add_tcm_test)
  set(oneValueArgs NAME)
  set(multiValueArgs ENVIRONMENT DEPENDENCIES)
  cmake_parse_arguments(_tcm_test "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  add_executable(${_tcm_test_NAME} ${_tcm_test_NAME}.cpp)
  target_compile_options(${_tcm_test_NAME}
    PRIVATE
    ${TCM_WARNING_LEVEL}
    ${TCM_TEST_WARNING_FLAGS}
    ${TCM_WARNING_SUPPRESS}
    ${TCM_MMD_FLAG}
    ${TCM_DSE_FLAG}
    ${TCM_COMMON_COMPILE_FLAGS}
  )
  target_compile_definitions(${_tcm_test_NAME} PRIVATE ${TCM_COMPILE_DEFINITIONS})

  target_link_libraries(${_tcm_test_NAME} PUBLIC
    TCM::tcm Threads::Threads
    ${_tcm_test_DEPENDENCIES}
    ${TCM_COMMON_LINK_LIBS}
  )
  add_test(NAME ${_tcm_test_NAME} COMMAND ${_tcm_test_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

  set_tests_properties(${_tcm_test_NAME} PROPERTIES ENVIRONMENT "${_tcm_test_ENVIRONMENT}")
endfunction()

add_tcm_test(NAME test_basic_apis ENVIRONMENT "TCM_ENABLE=1" DEPENDENCIES HWLOC::hwloc)

if (TCM_FUZZ_TESTING)
  add_tcm_test(NAME test_fuzzing ENVIRONMENT "TCM_ENABLE=1")

  set(TCM_FUZZ_OPTION -fsanitize=fuzzer)
  target_compile_options(test_fuzzing PRIVATE ${TCM_FUZZ_OPTION} -DTEST_EXECUTABLE_DIR="${CMAKE_RUNTIME_OUTPUT_DIRECTORY}")
  target_link_options(test_fuzzing PRIVATE ${TCM_FUZZ_OPTION})
  return()
endif()

# FCFS distriubtion strategy will not be available for end user since TCM_RESOURCE_DISTRIBUTION_STRATEGY is disabled
# add_tcm_test(NAME test_fcfsc ENVIRONMENT "TCM_ENABLE=1;TCM_RESOURCE_DISTRIBUTION_STRATEGY=FCFS")
add_tcm_test(NAME test_rb ENVIRONMENT "TCM_ENABLE=1" DEPENDENCIES HWLOC::hwloc)
add_tcm_test(NAME test_constrained_requests ENVIRONMENT "TCM_ENABLE=1" DEPENDENCIES HWLOC::hwloc)
add_tcm_test(NAME test_tcm_version)
add_tcm_test(NAME test_tcm_environment)
add_tcm_test(NAME test_tcm_disabling)

set_tests_properties(test_tcm_environment PROPERTIES
  FAIL_REGULAR_EXPRESSION "TCM: VERSION.*TCM: VERSION"
)

add_test(NAME test_tcm_environment_version_set COMMAND test_tcm_environment
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(test_tcm_environment_version_set PROPERTIES
  ENVIRONMENT "TCM_VERSION=1;TCM_ENABLE=1"
  PASS_REGULAR_EXPRESSION "TCM: VERSION.*TCM: VERSION;TCM: TCM_ENABLE *1"
  FAIL_REGULAR_EXPRESSION "ERROR"
)

add_test(NAME test_tcm_environment_disable COMMAND test_tcm_environment
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(test_tcm_environment_disable PROPERTIES
  PASS_REGULAR_EXPRESSION  "TCM: TCM *disabled"
  FAIL_REGULAR_EXPRESSION "ERROR"
)

add_test(NAME test_tcm_environment_disable_version_set COMMAND test_tcm_environment
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(test_tcm_environment_disable_version_set PROPERTIES
  ENVIRONMENT "TCM_VERSION=1"
  PASS_REGULAR_EXPRESSION  "TCM: VERSION.*TCM: VERSION;TCM: TCM_ENABLE *0"
  FAIL_REGULAR_EXPRESSION "ERROR"
)

# Group of tests that runs the same test in different environment checking TCM
# reacts accordingly
macro(add_tcm_disabling_test name environment expect_suggesting)
    add_test(NAME test_tcm_disabling_${name}
        COMMAND ${CMAKE_COMMAND} -E env
        --unset=TCMROOT
        --unset=TBBROOT
        --unset=ONEAPI_ROOT
        --unset=CMPLR_ROOT
        ${environment}
        $<TARGET_FILE:test_tcm_disabling>
        WORKING_DIRECTORY $<TARGET_FILE_DIR:test_tcm_disabling>
    )
    set_tests_properties(test_tcm_disabling_${name} PROPERTIES
        ENVIRONMENT "${environment}"
    )
    if (${expect_suggesting})
        set_tests_properties(test_tcm_disabling_${name} PROPERTIES
            PASS_REGULAR_EXPRESSION "Note: Several threading*"
            FAIL_REGULAR_EXPRESSION "ERROR"
        )
    else()
        set_tests_properties(test_tcm_disabling_${name} PROPERTIES
            FAIL_REGULAR_EXPRESSION "Note: Several threading*;ERROR"
        )
    endif()
endmacro()
add_tcm_disabling_test(with_env_untouched "" FALSE)
add_tcm_disabling_test(with_empty_value "TCM_ENABLE=" FALSE)
add_tcm_disabling_test(with_null_value "TCM_ENABLE=0" FALSE)
add_tcm_disabling_test(with_non_null_value "TCM_ENABLE=1" FALSE)

foreach(root_var "TCMROOT" "TBBROOT" "ONEAPI_ROOT" "CMPLR_ROOT")
    add_tcm_disabling_test(with_dev_env_${root_var} "${root_var}=stub" TRUE)
endforeach()


# Test no crash occurs if the app exits while still using TCM
set(TCM_TEST_PREMATURE_EXIT test_premature_exit)
add_executable(${TCM_TEST_PREMATURE_EXIT} test_premature_exit.cpp)
target_compile_options(
  ${TCM_TEST_PREMATURE_EXIT} PRIVATE ${TCM_WARNING_LEVEL} ${TCM_COMMON_COMPILE_FLAGS})
target_compile_definitions(${TCM_TEST_PREMATURE_EXIT}
  PRIVATE ${TCM_COMPILE_DEFINITIONS}
  PRIVATE TCM_LIB_NAME="$<TARGET_FILE_NAME:TCM::tcm>"
)
if (UNIX)
  target_link_options(${TCM_TEST_PREMATURE_EXIT} PRIVATE -Wl,-rpath,${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
endif (UNIX)
target_include_directories(${TCM_TEST_PREMATURE_EXIT} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(${TCM_TEST_PREMATURE_EXIT} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
add_test(NAME ${TCM_TEST_PREMATURE_EXIT} COMMAND ${TCM_TEST_PREMATURE_EXIT})
set_tests_properties(${TCM_TEST_PREMATURE_EXIT} PROPERTIES ENVIRONMENT "TCM_ENABLE=1")

if (UNIX)
  set(TCM_TEST_CGROUP_INFO test_cgroup_support)
  add_executable(${TCM_TEST_CGROUP_INFO} test_cgroup_support.cpp)
  target_include_directories(${TCM_TEST_CGROUP_INFO} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../src ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  target_link_libraries(${TCM_TEST_CGROUP_INFO} PRIVATE Threads::Threads)
  add_test(NAME ${TCM_TEST_CGROUP_INFO} COMMAND ${TCM_TEST_CGROUP_INFO})
endif (UNIX)
