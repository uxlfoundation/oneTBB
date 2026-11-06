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

# Performance benchmarks, built only with -DTBB_TEST_PERF=ON (see test/CMakeLists.txt).
#
# workflow_runner is built once per variant, all from the same source:
#   workflow_runner_jn_impl            join-based implementation, queueing body
#   workflow_runner_jn_impl_rejecting  join-based implementation, rejecting body
#   workflow_runner_limiter_impl       resource_limiter / resource_limited_node
# workflow_runner_all builds all three. Each variant is compiled against the oneTBB
# headers and library of this build (or of the external package, with
# TBB_FIND_PACKAGE), so limiter_impl measures whichever implementation that is.

set(TBB_TEST_PERF_TREE "" CACHE STRING
    "Label recorded as the oneTBB tree in perf benchmark output (default: git branch@sha of this source tree)")
option(TBB_TEST_PERF_TRACE "Compile tracing support into the perf benchmarks (USE_TRACE)" ON)

# The tree label defaults to the branch and commit of this source tree, read at
# configure time. With TBB_FIND_PACKAGE that describes this tree rather than the
# external oneTBB being measured, so pass TBB_TEST_PERF_TREE explicitly there.
set(_tbb_perf_tree "${TBB_TEST_PERF_TREE}")
if (NOT _tbb_perf_tree)
    set(_tbb_perf_tree "unknown")
    find_package(Git QUIET)
    if (GIT_FOUND)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --abbrev-ref HEAD
                        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                        RESULT_VARIABLE _tbb_perf_git_result
                        OUTPUT_VARIABLE _tbb_perf_git_branch
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
                        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                        OUTPUT_VARIABLE _tbb_perf_git_sha
                        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        # Exit status 1 means tracked files differ from HEAD.
        execute_process(COMMAND ${GIT_EXECUTABLE} diff-index --quiet HEAD --
                        WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
                        RESULT_VARIABLE _tbb_perf_git_dirty
                        OUTPUT_QUIET ERROR_QUIET)
        if (_tbb_perf_git_result EQUAL 0 AND _tbb_perf_git_branch AND _tbb_perf_git_sha)
            set(_tbb_perf_tree "${_tbb_perf_git_branch}@${_tbb_perf_git_sha}")
            if (_tbb_perf_git_dirty EQUAL 1)
                string(APPEND _tbb_perf_tree "-dirty")
            endif()
        endif()
    endif()
endif()
message(STATUS "Perf benchmarks: tree label '${_tbb_perf_tree}'")

set(_tbb_perf_workflow_dir ${CMAKE_CURRENT_SOURCE_DIR}/resource_limiter/workflow_runner)

