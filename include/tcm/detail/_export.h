/*
 *
 * Copyright (C) 2023 Intel Corporation
 *
 */

#ifndef __TCM_EXPORT_HEADER
#define __TCM_EXPORT_HEADER

#if WIN32
  #define __TCM_EXPORT __declspec(dllexport)
#else
  #define __TCM_EXPORT
#endif

#endif // __EXPORT_HEADER
