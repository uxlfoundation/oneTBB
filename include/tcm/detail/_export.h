/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_EXPORT_HEADER
#define __TCM_EXPORT_HEADER

#if _WIN32 || _WIN64
  #define __TCM_EXPORT __declspec(dllexport)
#else
  #define __TCM_EXPORT __attribute__((visibility("default")))
#endif

#endif // __EXPORT_HEADER
