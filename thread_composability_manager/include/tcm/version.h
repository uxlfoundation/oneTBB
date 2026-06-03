/*
   Copyright (c) 2023 Intel Corporation
   Copyright (c) 2026 UXL Foundation Contributors

   SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
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
