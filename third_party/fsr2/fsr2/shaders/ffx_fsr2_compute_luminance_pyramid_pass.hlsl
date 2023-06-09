// This file is part of the FidelityFX SDK.
//
// Copyright (c) 2022-2023 Advanced Micro Devices, Inc. All rights reserved.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#define FSR2_BIND_SRV_INPUT_COLOR                     0
#define FSR2_BIND_UAV_SPD_GLOBAL_ATOMIC               0
#define FSR2_BIND_UAV_EXPOSURE_MIP_LUMA_CHANGE        1
#define FSR2_BIND_UAV_EXPOSURE_MIP_5                  2
#define FSR2_BIND_UAV_AUTO_EXPOSURE                   3
#define FSR2_BIND_CB_FSR2                             0
#define FSR2_BIND_CB_SPD                              1

#include "ffx_fsr2_callbacks_hlsl.h"
#include "ffx_fsr2_common.h"

#if defined(FSR2_BIND_CB_SPD)
    cbuffer cbSPD : FFX_FSR2_DECLARE_CB(FSR2_BIND_CB_SPD) {

        FfxUInt32   mips;
        FfxUInt32   numWorkGroups;
        FfxUInt32x2 workGroupOffset;
        FfxUInt32x2 renderSize;
    };

    FfxUInt32 MipCount()
    {
        return mips;
    }

    FfxUInt32 NumWorkGroups()
    {
        return numWorkGroups;
    }

    FfxUInt32x2 WorkGroupOffset()
    {
        return workGroupOffset;
    }

    FfxUInt32x2 SPD_RenderSize()
    {
        return renderSize;
    }
#endif


FfxFloat32x2 SPD_LoadExposureBuffer()
{
    return rw_auto_exposure[FfxInt32x2(0,0)];
}

void SPD_SetExposureBuffer(FfxFloat32x2 value)
{
    rw_auto_exposure[FfxInt32x2(0,0)] = value;
}

FfxFloat32x4 SPD_LoadMipmap5(FfxInt32x2 iPxPos)
{
    return FfxFloat32x4(rw_img_mip_5[iPxPos], 0, 0, 0);
}

void SPD_SetMipmap(FfxInt32x2 iPxPos, FfxInt32 slice, FfxFloat32 value)
{
    switch (slice)
    {
    case FFX_FSR2_SHADING_CHANGE_MIP_LEVEL:
        rw_img_mip_shading_change[iPxPos] = value;
        break;
    case 5:
        rw_img_mip_5[iPxPos] = value;
        break;
    default:

        // avoid flattened side effect
#if defined(FSR2_BIND_UAV_EXPOSURE_MIP_LUMA_CHANGE) || defined(FFX_INTERNAL)
        rw_img_mip_shading_change[iPxPos] = rw_img_mip_shading_change[iPxPos];
#elif defined(FSR2_BIND_UAV_EXPOSURE_MIP_5) || defined(FFX_INTERNAL)
        rw_img_mip_5[iPxPos] = rw_img_mip_5[iPxPos];
#endif
        break;
    }
}

void SPD_IncreaseAtomicCounter(inout FfxUInt32 spdCounter)
{
   InterlockedAdd(rw_spd_global_atomic[FfxInt32x2(0,0)], 1, spdCounter);
}

void SPD_ResetAtomicCounter()
{
    rw_spd_global_atomic[FfxInt32x2(0,0)] = 0;
}

#include "ffx_fsr2_compute_luminance_pyramid.h"

#ifndef FFX_FSR2_THREAD_GROUP_WIDTH
#define FFX_FSR2_THREAD_GROUP_WIDTH 256
#endif // #ifndef FFX_FSR2_THREAD_GROUP_WIDTH
#ifndef FFX_FSR2_THREAD_GROUP_HEIGHT
#define FFX_FSR2_THREAD_GROUP_HEIGHT 1
#endif // #ifndef FFX_FSR2_THREAD_GROUP_HEIGHT
#ifndef FFX_FSR2_THREAD_GROUP_DEPTH
#define FFX_FSR2_THREAD_GROUP_DEPTH 1
#endif // #ifndef FFX_FSR2_THREAD_GROUP_DEPTH
#ifndef FFX_FSR2_NUM_THREADS
#define FFX_FSR2_NUM_THREADS [numthreads(FFX_FSR2_THREAD_GROUP_WIDTH, FFX_FSR2_THREAD_GROUP_HEIGHT, FFX_FSR2_THREAD_GROUP_DEPTH)]
#endif // #ifndef FFX_FSR2_NUM_THREADS

FFX_FSR2_NUM_THREADS
FFX_FSR2_EMBED_CB2_ROOTSIG_CONTENT
void CS(uint3 WorkGroupId : SV_GroupID, uint LocalThreadIndex : SV_GroupIndex)
{
    ComputeAutoExposure(WorkGroupId, LocalThreadIndex);
}
