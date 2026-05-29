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

project(resource_limited_benchmarks CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

# Helper function to create a benchmark variant
# Parameters:
#   target_name: Name of the executable target
#   source_file: Source file for this benchmark (e.g., genie_bench.cpp)
#   use_mode: 0 (join_node), 1 (resource_limiter), 2 (priority_aware_resource_limiter)
#   use_trace: 0 (no trace), 1 (eager), 2 (lazy)
#   use_timestamp: 0 or 1 (only for mode 2)
#   use_local_counter: 0 or 1 (only for mode 2)
#   use_notify: 0 or 1 (only for mode 2)
#   use_pressure: 0 or 1 (only for mode 2)
function(add_benchmark_variant target_name source_file use_mode use_trace use_timestamp use_local_counter use_notify use_pressure)
    add_executable(${target_name} ${source_file})

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

# Helper function to add all 33 variants for a single benchmark
# Parameters:
#   benchmark_name: Name prefix for targets (e.g., "genie_bench")
#   source_file: Source file for this benchmark (e.g., "genie_bench.cpp")
function(add_benchmark_variants benchmark_name source_file)
    # Mode 0: join_node (baseline) - 3 variants
    add_benchmark_variant(${benchmark_name}_mode0_notrace ${source_file} 0 0 0 0 0 0)
    add_benchmark_variant(${benchmark_name}_mode0_eager   ${source_file} 0 1 0 0 0 0)
    add_benchmark_variant(${benchmark_name}_mode0_lazy    ${source_file} 0 2 0 0 0 0)

    # Mode 1: resource_limiter - 3 variants
    add_benchmark_variant(${benchmark_name}_mode1_notrace ${source_file} 1 0 0 0 0 0)
    add_benchmark_variant(${benchmark_name}_mode1_eager   ${source_file} 1 1 0 0 0 0)
    add_benchmark_variant(${benchmark_name}_mode1_lazy    ${source_file} 1 2 0 0 0 0)

    # Mode 2: priority_aware_resource_limiter with PRESSURE ENABLED - 18 variants
    # local counter + timestamp + notify
    add_benchmark_variant(${benchmark_name}_mode2_local_ts_notify_notrace ${source_file} 2 0 1 1 1 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_ts_notify_eager   ${source_file} 2 1 1 1 1 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_ts_notify_lazy    ${source_file} 2 2 1 1 1 1)

    # local counter + timestamp + NO notify
    add_benchmark_variant(${benchmark_name}_mode2_local_ts_nonotify_notrace ${source_file} 2 0 1 1 0 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_ts_nonotify_eager   ${source_file} 2 1 1 1 0 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_ts_nonotify_lazy    ${source_file} 2 2 1 1 0 1)

    # local counter + NO timestamp + notify
    add_benchmark_variant(${benchmark_name}_mode2_local_notify_notrace ${source_file} 2 0 0 1 1 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_notify_eager   ${source_file} 2 1 0 1 1 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_notify_lazy    ${source_file} 2 2 0 1 1 1)

    # local counter + NO timestamp + NO notify
    add_benchmark_variant(${benchmark_name}_mode2_local_nonotify_notrace ${source_file} 2 0 0 1 0 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_nonotify_eager   ${source_file} 2 1 0 1 0 1)
    add_benchmark_variant(${benchmark_name}_mode2_local_nonotify_lazy    ${source_file} 2 2 0 1 0 1)

    # global counter + notify
    add_benchmark_variant(${benchmark_name}_mode2_global_notify_notrace ${source_file} 2 0 0 0 1 1)
    add_benchmark_variant(${benchmark_name}_mode2_global_notify_eager   ${source_file} 2 1 0 0 1 1)
    add_benchmark_variant(${benchmark_name}_mode2_global_notify_lazy    ${source_file} 2 2 0 0 1 1)

    # global counter + NO notify
    add_benchmark_variant(${benchmark_name}_mode2_global_nonotify_notrace ${source_file} 2 0 0 0 0 1)
    add_benchmark_variant(${benchmark_name}_mode2_global_nonotify_eager   ${source_file} 2 1 0 0 0 1)
    add_benchmark_variant(${benchmark_name}_mode2_global_nonotify_lazy    ${source_file} 2 2 0 0 0 1)

    # Mode 2: priority_aware_resource_limiter with PRESSURE DISABLED - 9 variants
    # No pressure + local counter + timestamp
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_local_ts_notrace ${source_file} 2 0 1 1 0 0)
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_local_ts_eager   ${source_file} 2 1 1 1 0 0)
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_local_ts_lazy    ${source_file} 2 2 1 1 0 0)

    # No pressure + local counter + NO timestamp
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_local_notrace ${source_file} 2 0 0 1 0 0)
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_local_eager   ${source_file} 2 1 0 1 0 0)
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_local_lazy    ${source_file} 2 2 0 1 0 0)

    # No pressure + global counter
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_global_notrace ${source_file} 2 0 0 0 0 0)
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_global_eager   ${source_file} 2 1 0 0 0 0)
    add_benchmark_variant(${benchmark_name}_mode2_nopressure_global_lazy    ${source_file} 2 2 0 0 0 0)
endfunction()

# ============================================================================
# Define all benchmark targets (7 benchmarks × 33 variants = 231 targets)
# ============================================================================

add_benchmark_variants(genie_bench genie_bench.cpp)
add_benchmark_variants(genie_diamond_bench genie_diamond_bench.cpp)
add_benchmark_variants(baseline_cycle_bench baseline_cycle_bench.cpp)
add_benchmark_variants(chain_bench chain_bench.cpp)
add_benchmark_variants(siblings_bench siblings_bench.cpp)
add_benchmark_variants(tree_bench tree_bench.cpp)
add_benchmark_variants(dining_philosophers_bench dining_philosophers_bench.cpp)

