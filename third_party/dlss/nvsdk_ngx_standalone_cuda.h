/*
 * SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */

#ifndef NVSDK_NGX_STANDALONE_CUDA_H
#define NVSDK_NGX_STANDALONE_CUDA_H

#if defined(NVSDK_NGX_HEADER_ONLY)

#include "nvsdk_ngx_loader.h"
#include "nvsdk_ngx_standalone_common.h"

#ifdef __cplusplus
extern "C"
{
#endif

typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CORE_CUDA_Init)(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, NVSDK_NGX_Version InSDKVersion);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CORE_CUDA_Init_Ext)(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, NVSDK_NGX_Version InSDKVersion, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CORE_CUDA_Init_Ext1)(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, NVSDK_NGX_CUDADevice *InDevice, NVSDK_NGX_Version InSDKVersion, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CORE_CUDA_Init_ProjectID)(const char *InProjectId, NVSDK_NGX_EngineType InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, NVSDK_NGX_Version InSDKVersion, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_Shutdown)();
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_Shutdown1)(NVSDK_NGX_CUDADevice *pDevice, unsigned int &outNDevicesLeft);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_GetParameters)(NVSDK_NGX_Parameter **OutParameters);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_GetScratchBufferSize)(NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter *InParameters, size_t *OutSizeInBytes);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_CreateFeature)(NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_CreateFeature1)(NVSDK_NGX_CUDADevice *InDevice, NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_EvaluateFeature)(const NVSDK_NGX_Handle *InFeature, const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_ReleaseFeature)(NVSDK_NGX_Handle *InHandle);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_GetFeatureRequirements)(int CudaDevice, const NVSDK_NGX_FeatureDiscoveryInfo *FeatureDiscoveryInfo, NVSDK_NGX_FeatureRequirement *OutSupported);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_AllocateParameters)(NVSDK_NGX_Parameter **OutParameters);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_GetCapabilityParameters)(NVSDK_NGX_Parameter **OutParameters);
typedef NVSDK_NGX_Result(NVSDK_CONV *PFN_NVSDK_NGX_CUDA_DestroyParameters)(NVSDK_NGX_Parameter *InParameters);

#define CUDA_IS_INITED                                                   \
    ((Init != nullptr || Init_Ext != nullptr || Init_Ext1 != nullptr) && \
     (Shutdown != nullptr || Shutdown1 != nullptr) &&                    \
     (GetParameters != nullptr || GetCapabilityParameters != nullptr) && \
     (CreateFeature != nullptr || CreateFeature1 != nullptr) &&          \
     EvaluateFeature != nullptr &&                                       \
     ReleaseFeature != nullptr)

#define CUDA_FNS                   \
    CORE_CUDA, Init,               \
    CORE_CUDA, Init_Ext,           \
    CORE_CUDA, Init_Ext1,          \
    CORE_CUDA, Init_ProjectID,     \
    CUDA, Shutdown,                \
    CUDA, Shutdown1,               \
    CUDA, GetParameters,           \
    CUDA, GetScratchBufferSize,    \
    CUDA, CreateFeature,           \
    CUDA, CreateFeature1,          \
    CUDA, EvaluateFeature,         \
    CUDA, ReleaseFeature,          \
    CUDA, GetFeatureRequirements,  \
    CUDA, AllocateParameters,      \
    CUDA, GetCapabilityParameters, \
    CUDA, DestroyParameters

#define NVSDK_NGX_BACKEND_NAME CUDA

NVSDK_NGX_DEFINE_API(CUDA_IS_INITED, CUDA_FNS)
NVSDK_NGX_DEFINE_INIT_COMMON(CUDA_FNS)

