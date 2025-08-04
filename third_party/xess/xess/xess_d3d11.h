/*******************************************************************************
 * Copyright (C) 2024 Intel Corporation
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


#ifndef XESS_DX11_H
#define XESS_DX11_H

#include <d3d11.h>

#include "xess.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
* @note XeSS D3D11 version only works on Intel Hardware. 
*/

XESS_PACK_B()
/**
 * @brief Execution parameters for XeSS D3D11.
 */
typedef struct _xess_d3d11_execute_params_t
{
    /** Input color texture. */
    ID3D11Resource *pColorTexture;
    /** Input motion vector texture. */
    ID3D11Resource *pVelocityTexture;
    /** Optional depth texture. Required if XESS_INIT_FLAG_HIGH_RES_MV has not been specified. */
    ID3D11Resource *pDepthTexture;
    /** Optional 1x1 exposure scale texture. Required if XESS_INIT_FLAG_EXPOSURE_TEXTURE has been
     * specified. */
    ID3D11Resource *pExposureScaleTexture;
    /** Optional responsive pixel mask texture. Required if XESS_INIT_FLAG_RESPONSIVE_PIXEL_MASK
     * has been specified. */
    ID3D11Resource *pResponsivePixelMaskTexture;
    /** Output texture in target resolution. */
    ID3D11Resource *pOutputTexture;

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
} xess_d3d11_execute_params_t;
XESS_PACK_E()

XESS_PACK_B()
/**
 * @brief Initialization parameters for XeSS VK.
 */
typedef struct _xess_d3d11_init_params_t
{
    /** Output width and height. */
    xess_2d_t outputResolution;
    /** Quality setting */
    xess_quality_settings_t qualitySetting;
    /** Initialization flags. */
    uint32_t initFlags;
} xess_d3d11_init_params_t;
XESS_PACK_E()

/** @addtogroup xess-d3d11 XeSS D3D11 API exports
 * @{
 */
/**
 * @brief Create an XeSS D3D11 context.
 * @param device A D3D11 device created by the user.
 * @param[out] phContext Returned xess context handle.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessD3D11CreateContext(ID3D11Device* device, xess_context_handle_t* phContext);

/**
 * @brief Initialize XeSS D3D11.
 * This is a blocking call that initializes XeSS and triggers internal
 * resources allocation and JIT for the XeSS kernels. The user must ensure that
 * any pending command lists are completed before re-initialization. When
 * During initialization, XeSS can create staging buffers and copy queues to
 * upload internal data. These will be destroyed at the end of initialization.
 *
 * @param hContext: The XeSS context handle.
 * @param pInitParams: Initialization parameters.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessD3D11Init(
    xess_context_handle_t hContext, const xess_d3d11_init_params_t* pInitParams);

/**
 * @brief Get XeSS D3D11 initialization parameters.
 * 
 * @note This function will return @ref XESS_RESULT_ERROR_UNINITIALIZED if @ref xessD3D11Init has not been called.
 *
 * @param hContext: The XeSS context handle.
 * @param[out] pInitParams: Returned initialization parameters.
 * @return XeSS return status code.
 */
XESS_API xess_result_t xessD3D11GetInitParams(
    xess_context_handle_t hContext, xess_d3d11_init_params_t* pInitParams);

/**
 * @brief Record XeSS upscaling commands into the command list.
 * @param hContext: The XeSS context handle.
 * @param pExecParams: Execution parameters.
 * @return XeSS return status code.
 */

XESS_API xess_result_t xessD3D11Execute(
    xess_context_handle_t hContext, const xess_d3d11_execute_params_t* pExecParams);

/** @}*/

#ifdef __cplusplus
}
#endif


#endif  // XESS_DX11_H
