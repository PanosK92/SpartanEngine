/*
* Copyright (c) 2021-2025 NVIDIA CORPORATION.  All rights reserved.
*
* NVIDIA Corporation and its licensors retain all intellectual property and proprietary
* rights in and to this software, related documentation and any modifications thereto.
* Any use, reproduction, disclosure or distribution of this software and related
* documentation without an express license agreement from NVIDIA Corporation is strictly
* prohibited.
*
* TO THE MAXIMUM EXTENT PERMITTED BY APPLICABLE LAW, THIS SOFTWARE IS PROVIDED *AS IS*
* AND NVIDIA AND ITS SUPPLIERS DISCLAIM ALL WARRANTIES, EITHER EXPRESS OR IMPLIED,
* INCLUDING, BUT NOT LIMITED TO, IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
* PARTICULAR PURPOSE.  IN NO EVENT SHALL NVIDIA OR ITS SUPPLIERS BE LIABLE FOR ANY
* SPECIAL, INCIDENTAL, INDIRECT, OR CONSEQUENTIAL DAMAGES WHATSOEVER (INCLUDING, WITHOUT
* LIMITATION, DAMAGES FOR LOSS OF BUSINESS PROFITS, BUSINESS INTERRUPTION, LOSS OF
* BUSINESS INFORMATION, OR ANY OTHER PECUNIARY LOSS) ARISING OUT OF THE USE OF OR
* INABILITY TO USE THIS SOFTWARE, EVEN IF NVIDIA HAS BEEN ADVISED OF THE POSSIBILITY OF
* SUCH DAMAGES.
*/

#pragma once

#ifndef NVSDK_NGX_HELPERS_DLSSG_H
#define NVSDK_NGX_HELPERS_DLSSG_H

#include <stdint.h> // for uint64_t

#include "nvsdk_ngx.h" // DX11/12 here
#include "nvsdk_ngx_defs_dlssg.h"
#include "nvsdk_ngx_params_dlssg.h"

typedef struct NVSDK_NGX_D3D12_DLSSG_Eval_Params
{
    ID3D12Resource* pBackbuffer;
    ID3D12Resource* pDepth;
    ID3D12Resource* pMVecs;
    ID3D12Resource* pHudless;                       // Optional
    ID3D12Resource* pUI;                            // Optional
    ID3D12Resource* pUIAlpha;                       // Optional
    ID3D12Resource* pBidirectionalDistortionField;  // Optional
    ID3D12Resource* pOutputInterpFrame;
    ID3D12Resource* pOutputRealFrame;               // Optional. In some cases, the feature may modify this frame (e.g. debugging)
    ID3D12Resource* pOutputDisableInterpolation;    // Optional
} NVSDK_NGX_D3D12_DLSSG_Eval_Params;

static inline NVSDK_NGX_Result NGX_D3D12_CREATE_DLSSG(
    ID3D12GraphicsCommandList* pInCmdList,
    unsigned int InCreationNodeMask,
    unsigned int InVisibilityNodeMask,
    NVSDK_NGX_Handle** ppOutHandle,
    NVSDK_NGX_Parameter* pInParams,
    NVSDK_NGX_DLSSG_Create_Params* pInDlssgCreateParams)
{
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_CreationNodeMask, InCreationNodeMask);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_VisibilityNodeMask, InVisibilityNodeMask);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_Width, pInDlssgCreateParams->Width);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_Parameter_Height, pInDlssgCreateParams->Height);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_BackbufferFormat, pInDlssgCreateParams->NativeBackbufferFormat);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_InternalWidth, pInDlssgCreateParams->RenderWidth);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_InternalHeight, pInDlssgCreateParams->RenderHeight);
    NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_DynamicResolution, pInDlssgCreateParams->DynamicResolutionScaling);

    return NVSDK_NGX_D3D12_CreateFeature(pInCmdList, NVSDK_NGX_Feature_FrameGeneration, pInParams, ppOutHandle);
}

