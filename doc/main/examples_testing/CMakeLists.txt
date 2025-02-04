cmake_minimum_required(VERSION 3.11)
project(examples_testsing)

set(_tbb_include_path "${CMAKE_CURRENT_SOURCE_DIR}/../../include")
set(_reference_examples_path "${CMAKE_CURRENT_SOURCE_DIR}/../reference/examples")
set(_userguide_examples_path "${CMAKE_CURRENT_SOURCE_DIR}/../tbb_userguide/examples")

add_custom_target(build-examples
    COMMENT "Build oneTBB documentation samples")

macro(add_example example_path)
    message(STATUS "Adding ${_example_path}")

    get_filename_component(_example_name "${_example_path}" NAME_WE)
    add_executable(${_example_name} EXCLUDE_FROM_ALL "${_example_path}")

    target_include_directories(${_example_name} PRIVATE ${_tbb_include_path})
    target_link_libraries(${_example_name} tbb_debug)
    target_link_libraries(${_example_name} tbbmalloc)

    add_dependencies(build-examples ${_example_name})
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
