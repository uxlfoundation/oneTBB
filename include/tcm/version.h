/*
    Copyright (c) 2023 Intel Corporation
*/

#ifndef __TCM_VERSION_HEADER
#define __TCM_VERSION_HEADER

#include "detail/_export.h"

#define __TCM_TO_STR_IMPL(str) #str
#define __TCM_TO_STR(str) __TCM_TO_STR_IMPL(str)

#ifndef ENDL
#define ENDL "\n"
#endif

#define TCM_VERSION_MAJOR 1
#define TCM_VERSION_MINOR 0
#define TCM_VERSION_PATCH 0

#define TCM_INTERFACE_VERSION 1000
#define TCM_INTERFACE_VERSION_MAJOR (TCM_INTERFACE_VERSION/1000)
#define TCM_INTERFACE_VERSION_MINOR ((TCM_INTERFACE_VERSION%1000)/10)

#define __TCM_VERSION_SUFFIX "-beta"

#define TCM_VERSION __TCM_TO_STR(TCM_VERSION_MAJOR) "." \
                    __TCM_TO_STR(TCM_VERSION_MINOR) "." \
                    __TCM_TO_STR(TCM_VERSION_PATCH)     \
                    __TCM_VERSION_SUFFIX

#define __TCM_VERSION_NUMBER "TCM: VERSION\t\t\t\t" TCM_VERSION ENDL

#ifndef TCM_DEBUG
    #define __TCM_DEBUG      "TCM: TCM_DEBUG\t\t\t\tundefined" ENDL
#else
    #define __TCM_DEBUG      "TCM: TCM_DEBUG\t\t\t\tset" ENDL
#endif

#define TCM_PRINT_VERSION \
    __TCM_VERSION_NUMBER  \
    __TCM_DEBUG

#ifdef __cplusplus
extern "C" {
#endif

__TCM_EXPORT const char* tcmRuntimeVersion();
__TCM_EXPORT unsigned tcmRuntimeInterfaceVersion();

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __TCM_VERSION_HEADER
