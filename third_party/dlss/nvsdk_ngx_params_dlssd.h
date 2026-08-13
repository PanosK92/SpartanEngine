/*
 * SPDX-FileCopyrightText: Copyright (c) 2023 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: LicenseRef-NvidiaProprietary
 *
 * NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
 * property and proprietary rights in and to this material, related
 * documentation and any modifications thereto. Any use, reproduction,
 * disclosure or distribution of this material and related documentation
 * without an express license agreement from NVIDIA CORPORATION or
 * its affiliates is strictly prohibited.
 */


#ifndef NVSDK_NGX_PARAMS_DLSSD_H
#define NVSDK_NGX_PARAMS_DLSSD_H

#include "nvsdk_ngx_defs_dlssd.h"

typedef struct NVSDK_NGX_DLSSD_Create_Params
{
    NVSDK_NGX_DLSS_Denoise_Mode InDenoiseMode;
    NVSDK_NGX_DLSS_Roughness_Mode InRoughnessMode;
    NVSDK_NGX_DLSS_Depth_Type InUseHWDepth;

    unsigned int InWidth;
    unsigned int InHeight;
    unsigned int InTargetWidth;
    unsigned int InTargetHeight;
    /*** OPTIONAL ***/
    NVSDK_NGX_PerfQuality_Value InPerfQualityValue;
    int     InFeatureCreateFlags;
    bool    InEnableOutputSubrects;
} NVSDK_NGX_DLSSD_Create_Params;


#endif // #define NVSDK_NGX_PARAMS_DLSSD_H