# tbb_add_workflow_runner(VARIANT <name> LIMITER <0|1> [BODY_POLICY <queueing|rejecting>])
#
# Adds the executable workflow_runner_<name>. LIMITER is USE_LIMITER_NODE: 0 builds
# jn_impl, 1 builds limiter_impl. BODY_POLICY sets JN_BODY_POLICY for jn_impl and
# defaults to the header's own default (queueing).
function(tbb_add_workflow_runner)
    cmake_parse_arguments(_wr "" "VARIANT;LIMITER;BODY_POLICY" "" ${ARGN})
    set(_target workflow_runner_${_wr_VARIANT})

    add_executable(${_target} ${_tbb_perf_workflow_dir}/workflow_runner.cpp)
    target_include_directories(${_target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/common)

    # The harness needs C++17. A compile feature raises the target to at least that
    # without overriding a newer CMAKE_CXX_STANDARD.
    target_compile_features(${_target} PRIVATE cxx_std_17)

    target_compile_options(${_target} PRIVATE
        ${TBB_WARNING_LEVEL}
        ${TBB_WARNING_SUPPRESS}
        ${TBB_TEST_WARNING_FLAGS}
        ${TBB_TEST_COMPILE_FLAGS}
        ${TBB_COMMON_COMPILE_FLAGS}
    )

    target_compile_definitions(${_target} PRIVATE
        USE_LIMITER_NODE=${_wr_LIMITER}
        USE_TRACE=$<BOOL:${TBB_TEST_PERF_TRACE}>
        VARIANT_TREE="${_tbb_perf_tree}"
        BENCH_COMPILER="${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
        $<$<CONFIG:DEBUG>:TBB_USE_DEBUG>
    )
    if (_wr_LIMITER)
        target_compile_definitions(${_target} PRIVATE TBB_PREVIEW_FLOW_GRAPH_RESOURCE_LIMITING=1)
    endif()
    if (_wr_BODY_POLICY)
        target_compile_definitions(${_target} PRIVATE JN_BODY_POLICY=${_wr_BODY_POLICY})
    endif()
    if (WIN32)
        # The harness reads its knobs with std::getenv, which the MSVC CRT deprecates.
        target_compile_definitions(${_target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()

    if (COMMAND target_link_options)
        target_link_options(${_target} PRIVATE ${TBB_COMMON_LINK_FLAGS} ${TBB_TEST_LINK_FLAGS})
    else()
        target_link_libraries(${_target} PRIVATE ${TBB_COMMON_LINK_FLAGS} ${TBB_TEST_LINK_FLAGS})
    endif()
    target_link_libraries(${_target} PRIVATE TBB::tbb Threads::Threads ${TBB_COMMON_LINK_LIBS})

    add_dependencies(workflow_runner_all ${_target})
endfunction()

# tbb_add_workflow_runner_test(NAME <test> VARIANT <name> INPUT <json> [PASS_REGEX <regex>])
#
# A short ctest run of workflow_runner_<variant> on a synthetic workflow, labelled
# "perf" (run with `ctest -L perf`). The budget is chosen so that the admission
# window binds (C = 4 messages for synthetic_joins.json) and the run takes about a
# second. The harness reports its conservation checks as `Error:` on stderr without
# a failing exit code, so those lines fail the test too. With PASS_REGEX the test
# instead passes exactly when the output matches it, whatever the exit code.
function(tbb_add_workflow_runner_test)
    cmake_parse_arguments(_wrt "" "NAME;VARIANT;INPUT;PASS_REGEX" "" ${ARGN})
    add_test(NAME ${_wrt_NAME}
             COMMAND workflow_runner_${_wrt_VARIANT}
                     --json ${_tbb_perf_workflow_dir}/test_inputs/${_wrt_INPUT}
                     --num_inputs 200 --num_threads 4 --gb_per_thread 0.01
             WORKING_DIRECTORY ${TBB_TEST_WORKING_DIRECTORY})
    set_tests_properties(${_wrt_NAME} PROPERTIES LABELS perf RUN_SERIAL TRUE TIMEOUT 120)
    if (_wrt_PASS_REGEX)
        set_tests_properties(${_wrt_NAME} PROPERTIES PASS_REGULAR_EXPRESSION "${_wrt_PASS_REGEX}")
    else()
        set_tests_properties(${_wrt_NAME} PROPERTIES FAIL_REGULAR_EXPRESSION "Error:;FATAL")
    endif()
    set_property(TEST ${_wrt_NAME} PROPERTY ENVIRONMENT ${TBB_TESTS_ENVIRONMENT} APPEND)
endfunction()

add_custom_target(workflow_runner_all)

tbb_add_workflow_runner(VARIANT jn_impl           LIMITER 0)
tbb_add_workflow_runner(VARIANT jn_impl_rejecting LIMITER 0 BODY_POLICY rejecting)
tbb_add_workflow_runner(VARIANT limiter_impl      LIMITER 1)

tbb_add_workflow_runner_test(NAME perf_workflow_runner_jn_impl
                             VARIANT jn_impl INPUT synthetic_joins.json)
tbb_add_workflow_runner_test(NAME perf_workflow_runner_jn_impl_rejecting
                             VARIANT jn_impl_rejecting INPUT synthetic_joins.json)
tbb_add_workflow_runner_test(NAME perf_workflow_runner_limiter_impl
                             VARIANT limiter_impl INPUT synthetic_joins.json)

# A rejecting build must refuse a workflow in which no algorithm declares a resource:
# the body policy would have nothing to apply to (see validate_workflow_wiring).
tbb_add_workflow_runner_test(NAME perf_workflow_runner_jn_impl_rejecting_refuses_inert
                             VARIANT jn_impl_rejecting INPUT synthetic_no_resources.json
                             PASS_REGEX "no algorithm in this workflow declares a resource")
