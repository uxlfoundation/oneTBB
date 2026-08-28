cmake_minimum_required(VERSION 3.5.0...3.31.3)

project(nbody CXX)

include(../../common/cmake/common.cmake)

set_common_project_settings(tbb)

add_executable(nbody universe.cpp nbody_video.cpp main.cpp)

add_subdirectory(../../common/gui gui)

target_link_libraries(nbody PUBLIC TBB::tbb Threads::Threads UI_LIB_nbody)
target_compile_options(nbody PRIVATE ${TBB_CXX_STD_FLAG})

if (MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL IntelLLVM)
    target_compile_options(nbody PRIVATE /EHsc)
endif()

if (EXAMPLES_UI_MODE STREQUAL "con")
    target_compile_definitions(nbody PRIVATE _CONSOLE)
endif()

set(EXECUTABLE "$<TARGET_FILE:nbody>")
set(ARGS 1024 0)
set(PERF_ARGS 1024 1000 silent)
set(LIGHT_ARGS 256 100)

add_execution_target(run_nbody nbody ${EXECUTABLE} "${ARGS}")
add_execution_target(perf_run_nbody nbody ${EXECUTABLE} "${PERF_ARGS}")
add_execution_target(light_run_nbody nbody ${EXECUTABLE} "${LIGHT_ARGS}")