NVSDK_NGX_Result NVSDK_NGX_CUDA_Init(unsigned long long InApplicationId, const wchar_t *InApplicationDataPath, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion)
{
    NGXLock lock(g_sdkLibMutexCUDA);
    NVSDK_NGX_Result res = NVSDK_NGX_CUDA_Init_Common();
    if (res != NVSDK_NGX_Result_Success)
    {
        return res;
    }

    if (GContext.API.Init_Ext)
    {
        return GContext.API.Init_Ext(InApplicationId, InApplicationDataPath, InSDKVersion, InFeatureInfo);
    }
    else
    {
        if (InFeatureInfo != nullptr && InFeatureInfo->PathListInfo.Path != nullptr && InFeatureInfo->PathListInfo.Length > 0)
        {
            // App has sent a non-empty list of extra paths to check for the feature binary but Init_Ext entrypoint to Core doesn't exist.
            // Fail with OUT_OF_DATE.
            return  NVSDK_NGX_Result_FAIL_OutOfDate;
        }
        return GContext.API.Init(InApplicationId, InApplicationDataPath, InSDKVersion);
    }
}

NVSDK_NGX_Result NVSDK_NGX_CUDA_Init1(unsigned long long InApplicationId, const wchar_t* InApplicationDataPath, NVSDK_NGX_CUDADevice* InDevice, const NVSDK_NGX_FeatureCommonInfo* InFeatureInfo, NVSDK_NGX_Version InSDKVersion)
{
    NGXLock lock(g_sdkLibMutexCUDA);
    NVSDK_NGX_Result res = NVSDK_NGX_CUDA_Init_Common();
    if (res != NVSDK_NGX_Result_Success)
    {
        return res;
    }
    if (!InDevice)
    {
        return NVSDK_NGX_Result_FAIL_InvalidParameter;
    }

    if (GContext.API.Init_Ext1)
    {
        return GContext.API.Init_Ext1(InApplicationId, InApplicationDataPath, InDevice, InSDKVersion, InFeatureInfo);
    }
    // Do not failback as Init_Ext1 is needed for multi-threaded which is the reason for NVSDK_NGX_CUDA_Init1.
    return  NVSDK_NGX_Result_FAIL_OutOfDate;
}

NVSDK_NGX_Result NVSDK_NGX_CUDA_Init_with_ProjectID(const char *InProjectId, NVSDK_NGX_EngineType InEngineType, const char *InEngineVersion, const wchar_t *InApplicationDataPath, const NVSDK_NGX_FeatureCommonInfo *InFeatureInfo, NVSDK_NGX_Version InSDKVersion)
{
    NGXLock lock(g_sdkLibMutexCUDA);
    NVSDK_NGX_Result res = NVSDK_NGX_CUDA_Init_Common();
    if (res != NVSDK_NGX_Result_Success)
    {
        return res;
    }

    if (GContext.API.Init_ProjectID)
    {
        return GContext.API.Init_ProjectID(InProjectId, InEngineType, InEngineVersion, InApplicationDataPath, InSDKVersion, InFeatureInfo);
    }
    else
    {
        // New SDK, but old driver, with no support for project ID
        return  NVSDK_NGX_Result_FAIL_OutOfDate;
    }
}

NVSDK_NGX_Result NVSDK_NGX_CUDA_Shutdown1(NVSDK_NGX_CUDADevice *pDevice)
{
    NGXLock lock(g_sdkLibMutexCUDA);

    NVSDK_NGX_Result result = NVSDK_NGX_Result_Success;
    unsigned int nDevicesLeft = 0;
    if (pDevice)
    {
        if (!GContext.API.Shutdown1) return NVSDK_NGX_Result_FAIL_NotInitialized;
        result = GContext.API.Shutdown1(pDevice, nDevicesLeft);
    }
    else
    {
        if (GContext.API.Shutdown1)
            result = GContext.API.Shutdown1(nullptr, nDevicesLeft);
        else if (GContext.API.Shutdown)
            result = GContext.API.Shutdown();
        else return NVSDK_NGX_Result_FAIL_NotInitialized;
    }
    if (result == NVSDK_NGX_Result_Success && nDevicesLeft == 0)
    {
        GContext = NGX_Context_CUDA_Lib();
    }
    return result;
}

