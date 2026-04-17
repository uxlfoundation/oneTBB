# Copyright (c) 2023 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if (WIN32)
    include(${CMAKE_CURRENT_LIST_DIR}/MSVC.cmake)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/Clang.cmake)
    # "--exclude-libs,ALL" is used to avoid accidental exporting of symbols
    #  from statically linked libraries
    set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS} -static-intel -Wl,--exclude-libs,ALL)
endif()