static inline NVSDK_NGX_Result NGX_D3D12_EVALUATE_DLSSG(
    ID3D12GraphicsCommandList* pInCmdList,
    NVSDK_NGX_Handle* pInHandle,
    NVSDK_NGX_Parameter* pInParams,
    NVSDK_NGX_D3D12_DLSSG_Eval_Params* pInDlssgEvalParams,
    NVSDK_NGX_DLSSG_Opt_Eval_Params* pInDlssgOptEvalParams)
{
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_Backbuffer, pInDlssgEvalParams->pBackbuffer);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_MVecs, pInDlssgEvalParams->pMVecs);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_Depth, pInDlssgEvalParams->pDepth);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_HUDLess, pInDlssgEvalParams->pHudless);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_UI, pInDlssgEvalParams->pUI);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_UIAlpha, pInDlssgEvalParams->pUIAlpha);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionField, pInDlssgEvalParams->pBidirectionalDistortionField);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputInterpolated, pInDlssgEvalParams->pOutputInterpFrame);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputReal, pInDlssgEvalParams->pOutputRealFrame);
    NVSDK_NGX_Parameter_SetD3d12Resource(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputDisableInterpolation, pInDlssgEvalParams->pOutputDisableInterpolation);

    if (pInDlssgOptEvalParams)
    {
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MultiFrameCount, pInDlssgOptEvalParams->multiFrameCount);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MultiFrameIndex, pInDlssgOptEvalParams->multiFrameIndex);

        NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraViewToClip, pInDlssgOptEvalParams->cameraViewToClip);
        NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_DLSSG_Parameter_ClipToCameraView, pInDlssgOptEvalParams->clipToCameraView);
        NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_DLSSG_Parameter_ClipToLensClip, pInDlssgOptEvalParams->clipToLensClip);
        NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_DLSSG_Parameter_ClipToPrevClip, pInDlssgOptEvalParams->clipToPrevClip);
        NVSDK_NGX_Parameter_SetVoidPointer(pInParams, NVSDK_NGX_DLSSG_Parameter_PrevClipToClip, pInDlssgOptEvalParams->prevClipToClip);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_JitterOffsetX, pInDlssgOptEvalParams->jitterOffset[0]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_JitterOffsetY, pInDlssgOptEvalParams->jitterOffset[1]);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_MvecScaleX, pInDlssgOptEvalParams->mvecScale[0]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_MvecScaleY, pInDlssgOptEvalParams->mvecScale[1]);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraPinholeOffsetX, pInDlssgOptEvalParams->cameraPinholeOffset[0]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraPinholeOffsetY, pInDlssgOptEvalParams->cameraPinholeOffset[1]);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraPosX, pInDlssgOptEvalParams->cameraPos[0]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraPosY, pInDlssgOptEvalParams->cameraPos[1]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraPosZ, pInDlssgOptEvalParams->cameraPos[2]);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraUpX, pInDlssgOptEvalParams->cameraUp[0]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraUpY, pInDlssgOptEvalParams->cameraUp[1]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraUpZ, pInDlssgOptEvalParams->cameraUp[2]);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraRightX, pInDlssgOptEvalParams->cameraRight[0]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraRightY, pInDlssgOptEvalParams->cameraRight[1]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraRightZ, pInDlssgOptEvalParams->cameraRight[2]);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraFwdX, pInDlssgOptEvalParams->cameraFwd[0]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraFwdY, pInDlssgOptEvalParams->cameraFwd[1]);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraFwdZ, pInDlssgOptEvalParams->cameraFwd[2]);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraNear, pInDlssgOptEvalParams->cameraNear);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraFar, pInDlssgOptEvalParams->cameraFar);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraFOV, pInDlssgOptEvalParams->cameraFOV);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraAspectRatio, pInDlssgOptEvalParams->cameraAspectRatio);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_ColorBuffersHDR, pInDlssgOptEvalParams->colorBuffersHDR);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_DepthInverted, pInDlssgOptEvalParams->depthInverted);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_CameraMotionIncluded, pInDlssgOptEvalParams->cameraMotionIncluded);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_Reset, pInDlssgOptEvalParams->reset);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_AutomodeOverrideReset, pInDlssgOptEvalParams->automodeOverrideReset);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_NotRenderingGameFrames, pInDlssgOptEvalParams->notRenderingGameFrames);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OrthoProjection, pInDlssgOptEvalParams->orthoProjection);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_MvecInvalidValue, pInDlssgOptEvalParams->motionVectorsInvalidValue);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MvecDilated, pInDlssgOptEvalParams->motionVectorsDilated);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MenuDetectionEnabled, pInDlssgOptEvalParams->menuDetectionEnabled);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MVecsSubrectBaseX, pInDlssgOptEvalParams->mvecsSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MVecsSubrectBaseY, pInDlssgOptEvalParams->mvecsSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MVecsSubrectWidth, pInDlssgOptEvalParams->mvecsSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_MVecsSubrectHeight, pInDlssgOptEvalParams->mvecsSubrectSize.Height);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_DepthSubrectBaseX, pInDlssgOptEvalParams->depthSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_DepthSubrectBaseY, pInDlssgOptEvalParams->depthSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_DepthSubrectWidth, pInDlssgOptEvalParams->depthSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_DepthSubrectHeight, pInDlssgOptEvalParams->depthSubrectSize.Height);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_HUDLessSubrectBaseX, pInDlssgOptEvalParams->hudLessSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_HUDLessSubrectBaseY, pInDlssgOptEvalParams->hudLessSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_HUDLessSubrectWidth, pInDlssgOptEvalParams->hudLessSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_HUDLessSubrectHeight, pInDlssgOptEvalParams->hudLessSubrectSize.Height);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UISubrectBaseX, pInDlssgOptEvalParams->uiSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UISubrectBaseY, pInDlssgOptEvalParams->uiSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UISubrectWidth, pInDlssgOptEvalParams->uiSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UISubrectHeight, pInDlssgOptEvalParams->uiSubrectSize.Height);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UIAlphaSubrectBaseX, pInDlssgOptEvalParams->uiAlphaSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UIAlphaSubrectBaseY, pInDlssgOptEvalParams->uiAlphaSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UIAlphaSubrectWidth, pInDlssgOptEvalParams->uiAlphaSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_UIAlphaSubrectHeight, pInDlssgOptEvalParams->uiAlphaSubrectSize.Height);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionFieldSubrectBaseX, pInDlssgOptEvalParams->bidirectionalDistFieldSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionFieldSubrectBaseY, pInDlssgOptEvalParams->bidirectionalDistFieldSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionFieldSubrectWidth, pInDlssgOptEvalParams->bidirectionalDistFieldSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionFieldSubrectHeight, pInDlssgOptEvalParams->bidirectionalDistFieldSubrectSize.Height);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionField_LowPrecision_IsLowPrecision, pInDlssgOptEvalParams->bidirectionalDistFieldPrecisionInfo.IsLowPrecision);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionField_LowPrecision_Bias, pInDlssgOptEvalParams->bidirectionalDistFieldPrecisionInfo.Bias);
        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_BidirectionalDistortionField_LowPrecision_Scale, pInDlssgOptEvalParams->bidirectionalDistFieldPrecisionInfo.Scale);

        NVSDK_NGX_Parameter_SetF(pInParams, NVSDK_NGX_DLSSG_Parameter_MinRelativeLinearDepthObjectSeparation, pInDlssgOptEvalParams->minRelativeLinearDepthObjectSeparation);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_InputBackbufferSubrectBaseX,  pInDlssgOptEvalParams->backbufferSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_InputBackbufferSubrectBaseY,  pInDlssgOptEvalParams->backbufferSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_InputBackbufferSubrectWidth,  pInDlssgOptEvalParams->backbufferSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_InputBackbufferSubrectHeight, pInDlssgOptEvalParams->backbufferSubrectSize.Height);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputInterpolatedSubrectBaseX, pInDlssgOptEvalParams->outputInterpSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputInterpolatedSubrectBaseY, pInDlssgOptEvalParams->outputInterpSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputInterpolatedSubrectWidth, pInDlssgOptEvalParams->outputInterpSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputInterpolatedSubrectHeight, pInDlssgOptEvalParams->outputInterpSubrectSize.Height);

        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputRealSubrectBaseX, pInDlssgOptEvalParams->outputRealSubrectBase.X);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputRealSubrectBaseY, pInDlssgOptEvalParams->outputRealSubrectBase.Y);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputRealSubrectWidth, pInDlssgOptEvalParams->outputRealSubrectSize.Width);
        NVSDK_NGX_Parameter_SetUI(pInParams, NVSDK_NGX_DLSSG_Parameter_OutputRealSubrectHeight, pInDlssgOptEvalParams->outputRealSubrectSize.Height);
    }

    return NVSDK_NGX_D3D12_EvaluateFeature_C(pInCmdList, pInHandle, pInParams, NULL);
}

