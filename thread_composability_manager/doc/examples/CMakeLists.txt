# Copyright (c) 2026 UXL Foundation Contributors
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

cmake_minimum_required(VERSION 3.5...3.31.3)
project(tcm_doc_examples LANGUAGES CXX)

add_custom_target(tcm-doc-examples COMMENT "Build TCM documentation samples")

macro(add_tcm_doc_example _doc_example_path)
  get_filename_component(_doc_example_name "${_doc_example_path}" NAME_WE)
  add_executable(${_doc_example_name} EXCLUDE_FROM_ALL "${_doc_example_path}")

  add_dependencies(${_doc_example_name} TCM::tcm)
  target_link_libraries(${_doc_example_name} TCM::tcm)

  add_dependencies(tcm-doc-examples ${_doc_example_name})
  add_test(NAME ${_doc_example_name} COMMAND ${_doc_example_name})
  set_tests_properties(${_doc_example_name} PROPERTIES
    LABELS "tcm-doc-examples"
    ENVIRONMENT "TCM_ENABLE=1")
endmacro()

file(GLOB_RECURSE TCM_DOC_EXAMPLES_LIST "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
foreach(_doc_example_path IN LISTS TCM_DOC_EXAMPLES_LIST)
  add_tcm_doc_example(${_doc_example_path})
endforeach()
