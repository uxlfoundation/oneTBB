# Copyright (c) 2021-2022 Intel Corporation


if (NOT MSVC) # no need to change options for MSVC
    # suppress extra warnings:
    # - suggest braces around initialization of subobject
    # - missing field initializer
    set(TCM_WARNING_LEVEL ${TCM_WARNING_LEVEL} -Wno-missing-braces)
endif()

foreach(tpp test_basic_apis.cpp test_fcfsc.cpp test_rb.cpp test_constrained_requests.cpp)
  string(REPLACE ".cpp" "" texe ${tpp})
  add_executable(${texe} ${tpp})
  target_compile_options(${texe}
    PRIVATE
    ${TCM_WARNING_LEVEL}
    ${TCM_TEST_WARNING_FLAGS}
    ${TCM_WARNING_SUPPRESS}
    ${TCM_MMD_FLAG}
    ${TCM_DSE_FLAG}
    ${TCM_COMMON_COMPILE_FLAGS}
  )
  target_compile_definitions(${texe} PRIVATE ${TCM_COMPILE_DEFINITIONS})

  target_link_libraries(${texe} PUBLIC tcm)
  add_test(${texe} ${texe})
endforeach()

# Group of tests that runs the same test in different environment checking TCM
# reacts accordingly
set(TCM_TEST_DISABLING test_tcm_disabling)
add_executable(${TCM_TEST_DISABLING} test_tcm_disabling.cpp)
target_compile_options(${TCM_TEST_DISABLING} PRIVATE ${TCM_WARNING_LEVEL})
target_compile_definitions(${TCM_TEST_DISABLING} PRIVATE ${TCM_COMPILE_DEFINITIONS})
target_link_libraries(${TCM_TEST_DISABLING} PUBLIC tcm)

add_test(${TCM_TEST_DISABLING}_with_empty_env ${TCM_TEST_DISABLING})
set_tests_properties(${TCM_TEST_DISABLING}_with_empty_env PROPERTIES ENVIRONMENT "TCM_DISABLE=")

add_test(${TCM_TEST_DISABLING}_with_non_null_env ${TCM_TEST_DISABLING})
set_tests_properties(${TCM_TEST_DISABLING}_with_non_null_env PROPERTIES ENVIRONMENT "TCM_DISABLE=1")

add_test(${TCM_TEST_DISABLING}_with_null_env ${TCM_TEST_DISABLING})
set_tests_properties(${TCM_TEST_DISABLING}_with_null_env PROPERTIES ENVIRONMENT "TCM_DISABLE=0")
