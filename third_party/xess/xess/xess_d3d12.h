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


#ifndef XESS_D3D12_H
#define XESS_D3D12_H

#include <d3d12.h>

#include "xess.h"

#ifdef __cplusplus
extern "C" {
#endif

XESS_PACK_B()
/**
 * @brief Execution parameters for XeSS D3D12.
 */
typedef struct _xess_d3d12_execute_params_t
{
    /** Input color texture. Must be in NON_PIXEL_SHADER_RESOURCE state.*/
    ID3D12Resource *pColorTexture;
    /** Input motion vector texture. Must be in NON_PIXEL_SHADER_RESOURCE state.*/
    ID3D12Resource *pVelocityTexture;
    /** Optional depth texture. Required if XESS_INIT_FLAG_HIGH_RES_MV has not been specified.
     * Must be in NON_PIXEL_SHADER_RESOURCE state.*/
    ID3D12Resource *pDepthTexture;
    /** Optional 1x1 exposure scale texture. Required if XESS_INIT_FLAG_EXPOSURE_TEXTURE has been
     * specified. Must be in NON_PIXEL_SHADER_RESOURCE state */
    ID3D12Resource *pExposureScaleTexture;
    /** Optional responsive pixel mask texture. Required if XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK
     * has been specified. Must be in NON_PIXEL_SHADER_RESOURCE state */
    ID3D12Resource *pResponsivePixelMaskTexture;
    /** Output texture in target resolution. Must be in UNORDERED_ACCESS state.*/
    ID3D12Resource *pOutputTexture;

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
    /** Optional external descriptor heap. */
    ID3D12DescriptorHeap* pDescriptorHeap;
    /** Offset in external descriptor heap in bytes.*/
    uint32_t descriptorHeapOffset;
} xess_d3d12_execute_params_t;
XESS_PACK_E()

XESS_PACK_B()
/**
 * @brief Initialization parameters for XeSS D3D12.
 */
typedef struct _xess_d3d12_init_params_t
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
    /** Optional externally allocated buffer storage for XeSS. If NULL the
     * storage is allocated internally. If allocated, the heap type must be
     * D3D12_HEAP_TYPE_DEFAULT. This heap is not accessed by the CPU. */
    ID3D12Heap* pTempBufferHeap;
    /** Offset in the externally allocated heap for temporary buffer storage. */
    uint64_t bufferHeapOffset;
    /** Optional externally allocated texture storage for XeSS. If NULL the
     * storage is allocated internally. If allocated, the heap type must be
     * D3D12_HEAP_TYPE_DEFAULT. This heap is not accessed by the CPU. */
    ID3D12Heap* pTempTextureHeap;
    /** Offset in the externally allocated heap for temporary texture storage. */
    uint64_t textureHeapOffset;
    /** Pointer to pipeline library. If not NULL will be used for pipeline caching. */
    ID3D12PipelineLibrary *pPipelineLibrary;
} xess_d3d12_init_params_t;
XESS_PACK_E()

/** @addtogroup xess-d3d12 XeSS D3D12 API exports
 * @{
 */
/**
 * @brief Create an XeSS D3D12 context.
 * @param pDevice: A D3D12 device created by the user.
 * @param[out] phContext: Returned xess context handle.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessD3D12CreateContext(
    ID3D12Device* pDevice, xess_context_handle_t* phContext);

/**
 * @brief Initiates pipeline build process
 * This function can only be called between @ref xessD3D12CreateContext and
 * @ref xessD3D12Init
 * This call initiates build of DX12 pipelines and kernel compilation
 * This call can be blocking (if @p blocking set to true) or non-blocking.
 * In case of non-blocking call library will wait for pipeline build on call to
 * @ref xessD3D12Init
 * If @p pPipelineLibrary passed to this call - same pipeline library must be passed
 * to @ref xessD3D12Init
 *
 * @param hContext The XeSS context handle.
 * @param pPipelineLibrary Optional pointer to pipeline library for pipeline caching.
 * @param blocking Wait for kernel compilation and pipeline creation to finish or not
 * @param initFlags Initialization flags. *Must* be identical to flags passed to @ref xessD3D12Init
 */
XESS_API xess_result_t xessD3D12BuildPipelines(xess_context_handle_t hContext,
    ID3D12PipelineLibrary *pPipelineLibrary, bool blocking, uint32_t initFlags);

/**
 * @brief Initialize XeSS D3D12.
 * This is a blocking call that initializes XeSS and triggers internal
 * resources allocation and JIT for the XeSS kernels. The user must ensure that
 * any pending command lists are completed before re-initialization. When
 * During initialization, XeSS can create staging buffers and copy queues to
 * upload internal data. These will be destroyed at the end of initialization.
 *
 * @note XeSS supports devices starting from D3D12_RESOURCE_HEAP_TIER_1, which means
 * that buffers and textures can not live in the same resource heap.
 *
 * @param hContext: The XeSS context handle.
 * @param pInitParams: Initialization parameters.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessD3D12Init(
    xess_context_handle_t hContext, const xess_d3d12_init_params_t* pInitParams);

/**
 * @brief Get XeSS D3D12 initialization parameters.
 * 
 * @note This function will return @ref XESS_RESULT_ERROR_UNINITIALIZED if @ref xessD3D12Init has not been called.
 *
 * @param hContext: The XeSS context handle.
 * @param[out] pInitParams: Returned initialization parameters.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessD3D12GetInitParams(
    xess_context_handle_t hContext, xess_d3d12_init_params_t* pInitParams);

/**
 * @brief Record XeSS upscaling commands into the command list.
 * @param hContext: The XeSS context handle.
 * @param pCommandList: The command list for XeSS commands.
 * @param pExecParams: Execution parameters.
 * @return XeSS return status code.
 */

XESS_API xess_result_t xessD3D12Execute(xess_context_handle_t hContext,
    ID3D12GraphicsCommandList* pCommandList, const xess_d3d12_execute_params_t* pExecParams);
/** @}*/

#ifdef __cplusplus
}
#endif


#endif  // XESS_D3D12_H
