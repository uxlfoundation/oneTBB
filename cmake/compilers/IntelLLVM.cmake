# Copyright (c) 2023 Intel Corporation

if (WIN32)
    include(${CMAKE_CURRENT_LIST_DIR}/MSVC.cmake)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/Clang.cmake)
endif()
