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

set(sycl_sample_name tbb-resumable-tasks-sycl)
project(${sycl_sample_name} CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

add_executable(${sycl_sample_name} ${sycl_sample_name}.cpp)
set_target_properties(${sycl_sample_name} PROPERTIES CXX_STANDARD 17)

get_target_property(is_tbb_imported TBB::tbb IMPORTED)
if (is_tbb_imported)
    get_target_property(tbb_imported_configurations TBB::tbb IMPORTED_CONFIGURATIONS)
    if (NOT tbb_imported_configurations MATCHES "RELEASE")
        set(should_issue_warning TRUE)
        target_link_libraries(${sycl_sample_name} PRIVATE TBB::tbb)
    else()
        target_link_libraries(${sycl_sample_name} PRIVATE "$<TARGET_PROPERTY:TBB::tbb,IMPORTED_LOCATION_RELEASE>")
    endif()
else()
    if (CMAKE_BUILD_TYPE MATCHES "debug")
        set(should_issue_warning TRUE)
    endif()
    target_link_libraries(${sycl_sample_name} PRIVATE TBB::tbb)
endif()


if (should_issue_warning AND NOT TBB_SYCL_SAMPLES_WARNING_SHOWN)
    message( WARNING 
        "Using Debug version of TBB in SYCL application may cause crashes due to conflicts \
        between Release and Debug version of TBB within the same application."
    )
    set(TBB_SYCL_SAMPLES_WARNING_SHOWN TRUE CACHE BOOL "")
endif()

target_compile_options(${sycl_sample_name} PRIVATE -fsycl)
target_link_options(${sycl_sample_name} PRIVATE -fsycl)

add_execution_target(run_${sycl_sample_name} ${sycl_sample_name} "$<TARGET_FILE:${sycl_sample_name}>" "")
add_execution_target(light_test_${sycl_sample_name} ${sycl_sample_name} "$<TARGET_FILE:${sycl_sample_name}>" "")

unset(sycl_sample_name)
unset(should_issue_warning)
