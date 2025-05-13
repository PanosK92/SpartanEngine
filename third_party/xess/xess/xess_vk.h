/*******************************************************************************
 * Copyright (C) 2023 Intel Corporation
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


#ifndef XESS_VK_H
#define XESS_VK_H

#include <vulkan/vulkan.h>

#include "xess.h"

#ifdef __cplusplus
extern "C" {
#endif

XESS_PACK_B()

typedef struct _xess_vk_image_view_info
{
    VkImageView imageView;
    VkImage image;
    VkImageSubresourceRange subresourceRange;
    VkFormat format;
    unsigned int width;
    unsigned int height;
} xess_vk_image_view_info;

XESS_PACK_E()

XESS_PACK_B()
/**
 * @brief Execution parameters for XeSS Vulkan.
 */
typedef struct _xess_vk_execute_params_t
{
    /** Input color texture. Must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL state.*/
    xess_vk_image_view_info colorTexture;
    /** Input motion vector texture. Must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL state.*/
    xess_vk_image_view_info velocityTexture;
    /** Optional depth texture. Required if XESS_INIT_FLAG_HIGH_RES_MV has not been specified.
     * Must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL state.*/
    xess_vk_image_view_info depthTexture;
    /** Optional 1x1 exposure scale texture. Required if XESS_INIT_FLAG_EXPOSURE_TEXTURE has been
     * specified. Must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL state */
    xess_vk_image_view_info exposureScaleTexture;
    /** Optional responsive pixel mask texture. Required if XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK
     * has been specified. Must be in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL state */
    xess_vk_image_view_info responsivePixelMaskTexture;
    /** Output texture in target resolution. Must be in VK_IMAGE_LAYOUT_GENERAL state.*/
    xess_vk_image_view_info outputTexture;

    /** Jitter X coordinate in the range [-0.5, 0.5]. */
    float jitterOffsetX;
    /** Jitter Y coordinate in the range [-0.5, 0.5]. */
    float jitterOffsetY;
    /** Optional input color scaling. Default is 1. */
    float exposureScale;
    /** Resets the history accumulation in this frame. */
    uint32_t resetHistory;
    /** Input color width. */
    uint32_t inputWidth;
    /** Input color height. */
    uint32_t inputHeight;
    /** Base coordinate for the input color in the texture. Default is (0,0). */
    xess_coord_t inputColorBase;
    /** Base coordinate for the input motion vector in the texture.  Default is (0,0). */
    xess_coord_t inputMotionVectorBase;
    /** Base coordinate for the input depth in the texture. Default is (0,0). */
    xess_coord_t inputDepthBase;
    /** Base coordinate for the input responsive pixel mask in the texture. Default is (0,0). */
    xess_coord_t inputResponsiveMaskBase;
    /** Reserved parameter. */
    xess_coord_t reserved0;
    /** Base coordinate for the output color.  Default is (0,0). */
    xess_coord_t outputColorBase;
} xess_vk_execute_params_t;
XESS_PACK_E()

XESS_PACK_B()
/**
 * @brief Initialization parameters for XeSS VK.
 */
typedef struct _xess_vk_init_params_t
{
    /** Output width and height. */
    xess_2d_t outputResolution;
    /** Quality setting */
    xess_quality_settings_t qualitySetting;
    /** Initialization flags. */
    uint32_t initFlags;
    /** Specifies the node mask for internally created resources on
     * multi-adapter systems. */
    uint32_t creationNodeMask;
    /** Specifies the node visibility mask for internally created resources
     * on multi-adapter systems. */
    uint32_t visibleNodeMask;
    /** Optional externally allocated buffer memory for XeSS. If VK_NULL_HANDLE the
     * memory is allocated internally. If provided, the memory must be allocated from
     * memory type that supports allocating buffers. The memory type should be DEVICE_LOCAL.
     * This memory is not accessed by the CPU. */
    VkDeviceMemory tempBufferHeap;
    /** Offset in the externally allocated memory for temporary buffer storage. */
    uint64_t bufferHeapOffset;
    /** Optional externally allocated texture memory for XeSS. If VK_NULL_HANDLE the
     * memory is allocated internally. If provided, the memory must be allocated from
     * memory type that supports allocating textures. The memory type should be DEVICE_LOCAL.
     * This memory is not accessed by the CPU. */
    VkDeviceMemory tempTextureHeap;
    /** Offset in the externally allocated memory for temporary texture storage. */
    uint64_t textureHeapOffset;
    /** Optional pipeline cache. If not VK_NULL_HANDLE will be used for pipeline creation. */
    VkPipelineCache pipelineCache;
} xess_vk_init_params_t;
XESS_PACK_E()

/** @addtogroup xess-vulkan XeSS VK API exports
 * @{
 */
 /**
  * @brief Get required extensions for Vulkan instance that would run XeSS.
  * This function must be called to get instance extensions need by XeSS. These
  * extensions must be enabled in subsequent vkCreateInstance call, that would create a VkInstance
  * object to be passed to @ref xessVKCreateContext
  * @param[out] instanceExtensionsCount returns a count of instance extensions to be enabled in Vulkan instance
  * @param[out] instanceExtensions returns a pointer to an array of \p instanceExtensionsCount required extension names.
  *             The memory used by the array is owned by the XeSS library and should not be freed by the application.
  * @param[out] minVkApiVersion The Vulkan API version that XeSS would use. When calling vkCreateInstance, the application
  *             should set VkApplicationInfo.apiVersion to value greater or equal to \p minVkApiVersion
  * @return XeSS return status code.
  */