NVSDK_NGX_Result NVSDK_NGX_CUDA_Shutdown()
{
    return NVSDK_NGX_CUDA_Shutdown1(nullptr);
}

NVSDK_NGX_DEFINE_WRAPPER(GetParameters, (NVSDK_NGX_Parameter **OutParameters), (OutParameters))
NVSDK_NGX_DEFINE_WRAPPER(AllocateParameters, (NVSDK_NGX_Parameter **OutParameters), (OutParameters))
NVSDK_NGX_DEFINE_WRAPPER(GetCapabilityParameters, (NVSDK_NGX_Parameter **OutParameters), (OutParameters))
NVSDK_NGX_DEFINE_WRAPPER(DestroyParameters, (NVSDK_NGX_Parameter* InParameters), (InParameters))
NVSDK_NGX_DEFINE_WRAPPER(GetScratchBufferSize, (NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter *InParameters, size_t *OutSizeInBytes), (InFeatureId, InParameters, OutSizeInBytes))
NVSDK_NGX_DEFINE_WRAPPER(CreateFeature, (NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle), (InFeatureId, InParameters, OutHandle))
NVSDK_NGX_DEFINE_WRAPPER(CreateFeature1, (NVSDK_NGX_CUDADevice *InDevice, NVSDK_NGX_Feature InFeatureId, const NVSDK_NGX_Parameter *InParameters, NVSDK_NGX_Handle **OutHandle), (InDevice, InFeatureId, InParameters, OutHandle))
NVSDK_NGX_DEFINE_WRAPPER(ReleaseFeature, (NVSDK_NGX_Handle *InHandle), (InHandle))

// GetFeatureRequirements can be called before Init, so it loads the library
// on demand using a zero LUID (registry fallback), matching the behavior in
// nvsdk_ngx_cuda_lib.cpp:420-441.
NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_CUDA_GetFeatureRequirements(
    int CudaDevice, const NVSDK_NGX_FeatureDiscoveryInfo *FeatureDiscoveryInfo,
    NVSDK_NGX_FeatureRequirement *OutSupported)
{
    if (!GLib)
    {
        NVSDK_NGX_LUID luid = {};
        GLib = NGXLoadCoreLibrary(luid);
        if (!GLib) return NVSDK_NGX_Result_FAIL_PlatformError;
        GContext.API.GetFeatureRequirements =
            (PFN_NVSDK_NGX_CUDA_GetFeatureRequirements)NGXGetLibrarySymbol(GLib, "NVSDK_NGX_CUDA_GetFeatureRequirements");
    }
    if (!GContext.API.GetFeatureRequirements) return NVSDK_NGX_Result_FAIL_NotImplemented;
    return GContext.API.GetFeatureRequirements(CudaDevice, FeatureDiscoveryInfo, OutSupported);
}

NVSDK_NGX_DEFINE_WRAPPER(EvaluateFeature, (const NVSDK_NGX_Handle *InHandle, const NVSDK_NGX_Parameter *InParameters, PFN_NVSDK_NGX_ProgressCallback InCallback), (InHandle, InParameters, InCallback))

NVSDK_NGX_Result NVSDK_CONV NVSDK_NGX_CUDA_EvaluateFeature_C(
    const NVSDK_NGX_Handle *InHandle, const NVSDK_NGX_Parameter *InParameters,
    PFN_NVSDK_NGX_ProgressCallback_C InCallback)
{
    if (!GContext.API.isInited()) return NVSDK_NGX_Result_FAIL_NotInitialized;
    if (!GContext.API.EvaluateFeature) return NVSDK_NGX_Result_FAIL_OutOfDate;
    return NGXProgressCallbackHelper::EvaluateFeature(GContext.API.EvaluateFeature, InHandle, InParameters, InCallback);
}

#ifdef __cplusplus
} // extern "C"
#endif

#endif /* NVSDK_NGX_HEADER_ONLY */
#endif /* NVSDK_NGX_STANDALONE_CUDA_H */
