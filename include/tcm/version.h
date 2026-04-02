/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_VERSION_HEADER
#define __TCM_VERSION_HEADER

#include "detail/_export.h"

#define TCM_VERSION_MAJOR 1
#define TCM_VERSION_MINOR 5
#define TCM_VERSION_PATCH 0

#define TCM_VERSION (10000 * TCM_VERSION_MAJOR + 100 * TCM_VERSION_MINOR + TCM_VERSION_PATCH)

#ifdef __cplusplus
extern "C" {
#endif

__TCM_EXPORT unsigned tcmGetVersion();

[[deprecated]] __TCM_EXPORT const char* tcmRuntimeVersion();
[[deprecated]] __TCM_EXPORT unsigned tcmRuntimeInterfaceVersion();

#ifdef __cplusplus
} // extern "C"
#endif

#endif // __TCM_VERSION_HEADER
