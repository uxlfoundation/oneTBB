# Copyright (C) 2023-2024 Intel Corporation
#
# This software and the related documents are Intel copyrighted materials, and your use of them is
# governed by the express license under which they were provided to you ("License"). Unless the
# License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
# transmit this software or the related documents without Intel's prior written permission.
#
# This software and the related documents are provided as is, with no express or implied
# warranties, other than those that are expressly stated in the License.

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
