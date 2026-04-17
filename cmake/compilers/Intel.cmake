# Copyright (c) 2023 Intel Corporation
#
# SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

if (MSVC)
    include(${CMAKE_CURRENT_LIST_DIR}/MSVC.cmake)
    set(TCM_WARNING_LEVEL ${TCM_WARNING_LEVEL} /W3)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/GNU.cmake)
    # "--exclude-libs,ALL" is used to avoid accidental exporting of symbols
    #  from statically linked libraries
    set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS} -static-intel -Wl,-z,relro,-z,now,--exclude-libs,ALL)
    set(TCM_COMMON_COMPILE_FLAGS ${TCM_COMMON_COMPILE_FLAGS} -fstack-protector -Wformat -Wformat-security
                                 $<$<NOT:$<CONFIG:Debug>>:-qno-opt-report-embed -D_FORTIFY_SOURCE=2>)
    set(TCM_VISIBILITY_INLINES_HIDDEN_FLAG -fvisibility-inlines-hidden)
endif()
