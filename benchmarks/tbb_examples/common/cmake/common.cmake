# Copyright (C) 2020-2023 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

macro(set_common_project_settings required_components)
    # Path to common headers
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../)

    if (NOT TARGET TBB::tbb)
        list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../../common/cmake/modules)
        find_package(TBB REQUIRED COMPONENTS ${required_components})
    endif()
    find_package(Threads REQUIRED)

    # ---------------------------------------------------------------------------------------------------------
    # Handle C++ standard version.
    if (NOT MSVC)  # no need to cover MSVC as it uses C++14 by default.
        if (NOT CMAKE_CXX_STANDARD)
            set(CMAKE_CXX_STANDARD 11)
        endif()
        if (CMAKE_CXX${CMAKE_CXX_STANDARD}_STANDARD_COMPILE_OPTION)  # if standard option was detected by CMake
            set(CMAKE_CXX_STANDARD_REQUIRED ON)
        endif()
    endif()

    set(CMAKE_CXX_EXTENSIONS OFF) # use -std=c++... instead of -std=gnu++...
    # ---------------------------------------------------------------------------------------------------------
endmacro()

macro(add_execution_target TARGET_NAME TARGET_DEPENDENCIES EXECUTABLE ARGS)
    if (WIN32)
        add_custom_target(${TARGET_NAME} set "PATH=$<TARGET_FILE_DIR:TBB::tbb>\\;$ENV{PATH}" & ${EXECUTABLE} ${ARGS})
    else()
        add_custom_target(${TARGET_NAME} ${EXECUTABLE} ${ARGS})
    endif()

    add_dependencies(${TARGET_NAME} ${TARGET_DEPENDENCIES})
endmacro()
