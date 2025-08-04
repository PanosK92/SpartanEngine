/*******************************************************************************
 * Copyright (C) 2021 Intel Corporation
 * 
 * This software and the related documents are Intel copyrighted materials, and
 * your use of them is governed by the express license under which they were
 * provided to you ("License"). Unless the License provides otherwise, you may
 * not use, modify, copy, publish, distribute, disclose or transmit this
 * software or the related documents without Intel's prior written permission.
 * 
 * This software and the related documents are provided as is, with no express
 * or implied warranties, other than those that are expressly stated in the
 * License.
 ******************************************************************************/


#ifndef XESS_D3D12_DEBUG_H
#define XESS_D3D12_DEBUG_H

#include <d3d12.h>

#include "xess.h"
#include "xess_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Define XESS_D3D12_DEBUG_ENABLE_PROFILING for backwards compatibility with previous XeSS versions*/
#ifndef XESS_D3D12_DEBUG_ENABLE_PROFILING
#define XESS_D3D12_DEBUG_ENABLE_PROFILING XESS_DEBUG_ENABLE_PROFILING
#endif

XESS_PACK_B()
typedef struct _xess_resources_to_dump_t
{
    /** Total resource count. In case it equal to zero content of other structure members is undefined.*/
    uint32_t resource_count;
    /** Pointer to an internal array of D3D12 resources. Array length is `resource_count`.*/
    ID3D12Resource* const* resources;
    /** Pointer to an internal array of D3D12 resource names. Array length is `resource_count`.*/
    const char* const* resource_names;
    /* Pointer to an internal array of suggested resource dump modes. Array length is `resource_count.`
     * If as_tensor is 0 (FALSE), then it is suggested to dump resource as RGBA texture.*/
    const uint32_t* as_tensor; // 0 = false
    /* Pointer to an internal array of paddings to be used during resource dump. Array length is `resource_count`.
     * Padding is assumed to be symmetrical across spatial dimensions and has same value for both borders in each dimension.*/
    const uint32_t* border_pixels_to_skip_count;
    /* Pointer to an internal array of channel count for each resource. If resource dimension is "buffer" value is non zero,
     * count is zero otherwise. Array length is `resource_count`.*/
    const uint32_t* tensor_channel_count;
    /* Pointer to an internal array of tensor width for each resource. Width must include padding on both sides.
     * If resource dimension is "buffer" value is non zero, * count is zero otherwise. Array length is `resource_count`.*/
    const uint32_t* tensor_width;
    /* Pointer to an internal array of tensor width for each resource. Height must include padding on both sides.
     * If resource dimension is "buffer" value is non zero, * count is zero otherwise. Array length is `resource_count`.*/
    const uint32_t* tensor_height;
} xess_resources_to_dump_t;
XESS_PACK_E()

/** @addtogroup xess-d3d12-debug XeSS D3D12 API debug exports
 * @{
 */
/**
 * @brief Query XeSS model to retrieve internal resources marked for dumping for further
 * debug and inspection.
 * @param hContext: The XeSS context handle.
 * @param pResourcesToDump: Pointer to user-provided pointer to structure to be filled with
 * debug resource array, their names and recommended dumping parameters. pResourcesToDump must not be null,
 * In case of failure (xess_result_t is not equal to XESS_RESULT_SUCCESS) **pResourcesToDump contents is undefined and must not be used.
 * In case of success, *pResourcesToDump may still be null, if no internal resources were added to dumping queue.
 * Build configuration for certain implementations may have dumping functionality compiled-out and XESS_RESULT_ERROR_NOT_IMPLEMENTED return code.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessD3D12GetResourcesToDump(xess_context_handle_t hContext, xess_resources_to_dump_t** pResourcesToDump);

/**
 * @brief Query XeSS model performance data for past executions.
 * This function is provided for backwards compatibility with previous XeSS versions and
 * and it's currently deprecated. Same functionality is provided by xessGetProfilingData
 * function from xess_debug.h. To enable performance collection,
 * context must be initialized with XESS_DEBUG_ENABLE_PROFILING flag added to xess_d3d12_init_params_t::initFlags.
 * If profiling is enabled, user must poll for profiling data after executing one or more command lists, otherwise
 * implementation will keep growing internal CPU buffers to accommodate all profiling data available.
 * Due to async nature of execution on GPU, data may not be available after submitting command lists to device queue.
 * It is advised to check `any_profiling_data_in_flight` flag in case all workloads has been submitted, but profiling
 * data for some frames is still not available.
 * Data pointed to by pProfilingData item(s) belongs to context instance and
 * is valid until next call to xessD3D12GetProfilingData or xessGetProfilingData for owning context.
 * @param hContext: The XeSS context handle.
 * @param pProfilingData: pointer to profiling data structure to be filled by implementation.
 * @return XeSS return status code.
*/
XESS_API xess_result_t xessD3D12GetProfilingData(xess_context_handle_t hContext, xess_profiling_data_t** pProfilingData);


/** @}*/

#ifdef __cplusplus
}
#endif


#endif  // XESS_D3D12_H
