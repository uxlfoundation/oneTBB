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

project(fib_cutoff CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

add_custom_target(fib_cutoff)
add_dependencies(fib_cutoff fib_cutoff_task_emu)

add_executable(fib_cutoff_task_emu time_fibonacci_cutoff.cpp fibonacci_impl_emu.cpp)
target_link_libraries(fib_cutoff_task_emu
                      TBB::tbb
                      Threads::Threads
                      $<$<PLATFORM_ID:Linux>:rt>)  # Link "rt" library on Linux
target_compile_options(fib_cutoff_task_emu PRIVATE ${TBB_CXX_STD_FLAG})

set(ARGS 2 30 1) # 30-th number on 2 threads, with 1 repetition measured
set(PERF_ARGS 8 42 5)

set(EXECUTABLE "$<TARGET_FILE:fib_cutoff_task_emu>")
#add_execution_target(run_fib_cutoff fib_cutoff_task_emu ${EXECUTABLE} "${ARGS}")
#add_execution_target(perf_run_fib_cutoff fib_cutoff_task_emu ${EXECUTABLE} "${PERF_ARGS}")
