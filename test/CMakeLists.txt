# Copyright (c) 2021-2023 Intel Corporation


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
  set(oneValueArgs NAME ENVIRONMENT)
  set(multiValueArgs DEPENDENCIES)
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

  target_link_libraries(${_tcm_test_NAME} PUBLIC TCM::tcm HWLOC::hwloc ${_tcm_test_DEPENDENCIES})
  add_test(NAME ${_tcm_test_NAME} COMMAND ${_tcm_test_NAME} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

  set_tests_properties(${_tcm_test_NAME} PROPERTIES ENVIRONMENT "${_tcm_test_ENVIRONMENT}")
endfunction()

add_tcm_test(NAME test_basic_apis)
add_tcm_test(NAME test_fcfsc ENVIRONMENT "TCM_RESOURCE_DISTRIBUTION_STRATEGY=FCFS")
add_tcm_test(NAME test_rb ENVIRONMENT "TCM_RESOURCE_DISTRIBUTION_STRATEGY=FAIR")
add_tcm_test(NAME test_constrained_requests)
add_tcm_test(NAME test_tcm_version)
add_tcm_test(NAME test_tcm_environment)

set_tests_properties(test_tcm_environment PROPERTIES
  FAIL_REGULAR_EXPRESSION "TCM: VERSION.*TCM: VERSION"
)

add_test(NAME test_tcm_environment_version_set COMMAND test_tcm_environment
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(test_tcm_environment_version_set PROPERTIES
  ENVIRONMENT "TCM_VERSION=1"
  PASS_REGULAR_EXPRESSION "TCM: VERSION.*TCM: VERSION;TCM: TCM_DISABLE *0"
  FAIL_REGULAR_EXPRESSION "ERROR"
)

add_test(NAME test_tcm_environment_disable COMMAND test_tcm_environment
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(test_tcm_environment_disable PROPERTIES
  ENVIRONMENT "TCM_DISABLE=1"
  PASS_REGULAR_EXPRESSION  "TCM: TCM_DISABLE *1"
  FAIL_REGULAR_EXPRESSION "ERROR;TCM: TCM_OVERSUBSCRIPTION_FACTOR"
)

add_test(NAME test_tcm_environment_disable_version_set COMMAND test_tcm_environment
         WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(test_tcm_environment_disable_version_set PROPERTIES
  ENVIRONMENT "TCM_VERSION=1;TCM_DISABLE=1"
  PASS_REGULAR_EXPRESSION  "TCM: VERSION.*TCM: VERSION;TCM: TCM_DISABLE *1;TCM: TCM_OVERSUBSCRIPTION_FACTOR"
  FAIL_REGULAR_EXPRESSION "ERROR"
)

# Group of tests that runs the same test in different environment checking TCM
# reacts accordingly
set(TCM_TEST_DISABLING test_tcm_disabling)
add_executable(${TCM_TEST_DISABLING} test_tcm_disabling.cpp)
target_compile_options(${TCM_TEST_DISABLING} PRIVATE ${TCM_WARNING_LEVEL})
target_compile_definitions(${TCM_TEST_DISABLING} PRIVATE ${TCM_COMPILE_DEFINITIONS})
target_link_libraries(${TCM_TEST_DISABLING} PUBLIC TCM::tcm HWLOC::hwloc)

add_test(NAME ${TCM_TEST_DISABLING}_with_empty_env COMMAND ${TCM_TEST_DISABLING} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(${TCM_TEST_DISABLING}_with_empty_env PROPERTIES ENVIRONMENT "TCM_DISABLE=")

add_test(NAME ${TCM_TEST_DISABLING}_with_non_null_env COMMAND ${TCM_TEST_DISABLING} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(${TCM_TEST_DISABLING}_with_non_null_env PROPERTIES ENVIRONMENT "TCM_DISABLE=1")

add_test(NAME ${TCM_TEST_DISABLING}_with_null_env COMMAND ${TCM_TEST_DISABLING} WORKING_DIRECTORY ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
set_tests_properties(${TCM_TEST_DISABLING}_with_null_env PROPERTIES ENVIRONMENT "TCM_DISABLE=0")


# Test no crash occurs if the app exits while still using TCM
set(TCM_TEST_PREMATURE_EXIT test_premature_exit)
add_executable(${TCM_TEST_PREMATURE_EXIT} test_premature_exit.cpp)
target_compile_options(${TCM_TEST_PREMATURE_EXIT} PRIVATE ${TCM_WARNING_LEVEL})
target_compile_definitions(${TCM_TEST_PREMATURE_EXIT}
  PRIVATE ${TCM_COMPILE_DEFINITIONS}
  PRIVATE TCM_LIB_PATH="$<TARGET_FILE:TCM::tcm>"
)
target_include_directories(${TCM_TEST_PREMATURE_EXIT} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries(${TCM_TEST_PREMATURE_EXIT} PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
add_test(NAME ${TCM_TEST_PREMATURE_EXIT} COMMAND ${TCM_TEST_PREMATURE_EXIT})