static inline NVSDK_NGX_Result NGX_D3D12_ESTIMATE_VRAM_DLSSG(
    NVSDK_NGX_Parameter* InParams,
    uint32_t mvecDepthWidth, uint32_t mvecDepthHeight,
    uint32_t colorWidth, uint32_t colorHeight,
    uint32_t colorBufferFormat,
    uint32_t mvecBufferFormat, uint32_t depthBufferFormat,
    uint32_t hudLessBufferFormat, uint32_t uiBufferFormat,
    size_t* estimatedVRAMInBytes
)
{
    void* Callback = NULL;
    NVSDK_NGX_Parameter_GetVoidPointer(InParams, NVSDK_NGX_Parameter_DLSSGEstimateVRAMCallback, &Callback);
    if (!Callback)
    {
        // Possible reasons for this:
        // - Installed feature is out of date and does not support the feature we need
        // - You used NVSDK_NGX_AllocateParameters() for creating InParams. Try using NVSDK_NGX_GetCapabilityParameters() instead
        return NVSDK_NGX_Result_FAIL_OutOfDate;
    }

    NVSDK_NGX_Result Res = NVSDK_NGX_Result_Success;
    PFN_NVSDK_NGX_DLSSG_EstimateVRAMCallback PFNCallback = (PFN_NVSDK_NGX_DLSSG_EstimateVRAMCallback)Callback;
    Res = PFNCallback(mvecDepthWidth, mvecDepthHeight, colorWidth, colorHeight,
        colorBufferFormat, mvecBufferFormat, depthBufferFormat, hudLessBufferFormat, uiBufferFormat, estimatedVRAMInBytes);
    if (NVSDK_NGX_FAILED(Res))
    {
        return Res;
    }

    return NVSDK_NGX_Result_Success;
}

#endif // NVSDK_NGX_HELPERS_DLSSG_H
