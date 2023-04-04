# Copyright (C) 2023 Intel Corporation
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
    set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS} -static-intel -Wl,-z,relro,-z,now,)
    set(TCM_COMMON_COMPILE_FLAGS ${TCM_COMMON_COMPILE_FLAGS} -fstack-protector -Wformat -Wformat-security
                                 $<$<NOT:$<CONFIG:Debug>>:-qno-opt-report-embed -D_FORTIFY_SOURCE=2>)
endif()
