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


#ifndef XESS_VK_DEBUG_H
#define XESS_VK_DEBUG_H

#include "xess_vk.h"
#include "xess_debug.h"

#ifdef __cplusplus
extern "C" {
#endif

XESS_PACK_B()
typedef struct _xess_vk_resource_to_dump_desc_t
{
    VkImage image;
    VkBuffer buffer;
    VkFormat image_format;
    uint64_t width;
    uint32_t height;
    VkImageLayout image_layout;
    uint32_t image_array_size;
    uint32_t image_depth;
} xess_vk_resource_to_dump_desc_t;
XESS_PACK_E()

XESS_PACK_B()
typedef struct _xess_vk_resources_to_dump_t
{
    /** Total resource count. In case it equal to zero content of other structure members is undefined.*/
    uint32_t resource_count;
    /** Pointer to an internal array of Vulkan resource descriptions (VkImage or VkBuffer). Array length is `resource_count`.*/
    _xess_vk_resource_to_dump_desc_t const* resources;
    /** Pointer to an internal array of Vulkan resource names. Array length is `resource_count`.*/
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
} xess_vk_resources_to_dump_t;
XESS_PACK_E()

/** @addtogroup xess-vk-debug XeSS Vulkan API debug exports
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
XESS_API xess_result_t xessVKGetResourcesToDump(xess_context_handle_t hContext, xess_vk_resources_to_dump_t** pResourcesToDump);

/** @}*/

#ifdef __cplusplus
}
#endif


#endif  // XESS_VK_DEBUG_H
