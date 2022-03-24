# Copyright (c) 2021-2022 Intel Corporation

foreach(tpp test_basic_apis.cpp test_fcfsc.cpp test_rb.cpp)
  string(REPLACE ".cpp" "" texe ${tpp})
  add_executable(${texe} ${tpp})
  target_link_libraries(${texe} PUBLIC tcm)
  add_test(${texe} ${texe})
endforeach()

# Group of tests that runs the same test in different environment checking TCM
# reacts accordingly
add_executable(test_tcm_disabling test_tcm_disabling.cpp)
target_link_libraries(test_tcm_disabling PUBLIC tcm)

add_test(test_tcm_disabling_with_empty_env test_tcm_disabling)
set_tests_properties(test_tcm_disabling_with_empty_env PROPERTIES ENVIRONMENT "TCM_DISABLE=")

add_test(test_tcm_disabling_with_non_null_env test_tcm_disabling)
set_tests_properties(test_tcm_disabling_with_non_null_env PROPERTIES ENVIRONMENT "TCM_DISABLE=1")

add_test(test_tcm_disabling_with_null_env test_tcm_disabling)
set_tests_properties(test_tcm_disabling_with_null_env PROPERTIES ENVIRONMENT "TCM_DISABLE=0")
