# Copyright (c) 2025 Intel Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

cmake_minimum_required(VERSION 3.11)
project(doc_examples_testing)

set(_tbb_include_path "${CMAKE_CURRENT_SOURCE_DIR}/../../../include")
set(_reference_examples_path "${CMAKE_CURRENT_SOURCE_DIR}/../reference/examples")
set(_userguide_examples_path "${CMAKE_CURRENT_SOURCE_DIR}/../tbb_userguide/examples")

add_custom_target(build-doc-examples
    COMMENT "Build oneTBB documentation samples")

macro(add_example example_path)
    message(STATUS "Adding ${_example_path}")

    get_filename_component(_example_name "${_example_path}" NAME_WE)
    add_executable(${_example_name} EXCLUDE_FROM_ALL "${_example_path}")

    target_include_directories(${_example_name} PRIVATE ${_tbb_include_path})
    target_link_libraries(${_example_name} tbb)
    target_link_libraries(${_example_name} tbbmalloc)

    add_dependencies(build-doc-examples ${_example_name})
    add_test(NAME ${_example_name} COMMAND ${_example_name})
endmacro()

file(GLOB_RECURSE REFERENCE_EXAMPLES_LIST "${_reference_examples_path}/*.cpp")
file(GLOB_RECURSE USERGUIDE_EXAMPLES_LIST "${_userguide_examples_path}/*.cpp")

foreach(_example_path IN LISTS REFERENCE_EXAMPLES_LIST)
    add_example(${_example_path})
endforeach()

foreach(_example_path IN LISTS USERGUIDE_EXAMPLES_LIST)
    add_example(${_example_path})
endforeach()

enable_testing()