XESS_API xess_result_t xessVKGetRequiredInstanceExtensions(uint32_t* instanceExtensionsCount, const char* const** instanceExtensions, uint32_t* minVkApiVersion);


 /**
  * @brief Get required extensions for Vulkan device that would run XeSS.
  * This function must be called to get device extensions need by XeSS. These
  * extensions must be enabled in subsequent vkCreateDevice call, that would create a VkDevice
  * object to be passed to @ref xessVKCreateContext
  * @param instance A VkInstance object created by the user
  * @param physicalDevice A VkPhysicalDevice selected by the user from instance
  * @param[out] deviceExtensionsCount returns a count of device extensions to be enabled in Vulkan device
  * @param[out] deviceExtensions returns a pointer to an array of \p deviceExtensionsCount required extension names
  *            The memory used by the array is owned by the XeSS library and should not be freed by the application.
  * @return XeSS return status code.
  */
XESS_API xess_result_t xessVKGetRequiredDeviceExtensions(VkInstance instance, VkPhysicalDevice physicalDevice,
   uint32_t* deviceExtensionsCount, const char* const** deviceExtensions);


 /**
  * @brief Get required features for Vulkan device that would run XeSS.
  * This function must be called to get device features need by XeSS. These
  * features must be enabled in subsequent vkCreateDevice call, that would create a VkDevice
  * object to be passed to @ref xessVKCreateContext.
  * @param instance A VkInstance object created by the user
  * @param physicalDevice A VkPhysicalDevice selected by the user from instance
  * @param[out] features a pointer to writable chain of feature structures, that this function would patch
  *             with required features, by filling required fields and attaching new structures to the chain if needed.
  *             The returned pointer should be passed to vkCreateDevice as pNext chain of VkDeviceCreateInfo structure.
  *             If null is passed, than the function constructs a new structure chain that should be merged
  *             into the chain that application would use with VkDeviceCreateInfo, with application responsibility to
  *             avoid any duplicates with its own structures.
  *             
  *             It is an error to chain VkDeviceCreateInfo structure with not null pEnabledFeatures field, as this field is const
  *             and cannot be patched by this function. VkPhysicalDeviceFeatures2 structure should be used instead.
  *             
  *             The memory used by the structures added by this funtion to the chain is owned by the XeSS library and
  *             should not be freed by the application.
  * @return XeSS return status code.
  */
XESS_API xess_result_t xessVKGetRequiredDeviceFeatures(VkInstance instance, VkPhysicalDevice physicalDevice, void** features);


/**
 * @brief Create an XeSS VK context.
 * @param instance A VkInstance object created by the user
 * @param physicalDevice A VkPhysicalDevice selected by the user from instance
 * @param device A VK device created by the user from physicalDevice
 * @param[out] phContext Returned xess context handle.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessVKCreateContext(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device,
    xess_context_handle_t* phContext);

/**
 * @brief Initiates pipeline build process
 * This function can only be called between @ref xessVKCreateContext and
 * @ref xessVKInit
 * This call initiates build of Vulkan pipelines and kernel compilation
 * This call can be blocking (if @p blocking set to true) or non-blocking.
 * In case of non-blocking call library will wait for pipeline build on call to
 * @ref xessVKInit
 * If @p pipelineCache passed to this call - same pipeline library must be passed
 * to @ref xessVKInit
 *
 * @param hContext The XeSS context handle.
 * @param pipelineCache Optional pointer to pipeline library for pipeline caching.
 * @param blocking Wait for kernel compilation and pipeline creation to finish or not
 * @param initFlags Initialization flags. *Must* be identical to flags passed to @ref xessVKInit
 */
XESS_API xess_result_t xessVKBuildPipelines(xess_context_handle_t hContext,
    VkPipelineCache pipelineCache, bool blocking, uint32_t initFlags);

/**
 * @brief Initialize XeSS VK.
 * This is a blocking call that initializes XeSS and triggers internal
 * resources allocation and JIT for the XeSS kernels. The user must ensure that
 * any pending command lists are completed before re-initialization. When
 * During initialization, XeSS can create staging buffers and copy queues to
 * upload internal data. These will be destroyed at the end of initialization.
 *
 * @note XeSS supports devices starting from VK_RESOURCE_HEAP_TIER_1, which means
 * that buffers and textures can not live in the same resource heap.
 *
 * @param hContext: The XeSS context handle.
 * @param pInitParams: Initialization parameters.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessVKInit(
    xess_context_handle_t hContext, const xess_vk_init_params_t* pInitParams);

/**
 * @brief Get XeSS VK initialization parameters.
 * 
 * @note This function will return @ref XESS_RESULT_ERROR_UNINITIALIZED if @ref xessVKInit has not been called.
 *
 * @param hContext: The XeSS context handle.
 * @param[out] pInitParams: Returned initialization parameters.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessVKGetInitParams(
    xess_context_handle_t hContext, xess_vk_init_params_t* pInitParams);

/**
 * @brief Record XeSS upscaling commands into the command list.
 * @param hContext: The XeSS context handle.
 * @param commandBuffer: The command bufgfer for XeSS commands.
 * @param pExecParams: Execution parameters.
 * @return XeSS return status code.
 */

XESS_API xess_result_t xessVKExecute(xess_context_handle_t hContext,
    VkCommandBuffer commandBuffer, const xess_vk_execute_params_t* pExecParams);
/** @}*/

#ifdef __cplusplus
}
#endif


#endif  // XESS_VK_H
