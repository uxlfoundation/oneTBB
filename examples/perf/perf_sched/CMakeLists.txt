# Copyright (c) 2023-2025 Intel Corporation
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

project(perf_sched CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

add_executable(perf_sched perf_sched.cpp perf.cpp)
target_link_libraries(perf_sched
                      TBB::tbb
                      Threads::Threads
                      $<$<PLATFORM_ID:Linux>:rt>)  # Link "rt" library on Linux
target_compile_options(perf_sched PRIVATE ${TBB_CXX_STD_FLAG})

set(EXECUTABLE "$<TARGET_FILE:perf_sched>")

#set(ARGS 2) # only run on 2 threads
#set(PERF_ARGS 1:8)

#add_execution_target(run_perf_sched perf_sched ${EXECUTABLE} "${ARGS}")
#add_execution_target(perf_run_perf_sched perf_sched ${EXECUTABLE} "${PERF_ARGS}")
