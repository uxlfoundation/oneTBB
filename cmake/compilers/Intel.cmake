# Copyright (c) 2023 Intel Corporation

if (MSVC)
    include(${CMAKE_CURRENT_LIST_DIR}/MSVC.cmake)
    set(TCM_WARNING_LEVEL ${TCM_WARNING_LEVEL} /W3)
else()
    include(${CMAKE_CURRENT_LIST_DIR}/GNU.cmake)
    set(TCM_LIB_LINK_FLAGS ${TCM_LIB_LINK_FLAGS} -static-intel -Wl,-z,relro,-z,now,)
    set(TCM_COMMON_COMPILE_FLAGS ${TCM_COMMON_COMPILE_FLAGS} -fstack-protector -Wformat -Wformat-security
                                 $<$<NOT:$<CONFIG:Debug>>:-qno-opt-report-embed -D_FORTIFY_SOURCE=2>)
endif()
