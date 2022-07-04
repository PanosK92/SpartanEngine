// This file is part of the FidelityFX SDK.
//
// Copyright (c) 2022 Advanced Micro Devices, Inc. All rights reserved.
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

#if !defined(FFX_FSR2_COMMON_H)
#define FFX_FSR2_COMMON_H

#if defined(FFX_CPU) || defined(FFX_GPU)
//Locks
#define LOCK_LIFETIME_REMAINING 0
#define LOCK_TEMPORAL_LUMA 1
#define LOCK_TRUST 2
#endif // #if defined(FFX_CPU) || defined(FFX_GPU)

#if defined(FFX_GPU)
FFX_STATIC const FfxFloat32 FSR2_EPSILON = 1e-03f;
FFX_STATIC const FfxFloat32 FSR2_TONEMAP_EPSILON = 1e-03f;
FFX_STATIC const FfxFloat32 FSR2_FLT_MAX = 3.402823466e+38f;
FFX_STATIC const FfxFloat32 FSR2_FLT_MIN = 1.175494351e-38f;

// treat vector truncation warnings as errors
#pragma warning(error: 3206)

// suppress warnings
#pragma warning(disable: 3205)  // conversion from larger type to smaller
#pragma warning(disable: 3571)  // in ffxPow(f, e), f could be negative

// Reconstructed depth usage
FFX_STATIC const FfxFloat32 reconstructedDepthBilinearWeightThreshold = 0.05f;

// Accumulation
FFX_STATIC const FfxFloat32 averageLanczosWeightPerFrame = 0.74f; // Average lanczos weight for jitter accumulated samples
FFX_STATIC const FfxFloat32 accumulationMaxOnMotion = 4.0f;

// Auto exposure
FFX_STATIC const FfxFloat32 resetAutoExposureAverageSmoothing = 1e8f;

struct LockState
{
    FfxBoolean NewLock; //Set for both unique new and re-locked new
    FfxBoolean WasLockedPrevFrame; //Set to identify if the pixel was already locked (relock)
};

FFX_MIN16_F GetNormalizedRemainingLockLifetime(FFX_MIN16_F3 fLockStatus)
{
    const FfxFloat32 fTrust = fLockStatus[LOCK_TRUST];

    return FFX_MIN16_F(((ffxSaturate(fLockStatus[LOCK_LIFETIME_REMAINING] - LockInitialLifetime()) / LockInitialLifetime())) * fTrust);
}

LOCK_STATUS_T CreateNewLockSample()
{
    LOCK_STATUS_T fLockStatus = LOCK_STATUS_T(0, 0, 0);

    fLockStatus[LOCK_TRUST] = LOCK_STATUS_F1(1);

    return fLockStatus;
}

void KillLock(FFX_PARAMETER_INOUT FFX_MIN16_F3 fLockStatus)
{
    fLockStatus[LOCK_LIFETIME_REMAINING] = FFX_MIN16_F(0);
}

#define SPLIT_LEFT 0
#define SPLIT_RIGHT 1
#ifndef SPLIT_SHADER
#define SPLIT_SHADER SPLIT_RIGHT
#endif

#if FFX_HALF

#define UPSAMPLE_F  FfxFloat16   
#define UPSAMPLE_F2 FfxFloat16x2   
#define UPSAMPLE_F3 FfxFloat16x3   
#define UPSAMPLE_F4 FfxFloat16x4   
#define UPSAMPLE_I  FfxInt16  
#define UPSAMPLE_I2 FfxInt16x2  
#define UPSAMPLE_I3 FfxInt16x3  
#define UPSAMPLE_I4 FfxInt16x4  
#define UPSAMPLE_U  FfxUInt16   
#define UPSAMPLE_U2 FfxUInt16x2   
#define UPSAMPLE_U3 FfxUInt16x3   
#define UPSAMPLE_U4 FfxUInt16x4
#define UPSAMPLE_F2_BROADCAST(X) FFX_BROADCAST_MIN_FLOAT16X2(X)
#define UPSAMPLE_F3_BROADCAST(X) FFX_BROADCAST_MIN_FLOAT16X3(X)
#define UPSAMPLE_F4_BROADCAST(X) FFX_BROADCAST_MIN_FLOAT16X4(X)  
#define UPSAMPLE_I2_BROADCAST(X) FFX_BROADCAST_MIN_INT16X2(X)
#define UPSAMPLE_I3_BROADCAST(X) FFX_BROADCAST_MIN_INT16X3(X)
#define UPSAMPLE_I4_BROADCAST(X) FFX_BROADCAST_MIN_INT16X4(X)   
#define UPSAMPLE_U2_BROADCAST(X) FFX_BROADCAST_MIN_UINT16X2(X)
#define UPSAMPLE_U3_BROADCAST(X) FFX_BROADCAST_MIN_UINT16X3(X)
#define UPSAMPLE_U4_BROADCAST(X) FFX_BROADCAST_MIN_UINT16X4(X)   