# ============================================================================
# Convenience build targets
# ============================================================================

# Per-benchmark targets (builds all 33 variants of one benchmark)
set(ALL_GENIE_BENCH_TARGETS "")
set(ALL_GENIE_DIAMOND_BENCH_TARGETS "")
set(ALL_BASELINE_CYCLE_BENCH_TARGETS "")
set(ALL_CHAIN_BENCH_TARGETS "")
set(ALL_SIBLINGS_BENCH_TARGETS "")
set(ALL_TREE_BENCH_TARGETS "")
set(ALL_DINING_PHILOSOPHERS_BENCH_TARGETS "")

foreach(benchmark IN ITEMS genie_bench genie_diamond_bench baseline_cycle_bench chain_bench siblings_bench tree_bench dining_philosophers_bench)
    foreach(mode IN ITEMS mode0 mode1)
        foreach(trace IN ITEMS notrace eager lazy)
            list(APPEND ALL_${benchmark}_TARGETS ${benchmark}_${mode}_${trace})
        endforeach()
    endforeach()

    # Mode 2 variants
    foreach(config IN ITEMS local_ts_notify local_ts_nonotify local_notify local_nonotify global_notify global_nonotify nopressure_local_ts nopressure_local nopressure_global)
        foreach(trace IN ITEMS notrace eager lazy)
            list(APPEND ALL_${benchmark}_TARGETS ${benchmark}_mode2_${config}_${trace})
        endforeach()
    endforeach()

    string(TOUPPER ${benchmark} benchmark_upper)
    add_custom_target(build_all_${benchmark}
        DEPENDS ${ALL_${benchmark_upper}_TARGETS})
endforeach()

# Per-mode targets (builds all benchmarks in one mode)
set(ALL_MODE0_TARGETS "")
set(ALL_MODE1_TARGETS "")
set(ALL_MODE2_TARGETS "")

foreach(benchmark IN ITEMS genie_bench genie_diamond_bench baseline_cycle_bench chain_bench siblings_bench tree_bench dining_philosophers_bench)
    foreach(trace IN ITEMS notrace eager lazy)
        list(APPEND ALL_MODE0_TARGETS ${benchmark}_mode0_${trace})
        list(APPEND ALL_MODE1_TARGETS ${benchmark}_mode1_${trace})
    endforeach()

    foreach(config IN ITEMS local_ts_notify local_ts_nonotify local_notify local_nonotify global_notify global_nonotify nopressure_local_ts nopressure_local nopressure_global)
        foreach(trace IN ITEMS notrace eager lazy)
            list(APPEND ALL_MODE2_TARGETS ${benchmark}_mode2_${config}_${trace})
        endforeach()
    endforeach()
endforeach()

add_custom_target(build_all_mode0 DEPENDS ${ALL_MODE0_TARGETS})
add_custom_target(build_all_mode1 DEPENDS ${ALL_MODE1_TARGETS})
add_custom_target(build_all_mode2 DEPENDS ${ALL_MODE2_TARGETS})

# Per-Mode-2-configuration targets
foreach(config IN ITEMS local_ts_notify local_ts_nonotify local_notify local_nonotify global_notify global_nonotify nopressure_local_ts nopressure_local nopressure_global)
    set(CONFIG_TARGETS "")
    foreach(benchmark IN ITEMS genie_bench genie_diamond_bench baseline_cycle_bench chain_bench siblings_bench tree_bench dining_philosophers_bench)
        foreach(trace IN ITEMS notrace eager lazy)
            list(APPEND CONFIG_TARGETS ${benchmark}_mode2_${config}_${trace})
        endforeach()
    endforeach()
    add_custom_target(build_all_mode2_${config} DEPENDS ${CONFIG_TARGETS})
endforeach()

# Per-trace-mode targets (builds all benchmarks, all modes, one trace mode)
set(ALL_NOTRACE_TARGETS "")
set(ALL_EAGER_TARGETS "")
set(ALL_LAZY_TARGETS "")

foreach(benchmark IN ITEMS genie_bench genie_diamond_bench baseline_cycle_bench chain_bench siblings_bench tree_bench dining_philosophers_bench)
    list(APPEND ALL_NOTRACE_TARGETS ${benchmark}_mode0_notrace ${benchmark}_mode1_notrace)
    list(APPEND ALL_EAGER_TARGETS ${benchmark}_mode0_eager ${benchmark}_mode1_eager)
    list(APPEND ALL_LAZY_TARGETS ${benchmark}_mode0_lazy ${benchmark}_mode1_lazy)

    foreach(config IN ITEMS local_ts_notify local_ts_nonotify local_notify local_nonotify global_notify global_nonotify nopressure_local_ts nopressure_local nopressure_global)
        list(APPEND ALL_NOTRACE_TARGETS ${benchmark}_mode2_${config}_notrace)
        list(APPEND ALL_EAGER_TARGETS ${benchmark}_mode2_${config}_eager)
        list(APPEND ALL_LAZY_TARGETS ${benchmark}_mode2_${config}_lazy)
    endforeach()
endforeach()

add_custom_target(build_all_notrace DEPENDS ${ALL_NOTRACE_TARGETS})
add_custom_target(build_all_eager DEPENDS ${ALL_EAGER_TARGETS})
add_custom_target(build_all_lazy DEPENDS ${ALL_LAZY_TARGETS})

# Top-level targets
add_custom_target(build_all_benchmarks
    DEPENDS build_all_mode0 build_all_mode1 build_all_mode2)

# Main target for examples system integration
add_custom_target(resource_perf DEPENDS build_all_benchmarks)
