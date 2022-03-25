/*
 *
 * Copyright (C) 2021-2022 Intel Corporation
 *
 */

#ifndef __TCM_HEADER
#define __TCM_HEADER

#include "tcm/types.h"

#if WIN32
  #define __TCM_EXPORT __declspec(dllexport)
#else
  #define __TCM_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Thread Composability Manager interfaces:

__TCM_EXPORT ze_result_t zermConnect(zerm_callback_t callback,
                                     zerm_client_id_t *client_id);
__TCM_EXPORT ze_result_t zermDisconnect(zerm_client_id_t client_id);

__TCM_EXPORT ze_result_t zermRequestPermit(zerm_client_id_t client_id,
                                           zerm_permit_request_t request,
                                           void* callback_arg,
                                           zerm_permit_handle_t* permit_handle,
                                           zerm_permit_t* permit);

__TCM_EXPORT ze_result_t zermGetPermitData(zerm_permit_handle_t permit_handle,
                                           zerm_permit_t* permit);

__TCM_EXPORT ze_result_t zermReleasePermit(zerm_permit_handle_t permit);

__TCM_EXPORT ze_result_t zermIdlePermit(zerm_permit_handle_t permit_handle);

__TCM_EXPORT ze_result_t zermDeactivatePermit(zerm_permit_handle_t permit_handle);

__TCM_EXPORT ze_result_t zermActivatePermit(zerm_permit_handle_t permit_handle);

__TCM_EXPORT ze_result_t zermRegisterThread(zerm_permit_handle_t permit_handle);

__TCM_EXPORT ze_result_t zermUnregisterThread();

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __TCM_HEADER */