#else //FFX_HALF

#define UPSAMPLE_F  FfxFloat32    
#define UPSAMPLE_F2 FfxFloat32x2   
#define UPSAMPLE_F3 FfxFloat32x3   
#define UPSAMPLE_F4 FfxFloat32x4   
#define UPSAMPLE_I  FfxInt32    
#define UPSAMPLE_I2 FfxInt32x2   
#define UPSAMPLE_I3 FfxInt32x3   
#define UPSAMPLE_I4 FfxInt32x4   
#define UPSAMPLE_U  FfxUInt32    
#define UPSAMPLE_U2 FfxUInt32x2   
#define UPSAMPLE_U3 FfxUInt32x3   
#define UPSAMPLE_U4 FfxUInt32x4   
#define UPSAMPLE_F2_BROADCAST(X) FFX_BROADCAST_FLOAT32X2(X)
#define UPSAMPLE_F3_BROADCAST(X) FFX_BROADCAST_FLOAT32X3(X)
#define UPSAMPLE_F4_BROADCAST(X) FFX_BROADCAST_FLOAT32X4(X)  
#define UPSAMPLE_I2_BROADCAST(X) FFX_BROADCAST_INT32X2(X)
#define UPSAMPLE_I3_BROADCAST(X) FFX_BROADCAST_INT32X3(X)
#define UPSAMPLE_I4_BROADCAST(X) FFX_BROADCAST_INT32X4(X)   
#define UPSAMPLE_U2_BROADCAST(X) FFX_BROADCAST_UINT32X2(X)
#define UPSAMPLE_U3_BROADCAST(X) FFX_BROADCAST_UINT32X3(X)
#define UPSAMPLE_U4_BROADCAST(X) FFX_BROADCAST_UINT32X4(X) 

#endif //FFX_HALF

struct RectificationBoxData
{
    UPSAMPLE_F3 boxCenter;
    UPSAMPLE_F3 boxVec;
    UPSAMPLE_F3 aabbMin;
    UPSAMPLE_F3 aabbMax;
};

struct RectificationBox
{
    RectificationBoxData data_;
    UPSAMPLE_F fBoxCenterWeight;    
};

void RectificationBoxReset(FFX_PARAMETER_INOUT RectificationBox rectificationBox, const UPSAMPLE_F3 initialColorSample)
{
    rectificationBox.fBoxCenterWeight = UPSAMPLE_F(0.0);

    rectificationBox.data_.boxCenter = UPSAMPLE_F3_BROADCAST(0);
    rectificationBox.data_.boxVec = UPSAMPLE_F3_BROADCAST(0);
    rectificationBox.data_.aabbMin = initialColorSample;
    rectificationBox.data_.aabbMax = initialColorSample;
}

void RectificationBoxAddSample(FFX_PARAMETER_INOUT RectificationBox rectificationBox, const UPSAMPLE_F3 colorSample, const UPSAMPLE_F fSampleWeight)
{
    rectificationBox.data_.aabbMin = ffxMin(rectificationBox.data_.aabbMin, colorSample);
    rectificationBox.data_.aabbMax = ffxMax(rectificationBox.data_.aabbMax, colorSample);
    UPSAMPLE_F3 weightedSample = colorSample * fSampleWeight;
    rectificationBox.data_.boxCenter += weightedSample;
    rectificationBox.data_.boxVec += colorSample * weightedSample;
    rectificationBox.fBoxCenterWeight += fSampleWeight;
}

void RectificationBoxComputeVarianceBoxData(FFX_PARAMETER_INOUT RectificationBox rectificationBox)
{
    rectificationBox.fBoxCenterWeight = (abs(rectificationBox.fBoxCenterWeight) > UPSAMPLE_F(FSR2_EPSILON) ? rectificationBox.fBoxCenterWeight : UPSAMPLE_F(1.f));
    rectificationBox.data_.boxCenter /= rectificationBox.fBoxCenterWeight;
    rectificationBox.data_.boxVec /= rectificationBox.fBoxCenterWeight;
    UPSAMPLE_F3 stdDev = sqrt(abs(rectificationBox.data_.boxVec - rectificationBox.data_.boxCenter * rectificationBox.data_.boxCenter));
    rectificationBox.data_.boxVec = stdDev;
}

