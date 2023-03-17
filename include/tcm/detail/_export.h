/*
    Copyright (c) 2023 Intel Corporation
*/

#ifndef __TCM_EXPORT_HEADER
#define __TCM_EXPORT_HEADER

#if _WIN32 || _WIN64
  #define __TCM_EXPORT __declspec(dllexport)
#else
  #define __TCM_EXPORT
#endif

#endif // __EXPORT_HEADER
