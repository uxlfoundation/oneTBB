# Copyright (c) 2020-2025 Intel Corporation
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

cmake_minimum_required(VERSION 3.5.0...3.31.3)

project(seismic CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

add_executable(seismic universe.cpp seismic_video.cpp main.cpp)

add_subdirectory(../../common/gui gui)

target_link_libraries(seismic PUBLIC TBB::tbb Threads::Threads UI_LIB_seismic)
target_compile_options(seismic PRIVATE ${TBB_CXX_STD_FLAG})
if (MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL IntelLLVM)
    target_compile_options(seismic PRIVATE /EHsc)
endif()

if (EXAMPLES_UI_MODE STREQUAL "con")
    target_compile_definitions(seismic PRIVATE _CONSOLE)
endif()

set(EXECUTABLE "$<TARGET_FILE:seismic>")
set(ARGS auto 0)
set(PERF_ARGS auto 10000 silent)
set(LIGHT_ARGS 1:2 100)

add_execution_target(run_seismic seismic ${EXECUTABLE} "${ARGS}")
add_execution_target(perf_run_seismic seismic ${EXECUTABLE} "${PERF_ARGS}")
add_execution_target(light_test_seismic seismic ${EXECUTABLE} "${LIGHT_ARGS}")
