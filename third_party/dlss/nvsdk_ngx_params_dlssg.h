#pragma once
/*
* Copyright (c) 2021-2024 NVIDIA CORPORATION.  All rights reserved.
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


#ifndef NVSDK_NGX_PARAMS_DLSSG_H
#define NVSDK_NGX_PARAMS_DLSSG_H

#include <stdint.h> // for uint64_t

#include "nvsdk_ngx.h" // DX11/12 here
#include "nvsdk_ngx_defs.h"

typedef struct NVSDK_NGX_DLSSG_Create_Params
{
    unsigned int Width;
    unsigned int Height;
    unsigned int NativeBackbufferFormat;
    unsigned int RenderWidth;
    unsigned int RenderHeight;
    bool DynamicResolutionScaling;
} NVSDK_NGX_DLSSG_Create_Params;

typedef struct NVSDK_NGX_DLSSG_Opt_Eval_Params
{
    //! Number of intermediate frames to generate between rendered frames
    //! (1 = 2x, 2 = 3x, etc.)
    unsigned int multiFrameCount = 1;
    //! The index (starting at 1) of the intermediate frame being generated, up
    //! to multiFrameCount (inclusive)
    unsigned int multiFrameIndex = 1;

    //! IMPORTANT: All matrices should NOT contain temporal AA jitter offset
    //! Specifies matrix transformation from the camera view to the clip space.
    float cameraViewToClip[4][4];
    //! Specifies matrix transformation from the clip space to the camera view space.
    float clipToCameraView[4][4];
    //! Specifies matrix transformation describing lens distortion in clip space.
    float clipToLensClip[4][4];
    //! Specifies matrix transformation from the current clip to the previous clip space.
    float clipToPrevClip[4][4];
    //! Specifies matrix transformation from the previous clip to the current clip space.
    float prevClipToClip[4][4];

    //! Specifies clip space jitter offset
    float jitterOffset[2];
    //! Specifies scale factors used to normalize motion vectors (so the values are in [-1,1] range)
    float mvecScale[2];
    //! Specifies camera pinhole offset.
    float cameraPinholeOffset[2];
    //! Specifies camera position in world space.
    float cameraPos[3];
    //! Specifies camera up vector in world space.
    float cameraUp[3];
    //! Specifies camera right vector in world space.
    float cameraRight[3];
    //! Specifies camera forward vector in world space.
    float cameraFwd[3];

    //! Specifies camera near view plane distance.
    float cameraNear;
    //! Specifies camera far view plane distance.
    float cameraFar;
    //! Specifies camera field of view in radians.
    float cameraFOV;
    //! Specifies camera aspect ratio defined as view space width divided by height.
    float cameraAspectRatio;

    //! Specifies if tagged color buffers are full HDR (rendering to an HDR monitor) or not
    bool colorBuffersHDR;
    //! Specifies if depth values are inverted (value closer to the camera is higher) or not.
    bool depthInverted;
    //! Specifies if camera motion is included in the MVec buffer.
    bool cameraMotionIncluded;
    //! Specifies if previous frame has no connection to the current one (motion vectors are invalid)
    bool reset;
    //! Specifies if Automode overrode the input Reset flag to true.
    bool automodeOverrideReset;
    //! Specifies if application is not currently rendering game frames (paused in menu, playing video cut-scenes)
    bool notRenderingGameFrames;
    //! Specifies if orthographic projection is used or not.
    bool orthoProjection;

    //! Specifies which value represents an invalid (un-initialized) value in the motion vectors buffer
    float motionVectorsInvalidValue;
    //! Specifies if motion vectors are already dilated or not.
    bool motionVectorsDilated;

    //! Specifies whether or not fullscreen menu detection should be run
    bool menuDetectionEnabled;

    NVSDK_NGX_Coordinates mvecsSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions mvecsSubrectSize = { 0, 0 };
    NVSDK_NGX_Coordinates depthSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions depthSubrectSize = { 0, 0 };

    NVSDK_NGX_Coordinates hudLessSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions hudLessSubrectSize = { 0, 0 };
    NVSDK_NGX_Coordinates uiSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions uiSubrectSize = { 0, 0 };
    NVSDK_NGX_Coordinates uiAlphaSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions uiAlphaSubrectSize = { 0, 0 };

    NVSDK_NGX_Coordinates bidirectionalDistFieldSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions bidirectionalDistFieldSubrectSize = { 0, 0 };
    NVSDK_NGX_PrecisionInfo bidirectionalDistFieldPrecisionInfo = { 0, 0.0f, 0.0f };

    //! Optional heuristic that specifies the minimum depth difference between two objects in screen-space.
    //! See comment above NVSDK_NGX_DLSSG_Parameter_MinRelativeLinearDepthObjectSeparation for details.
    float minRelativeLinearDepthObjectSeparation = 40.0f;

    NVSDK_NGX_Coordinates backbufferSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions  backbufferSubrectSize = { 0, 0 };

    NVSDK_NGX_Coordinates outputInterpSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions  outputInterpSubrectSize = { 0, 0 };
    NVSDK_NGX_Coordinates outputRealSubrectBase = { 0, 0 };
    NVSDK_NGX_Dimensions  outputRealSubrectSize = { 0, 0 };
} NVSDK_NGX_DLSSG_Opt_Eval_Params;

#endif // NVSDK_NGX_PARAMS_DLSSG_H
