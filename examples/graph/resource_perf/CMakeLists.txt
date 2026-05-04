# Copyright (c) 2026 UXL Foundation Contributors
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

project(resource_limited_ubenches CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

# Helper function to create a ubench variant
# Parameters:
#   target_name: Name of the executable target
#   use_mode: 0 (join_node), 1 (resource_limiter), 2 (pressure_aware_resource_limiter)
#   use_trace: 0 (no trace), 1 (eager), 2 (lazy)
#   use_timestamp: 0 or 1 (only for mode 2)
#   use_local_counter: 0 or 1 (only for mode 2)
#   use_notify: 0 or 1 (only for mode 2)
#   use_pressure: 0 or 1 (only for mode 2)
function(add_ubench_variant target_name use_mode use_trace use_timestamp use_local_counter use_notify use_pressure)
    add_executable(${target_name}
        resource_limited_ubenches.cpp)

    target_link_libraries(${target_name} TBB::tbb Threads::Threads)
    target_compile_features(${target_name} PRIVATE cxx_std_17)
    target_compile_options(${target_name} PRIVATE ${TBB_CXX_STD_FLAG})

    # Use local modified headers instead of system TBB headers
    # Must come BEFORE target_link_libraries to take precedence
    target_include_directories(${target_name} BEFORE PRIVATE
        ${CMAKE_SOURCE_DIR}/include)

    # Set USE_MODE and USE_TRACE plus enable resource limiting preview feature
    target_compile_definitions(${target_name} PRIVATE
        USE_MODE=${use_mode}
        USE_TRACE=${use_trace}
        TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING=1
        __TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING=1)

    # Set pressure-aware configuration macros (only relevant for mode 2)
    if(use_mode EQUAL 2)
        target_compile_definitions(${target_name} PRIVATE
            __TBB_USE_TIMESTAMP_IN_REQUEST_ID=${use_timestamp}
            __TBB_USE_CONSUMER_LOCAL_COUNTER_FOR_REQUEST_ID=${use_local_counter}
            __TBB_USE_NOTIFY_ON_REPORT_PRESSURE=${use_notify}
            __TBB_USE_PRESSURE=${use_pressure})
    endif()
endfunction()

# Mode 0: join_node (baseline)
add_ubench_variant(resource_ubench_mode0_notrace 0 0 0 0 0 0)
add_ubench_variant(resource_ubench_mode0_eager   0 1 0 0 0 0)
add_ubench_variant(resource_ubench_mode0_lazy    0 2 0 0 0 0)

# Mode 1: resource_limiter
add_ubench_variant(resource_ubench_mode1_notrace 1 0 0 0 0 0)
add_ubench_variant(resource_ubench_mode1_eager   1 1 0 0 0 0)
add_ubench_variant(resource_ubench_mode1_lazy    1 2 0 0 0 0)

# Mode 2: pressure_aware_resource_limiter with PRESSURE ENABLED (existing variants)
# local counter + timestamp + notify
add_ubench_variant(resource_ubench_mode2_local_ts_notify_notrace 2 0 1 1 1 1)
add_ubench_variant(resource_ubench_mode2_local_ts_notify_eager   2 1 1 1 1 1)
add_ubench_variant(resource_ubench_mode2_local_ts_notify_lazy    2 2 1 1 1 1)

# Mode 2: pressure_aware_resource_limiter with local counter + timestamp + NO notify
add_ubench_variant(resource_ubench_mode2_local_ts_nonotify_notrace 2 0 1 1 0 1)
add_ubench_variant(resource_ubench_mode2_local_ts_nonotify_eager   2 1 1 1 0 1)
add_ubench_variant(resource_ubench_mode2_local_ts_nonotify_lazy    2 2 1 1 0 1)

# Mode 2: pressure_aware_resource_limiter with local counter + NO timestamp + notify
add_ubench_variant(resource_ubench_mode2_local_notify_notrace 2 0 0 1 1 1)
add_ubench_variant(resource_ubench_mode2_local_notify_eager   2 1 0 1 1 1)
add_ubench_variant(resource_ubench_mode2_local_notify_lazy    2 2 0 1 1 1)

# Mode 2: pressure_aware_resource_limiter with local counter + NO timestamp + NO notify
add_ubench_variant(resource_ubench_mode2_local_nonotify_notrace 2 0 0 1 0 1)
add_ubench_variant(resource_ubench_mode2_local_nonotify_eager   2 1 0 1 0 1)
add_ubench_variant(resource_ubench_mode2_local_nonotify_lazy    2 2 0 1 0 1)

# Mode 2: pressure_aware_resource_limiter with global counter + notify
add_ubench_variant(resource_ubench_mode2_global_notify_notrace 2 0 0 0 1 1)
add_ubench_variant(resource_ubench_mode2_global_notify_eager   2 1 0 0 1 1)
add_ubench_variant(resource_ubench_mode2_global_notify_lazy    2 2 0 0 1 1)

# Mode 2: pressure_aware_resource_limiter with global counter + NO notify
add_ubench_variant(resource_ubench_mode2_global_nonotify_notrace 2 0 0 0 0 1)
add_ubench_variant(resource_ubench_mode2_global_nonotify_eager   2 1 0 0 0 1)
add_ubench_variant(resource_ubench_mode2_global_nonotify_lazy    2 2 0 0 0 1)

# Mode 2: pressure_aware_resource_limiter with PRESSURE DISABLED
# Note: When pressure=0, notify has no effect, so we only vary local/global and timestamp

# No pressure + local counter + timestamp
add_ubench_variant(resource_ubench_mode2_nopressure_local_ts_notrace 2 0 1 1 0 0)
add_ubench_variant(resource_ubench_mode2_nopressure_local_ts_eager   2 1 1 1 0 0)
add_ubench_variant(resource_ubench_mode2_nopressure_local_ts_lazy    2 2 1 1 0 0)

# No pressure + local counter + NO timestamp
add_ubench_variant(resource_ubench_mode2_nopressure_local_notrace 2 0 0 1 0 0)
add_ubench_variant(resource_ubench_mode2_nopressure_local_eager   2 1 0 1 0 0)
add_ubench_variant(resource_ubench_mode2_nopressure_local_lazy    2 2 0 1 0 0)

# No pressure + global counter (timestamp and notify don't apply)
add_ubench_variant(resource_ubench_mode2_nopressure_global_notrace 2 0 0 0 0 0)
add_ubench_variant(resource_ubench_mode2_nopressure_global_eager   2 1 0 0 0 0)
add_ubench_variant(resource_ubench_mode2_nopressure_global_lazy    2 2 0 0 0 0)

# Convenience targets to build all variants of a specific configuration
add_custom_target(build_all_mode0
    DEPENDS resource_ubench_mode0_notrace
            resource_ubench_mode0_eager
            resource_ubench_mode0_lazy)

add_custom_target(build_all_mode1
    DEPENDS resource_ubench_mode1_notrace
            resource_ubench_mode1_eager
            resource_ubench_mode1_lazy)

add_custom_target(build_all_mode2_local_ts_notify
    DEPENDS resource_ubench_mode2_local_ts_notify_notrace
            resource_ubench_mode2_local_ts_notify_eager
            resource_ubench_mode2_local_ts_notify_lazy)

add_custom_target(build_all_mode2_local_ts_nonotify
    DEPENDS resource_ubench_mode2_local_ts_nonotify_notrace
            resource_ubench_mode2_local_ts_nonotify_eager
            resource_ubench_mode2_local_ts_nonotify_lazy)

add_custom_target(build_all_mode2_local_notify
    DEPENDS resource_ubench_mode2_local_notify_notrace
            resource_ubench_mode2_local_notify_eager
            resource_ubench_mode2_local_notify_lazy)

add_custom_target(build_all_mode2_local_nonotify
    DEPENDS resource_ubench_mode2_local_nonotify_notrace
            resource_ubench_mode2_local_nonotify_eager
            resource_ubench_mode2_local_nonotify_lazy)

add_custom_target(build_all_mode2_global_notify
    DEPENDS resource_ubench_mode2_global_notify_notrace
            resource_ubench_mode2_global_notify_eager
            resource_ubench_mode2_global_notify_lazy)

add_custom_target(build_all_mode2_global_nonotify
    DEPENDS resource_ubench_mode2_global_nonotify_notrace
            resource_ubench_mode2_global_nonotify_eager
            resource_ubench_mode2_global_nonotify_lazy)

add_custom_target(build_all_mode2_nopressure_local_ts
    DEPENDS resource_ubench_mode2_nopressure_local_ts_notrace
            resource_ubench_mode2_nopressure_local_ts_eager
            resource_ubench_mode2_nopressure_local_ts_lazy)

add_custom_target(build_all_mode2_nopressure_local
    DEPENDS resource_ubench_mode2_nopressure_local_notrace
            resource_ubench_mode2_nopressure_local_eager
            resource_ubench_mode2_nopressure_local_lazy)

add_custom_target(build_all_mode2_nopressure_global
    DEPENDS resource_ubench_mode2_nopressure_global_notrace
            resource_ubench_mode2_nopressure_global_eager
            resource_ubench_mode2_nopressure_global_lazy)

# Build all mode2 variants
add_custom_target(build_all_mode2
    DEPENDS build_all_mode2_local_ts_notify
            build_all_mode2_local_ts_nonotify
            build_all_mode2_local_notify
            build_all_mode2_local_nonotify
            build_all_mode2_global_notify
            build_all_mode2_global_nonotify
            build_all_mode2_nopressure_local_ts
            build_all_mode2_nopressure_local
            build_all_mode2_nopressure_global)

# Build all notrace variants
add_custom_target(build_all_notrace
    DEPENDS resource_ubench_mode0_notrace
            resource_ubench_mode1_notrace
            resource_ubench_mode2_local_ts_notify_notrace
            resource_ubench_mode2_local_ts_nonotify_notrace
            resource_ubench_mode2_local_notify_notrace
            resource_ubench_mode2_local_nonotify_notrace
            resource_ubench_mode2_global_notify_notrace
            resource_ubench_mode2_global_nonotify_notrace
            resource_ubench_mode2_nopressure_local_ts_notrace
            resource_ubench_mode2_nopressure_local_notrace
            resource_ubench_mode2_nopressure_global_notrace)

# Build all eager variants
add_custom_target(build_all_eager
    DEPENDS resource_ubench_mode0_eager
            resource_ubench_mode1_eager
            resource_ubench_mode2_local_ts_notify_eager
            resource_ubench_mode2_local_ts_nonotify_eager
            resource_ubench_mode2_local_notify_eager
            resource_ubench_mode2_local_nonotify_eager
            resource_ubench_mode2_global_notify_eager
            resource_ubench_mode2_global_nonotify_eager
            resource_ubench_mode2_nopressure_local_ts_eager
            resource_ubench_mode2_nopressure_local_eager
            resource_ubench_mode2_nopressure_global_eager)

# Build all lazy variants
add_custom_target(build_all_lazy
    DEPENDS resource_ubench_mode0_lazy
            resource_ubench_mode1_lazy
            resource_ubench_mode2_local_ts_notify_lazy
            resource_ubench_mode2_local_ts_nonotify_lazy
            resource_ubench_mode2_local_notify_lazy
            resource_ubench_mode2_local_nonotify_lazy
            resource_ubench_mode2_global_notify_lazy
            resource_ubench_mode2_global_nonotify_lazy
            resource_ubench_mode2_nopressure_local_ts_lazy
            resource_ubench_mode2_nopressure_local_lazy
            resource_ubench_mode2_nopressure_global_lazy)

# Build all 33 variants (3 mode0 + 3 mode1 + 27 mode2)
add_custom_target(build_all_ubenches
    DEPENDS build_all_mode0
            build_all_mode1
            build_all_mode2)

# Main target for examples system integration (builds commonly used variant)
add_custom_target(resource_perf
    DEPENDS build_all_ubenches)

# Execution targets with default arguments (1 execution, 25 inputs, generation_rate=1.0)
# std::string benchmark = "all";
# int num_executions = 1;
# int num_inputs = 100;
# double generation_rate = 5.0;
# int num_nodes = 10;
# int num_resources = 1;
set(DEFAULT_ARGS all 1 25 5.0 10 2)
set(LIGHT_ARGS all 1 10 5.0 10 2)

# Main run target for examples system integration
add_execution_target(run_resource_perf resource_perf
                     "$<TARGET_FILE:resource_ubench_mode1_notrace>" "${DEFAULT_ARGS}")
add_execution_target(light_test_resource_perf resource_perf
                     "$<TARGET_FILE:resource_ubench_mode1_notrace>" "${LIGHT_ARGS}")

# Execution targets for all 33 variants

# Mode 0 (join_node)
add_execution_target(run_resource_ubench_mode0_notrace resource_ubench_mode0_notrace
                     "$<TARGET_FILE:resource_ubench_mode0_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode0_eager resource_ubench_mode0_eager
                     "$<TARGET_FILE:resource_ubench_mode0_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode0_lazy resource_ubench_mode0_lazy
                     "$<TARGET_FILE:resource_ubench_mode0_lazy>" "${DEFAULT_ARGS}")

# Mode 1 (resource_limiter)
add_execution_target(run_resource_ubench_mode1_notrace resource_ubench_mode1_notrace
                     "$<TARGET_FILE:resource_ubench_mode1_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode1_eager resource_ubench_mode1_eager
                     "$<TARGET_FILE:resource_ubench_mode1_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode1_lazy resource_ubench_mode1_lazy
                     "$<TARGET_FILE:resource_ubench_mode1_lazy>" "${DEFAULT_ARGS}")

# Mode 2: local counter + timestamp + notify
add_execution_target(run_resource_ubench_mode2_local_ts_notify_notrace resource_ubench_mode2_local_ts_notify_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_local_ts_notify_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_ts_notify_eager resource_ubench_mode2_local_ts_notify_eager
                     "$<TARGET_FILE:resource_ubench_mode2_local_ts_notify_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_ts_notify_lazy resource_ubench_mode2_local_ts_notify_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_local_ts_notify_lazy>" "${DEFAULT_ARGS}")

# Mode 2: local counter + timestamp + NO notify
add_execution_target(run_resource_ubench_mode2_local_ts_nonotify_notrace resource_ubench_mode2_local_ts_nonotify_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_local_ts_nonotify_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_ts_nonotify_eager resource_ubench_mode2_local_ts_nonotify_eager
                     "$<TARGET_FILE:resource_ubench_mode2_local_ts_nonotify_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_ts_nonotify_lazy resource_ubench_mode2_local_ts_nonotify_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_local_ts_nonotify_lazy>" "${DEFAULT_ARGS}")

# Mode 2: local counter + NO timestamp + notify
add_execution_target(run_resource_ubench_mode2_local_notify_notrace resource_ubench_mode2_local_notify_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_local_notify_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_notify_eager resource_ubench_mode2_local_notify_eager
                     "$<TARGET_FILE:resource_ubench_mode2_local_notify_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_notify_lazy resource_ubench_mode2_local_notify_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_local_notify_lazy>" "${DEFAULT_ARGS}")

# Mode 2: local counter + NO timestamp + NO notify
add_execution_target(run_resource_ubench_mode2_local_nonotify_notrace resource_ubench_mode2_local_nonotify_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_local_nonotify_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_nonotify_eager resource_ubench_mode2_local_nonotify_eager
                     "$<TARGET_FILE:resource_ubench_mode2_local_nonotify_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_local_nonotify_lazy resource_ubench_mode2_local_nonotify_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_local_nonotify_lazy>" "${DEFAULT_ARGS}")

# Mode 2: global counter + notify
add_execution_target(run_resource_ubench_mode2_global_notify_notrace resource_ubench_mode2_global_notify_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_global_notify_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_global_notify_eager resource_ubench_mode2_global_notify_eager
                     "$<TARGET_FILE:resource_ubench_mode2_global_notify_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_global_notify_lazy resource_ubench_mode2_global_notify_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_global_notify_lazy>" "${DEFAULT_ARGS}")

# Mode 2: global counter + NO notify
add_execution_target(run_resource_ubench_mode2_global_nonotify_notrace resource_ubench_mode2_global_nonotify_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_global_nonotify_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_global_nonotify_eager resource_ubench_mode2_global_nonotify_eager
                     "$<TARGET_FILE:resource_ubench_mode2_global_nonotify_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_global_nonotify_lazy resource_ubench_mode2_global_nonotify_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_global_nonotify_lazy>" "${DEFAULT_ARGS}")

# Mode 2: No pressure + local counter + timestamp
add_execution_target(run_resource_ubench_mode2_nopressure_local_ts_notrace resource_ubench_mode2_nopressure_local_ts_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_local_ts_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_nopressure_local_ts_eager resource_ubench_mode2_nopressure_local_ts_eager
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_local_ts_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_nopressure_local_ts_lazy resource_ubench_mode2_nopressure_local_ts_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_local_ts_lazy>" "${DEFAULT_ARGS}")

# Mode 2: No pressure + local counter + NO timestamp
add_execution_target(run_resource_ubench_mode2_nopressure_local_notrace resource_ubench_mode2_nopressure_local_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_local_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_nopressure_local_eager resource_ubench_mode2_nopressure_local_eager
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_local_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_nopressure_local_lazy resource_ubench_mode2_nopressure_local_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_local_lazy>" "${DEFAULT_ARGS}")

# Mode 2: No pressure + global counter
add_execution_target(run_resource_ubench_mode2_nopressure_global_notrace resource_ubench_mode2_nopressure_global_notrace
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_global_notrace>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_nopressure_global_eager resource_ubench_mode2_nopressure_global_eager
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_global_eager>" "${DEFAULT_ARGS}")
add_execution_target(run_resource_ubench_mode2_nopressure_global_lazy resource_ubench_mode2_nopressure_global_lazy
                     "$<TARGET_FILE:resource_ubench_mode2_nopressure_global_lazy>" "${DEFAULT_ARGS}")

# Master target to run all 33 variants
add_custom_target(run_all_ubenches
    DEPENDS run_resource_ubench_mode0_notrace
            run_resource_ubench_mode0_eager
            run_resource_ubench_mode0_lazy
            run_resource_ubench_mode1_notrace
            run_resource_ubench_mode1_eager
            run_resource_ubench_mode1_lazy
            run_resource_ubench_mode2_local_ts_notify_notrace
            run_resource_ubench_mode2_local_ts_notify_eager
            run_resource_ubench_mode2_local_ts_notify_lazy
            run_resource_ubench_mode2_local_ts_nonotify_notrace
            run_resource_ubench_mode2_local_ts_nonotify_eager
            run_resource_ubench_mode2_local_ts_nonotify_lazy
            run_resource_ubench_mode2_local_notify_notrace
            run_resource_ubench_mode2_local_notify_eager
            run_resource_ubench_mode2_local_notify_lazy
            run_resource_ubench_mode2_local_nonotify_notrace
            run_resource_ubench_mode2_local_nonotify_eager
            run_resource_ubench_mode2_local_nonotify_lazy
            run_resource_ubench_mode2_global_notify_notrace
            run_resource_ubench_mode2_global_notify_eager
            run_resource_ubench_mode2_global_notify_lazy
            run_resource_ubench_mode2_global_nonotify_notrace
            run_resource_ubench_mode2_global_nonotify_eager
            run_resource_ubench_mode2_global_nonotify_lazy
            run_resource_ubench_mode2_nopressure_local_ts_notrace
            run_resource_ubench_mode2_nopressure_local_ts_eager
            run_resource_ubench_mode2_nopressure_local_ts_lazy
            run_resource_ubench_mode2_nopressure_local_notrace
            run_resource_ubench_mode2_nopressure_local_eager
            run_resource_ubench_mode2_nopressure_local_lazy
            run_resource_ubench_mode2_nopressure_global_notrace
            run_resource_ubench_mode2_nopressure_global_eager
            run_resource_ubench_mode2_nopressure_global_lazy)

# Run only notrace variants (11 variants)
add_custom_target(run_all_notrace_ubenches
    DEPENDS run_resource_ubench_mode0_notrace
            run_resource_ubench_mode1_notrace
            run_resource_ubench_mode2_local_ts_notify_notrace
            run_resource_ubench_mode2_local_ts_nonotify_notrace
            run_resource_ubench_mode2_local_notify_notrace
            run_resource_ubench_mode2_local_nonotify_notrace
            run_resource_ubench_mode2_global_notify_notrace
            run_resource_ubench_mode2_global_nonotify_notrace
            run_resource_ubench_mode2_nopressure_local_ts_notrace
            run_resource_ubench_mode2_nopressure_local_notrace
            run_resource_ubench_mode2_nopressure_global_notrace)

# Run only eager trace variants (11 variants)
add_custom_target(run_all_eager_ubenches
    DEPENDS run_resource_ubench_mode0_eager
            run_resource_ubench_mode1_eager
            run_resource_ubench_mode2_local_ts_notify_eager
            run_resource_ubench_mode2_local_ts_nonotify_eager
            run_resource_ubench_mode2_local_notify_eager
            run_resource_ubench_mode2_local_nonotify_eager
            run_resource_ubench_mode2_global_notify_eager
            run_resource_ubench_mode2_global_nonotify_eager
            run_resource_ubench_mode2_nopressure_local_ts_eager
            run_resource_ubench_mode2_nopressure_local_eager
            run_resource_ubench_mode2_nopressure_global_eager)

# Run only lazy trace variants (11 variants)
add_custom_target(run_all_lazy_ubenches
    DEPENDS run_resource_ubench_mode0_lazy
            run_resource_ubench_mode1_lazy
            run_resource_ubench_mode2_local_ts_notify_lazy
            run_resource_ubench_mode2_local_ts_nonotify_lazy
            run_resource_ubench_mode2_local_notify_lazy
            run_resource_ubench_mode2_local_nonotify_lazy
            run_resource_ubench_mode2_global_notify_lazy
            run_resource_ubench_mode2_global_nonotify_lazy
            run_resource_ubench_mode2_nopressure_local_ts_lazy
            run_resource_ubench_mode2_nopressure_local_lazy
            run_resource_ubench_mode2_nopressure_global_lazy)