RectificationBoxData RectificationBoxGetData(FFX_PARAMETER_INOUT RectificationBox rectificationBox)
{
    return rectificationBox.data_;
}

FfxFloat32x3 SafeRcp3(FfxFloat32x3 v)
{
    return (all(FFX_NOT_EQUAL(v, FFX_BROADCAST_FLOAT32X3(0)))) ? (FFX_BROADCAST_FLOAT32X3(1.0f) / v) : FFX_BROADCAST_FLOAT32X3(0.0f);
}

FfxFloat32 MinDividedByMax(const FfxFloat32 v0, const FfxFloat32 v1)
{
    const FfxFloat32 m = ffxMax(v0, v1);
    return m != 0 ? ffxMin(v0, v1) / m : 0;
}

#if FFX_HALF
FFX_MIN16_F MinDividedByMax(const FFX_MIN16_F v0, const FFX_MIN16_F v1)
{
    const FFX_MIN16_F m = ffxMax(v0, v1);
    return m != FFX_MIN16_F(0) ? ffxMin(v0, v1) / m : FFX_MIN16_F(0);
}
#endif

FfxFloat32 MaxDividedByMin(const FfxFloat32 v0, const FfxFloat32 v1)
{
    const FfxFloat32 m = ffxMin(v0, v1);
    return m != 0 ? ffxMax(v0, v1) / m : 0;
}

FFX_MIN16_F3 RGBToYCoCg_16(FFX_MIN16_F3 fRgb)
{
    FFX_MIN16_F3 fYCoCg;
    fYCoCg.x = dot(fRgb.rgb, FFX_MIN16_F3(+0.25f, +0.50f, +0.25f));
    fYCoCg.y = dot(fRgb.rgb, FFX_MIN16_F3(+0.50f, +0.00f, -0.50f));
    fYCoCg.z = dot(fRgb.rgb, FFX_MIN16_F3(-0.25f, +0.50f, -0.25f));
    return fYCoCg;
}

FFX_MIN16_F3 RGBToYCoCg_V2_16(FFX_MIN16_F3 fRgb)
{
    FFX_MIN16_F a = fRgb.g * FFX_MIN16_F(0.5f);
    FFX_MIN16_F b = (fRgb.r + fRgb.b) * FFX_MIN16_F(0.25f);
    FFX_MIN16_F3 fYCoCg;
    fYCoCg.x = a + b;
    fYCoCg.y = (fRgb.r - fRgb.b) * FFX_MIN16_F(0.5f);
    fYCoCg.z = a - b;
    return fYCoCg;
}

FfxFloat32x3 YCoCgToRGB(FfxFloat32x3 fYCoCg)
{
    FfxFloat32x3 fRgb;
    FfxFloat32 tmp = fYCoCg.x - fYCoCg.z / 2.0;
    fRgb.g = fYCoCg.z + tmp;
    fRgb.b = tmp - fYCoCg.y / 2.0;
    fRgb.r = fRgb.b + fYCoCg.y;
    return fRgb;
}
#if FFX_HALF
FFX_MIN16_F3 YCoCgToRGB(FFX_MIN16_F3 fYCoCg)
{
    FFX_MIN16_F3 fRgb;
    FFX_MIN16_F tmp = fYCoCg.x - fYCoCg.z * FFX_MIN16_F(0.5f);
    fRgb.g = fYCoCg.z + tmp;
    fRgb.b = tmp - fYCoCg.y * FFX_MIN16_F(0.5f);
    fRgb.r = fRgb.b + fYCoCg.y;
    return fRgb;
}
#endif

FfxFloat32x3 RGBToYCoCg(FfxFloat32x3 fRgb)
{
    FfxFloat32x3 fYCoCg;
    fYCoCg.y = fRgb.r - fRgb.b;
    FfxFloat32 tmp = fRgb.b + fYCoCg.y / 2.0;
    fYCoCg.z = fRgb.g - tmp;
    fYCoCg.x = tmp + fYCoCg.z / 2.0;
    return fYCoCg;
}
#if FFX_HALF
FFX_MIN16_F3 RGBToYCoCg(FFX_MIN16_F3 fRgb)
{
    FFX_MIN16_F3 fYCoCg;
    fYCoCg.y = fRgb.r - fRgb.b;
    FFX_MIN16_F tmp = fRgb.b + fYCoCg.y * FFX_MIN16_F(0.5f);
    fYCoCg.z = fRgb.g - tmp;
    fYCoCg.x = tmp + fYCoCg.z * FFX_MIN16_F(0.5f);
    return fYCoCg;
}
#endif

