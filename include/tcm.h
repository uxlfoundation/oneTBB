/*
    Copyright (C) 2023 Intel Corporation

    This software and the related documents are Intel copyrighted materials, and your use of them is
    governed by the express license under which they were provided to you ("License"). Unless the
    License provides otherwise, you may not use, modify, copy, publish, distribute, disclose or
    transmit this software or the related documents without Intel's prior written permission.

    This software and the related documents are provided as is, with no express or implied
    warranties, other than those that are expressly stated in the License.
*/

#ifndef __TCM_HEADER
#define __TCM_HEADER

#include "tcm/detail/_export.h"
#include "tcm/types.h"
#include "tcm/version.h"

#ifdef __cplusplus
extern "C" {
#endif

// Thread Composability Manager interfaces:

__TCM_EXPORT tcm_result_t tcmConnect(tcm_callback_t callback,
                                     tcm_client_id_t *client_id);
__TCM_EXPORT tcm_result_t tcmDisconnect(tcm_client_id_t client_id);

__TCM_EXPORT tcm_result_t tcmRequestPermit(tcm_client_id_t client_id,
                                           tcm_permit_request_t request,
                                           void* callback_arg,
                                           tcm_permit_handle_t* permit_handle,
                                           tcm_permit_t* permit);

__TCM_EXPORT tcm_result_t tcmGetPermitData(tcm_permit_handle_t permit_handle,
                                           tcm_permit_t* permit);

__TCM_EXPORT tcm_result_t tcmReleasePermit(tcm_permit_handle_t permit);

__TCM_EXPORT tcm_result_t tcmIdlePermit(tcm_permit_handle_t permit_handle);

__TCM_EXPORT tcm_result_t tcmDeactivatePermit(tcm_permit_handle_t permit_handle);

__TCM_EXPORT tcm_result_t tcmActivatePermit(tcm_permit_handle_t permit_handle);

__TCM_EXPORT tcm_result_t tcmRegisterThread(tcm_permit_handle_t permit_handle);

__TCM_EXPORT tcm_result_t tcmUnregisterThread();

__TCM_EXPORT tcm_result_t tcmGetVersionInfo(char* buffer, uint32_t buffer_size);

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* __TCM_HEADER */

