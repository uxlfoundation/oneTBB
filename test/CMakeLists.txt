# Copyright (c) 2021-2022 Intel Corporation

foreach(tpp test_basic_apis.cpp test_fcfsc.cpp test_rb.cpp)
  string(REPLACE ".cpp" "" texe ${tpp})
  add_executable(${texe} ${tpp})
  target_link_libraries(${texe} PUBLIC tcm)
  add_test(${texe} ${texe})
endforeach()