FfxFloat32x3 RGBToYCoCg_V2(FfxFloat32x3 fRgb)
{
    FfxFloat32 a = fRgb.g * 0.5f;
    FfxFloat32 b = (fRgb.r + fRgb.b) * 0.25f;
    FfxFloat32x3 fYCoCg;
    fYCoCg.x = a + b;
    fYCoCg.y = (fRgb.r - fRgb.b) * 0.5f;
    fYCoCg.z = a - b;
    return fYCoCg;
}

FfxFloat32 RGBToLuma(FfxFloat32x3 fLinearRgb)
{
    return dot(fLinearRgb, FfxFloat32x3(0.2126f, 0.7152f, 0.0722f));
}

FfxFloat32 RGBToPerceivedLuma(FfxFloat32x3 fLinearRgb)
{
    FfxFloat32 fLuminance = RGBToLuma(fLinearRgb);

    FfxFloat32 fPercievedLuminance = 0;
    if (fLuminance <= 216.0f / 24389.0f) {
        fPercievedLuminance = fLuminance * (24389.0f / 27.0f);
    } else {
        fPercievedLuminance = ffxPow(fLuminance, 1.0f / 3.0f) * 116.0f - 16.0f;
    }

    return fPercievedLuminance * 0.01f;
}


FfxFloat32x3 Tonemap(FfxFloat32x3 fRgb)
{
    return fRgb / (ffxMax(ffxMax(0.f, fRgb.r), ffxMax(fRgb.g, fRgb.b)) + 1.f).xxx;
}

FfxFloat32x3 InverseTonemap(FfxFloat32x3 fRgb)
{
    return fRgb / ffxMax(FSR2_TONEMAP_EPSILON, 1.f - ffxMax(fRgb.r, ffxMax(fRgb.g, fRgb.b))).xxx;
}

#if FFX_HALF
FFX_MIN16_F3 Tonemap(FFX_MIN16_F3 fRgb)
{
    return fRgb / (ffxMax(ffxMax(FFX_MIN16_F(0.f), fRgb.r), ffxMax(fRgb.g, fRgb.b)) + FFX_MIN16_F(1.f)).xxx;
}

FFX_MIN16_F3 InverseTonemap(FFX_MIN16_F3 fRgb)
{
    return fRgb / ffxMax(FFX_MIN16_F(FSR2_TONEMAP_EPSILON), FFX_MIN16_F(1.f) - ffxMax(fRgb.r, ffxMax(fRgb.g, fRgb.b))).xxx;
}

FFX_MIN16_I2 ClampLoad(FFX_MIN16_I2 iPxSample, FFX_MIN16_I2 iPxOffset, FFX_MIN16_I2 iTextureSize)
{
    return clamp(iPxSample + iPxOffset, FFX_MIN16_I2(0, 0), iTextureSize - FFX_MIN16_I2(1, 1));
}
#endif

FfxInt32x2 ClampLoad(FfxInt32x2 iPxSample, FfxInt32x2 iPxOffset, FfxInt32x2 iTextureSize)
{
    return clamp(iPxSample + iPxOffset, FfxInt32x2(0, 0), iTextureSize - FfxInt32x2(1, 1));
}

FfxBoolean IsOnScreen(FFX_MIN16_I2 pos, FFX_MIN16_I2 size)
{
    return all(FFX_GREATER_THAN_EQUAL(pos, FFX_BROADCAST_MIN_FLOAT16X2(0))) && all(FFX_LESS_THAN(pos, size));
}

FfxFloat32 ComputeAutoExposureFromLavg(FfxFloat32 Lavg)
{
    Lavg = exp(Lavg);

    const FfxFloat32 S = 100.0f; //ISO arithmetic speed
    const FfxFloat32 K = 12.5f;
    FfxFloat32 ExposureISO100 = log2((Lavg * S) / K);

    const FfxFloat32 q = 0.65f;
    FfxFloat32 Lmax = (78.0f / (q * S)) * ffxPow(2.0f, ExposureISO100);

    return 1 / Lmax;
}
#endif // #if defined(FFX_GPU)

#endif //!defined(FFX_FSR2_COMMON_H)