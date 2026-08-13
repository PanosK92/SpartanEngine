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

#ifndef NVSDK_NGX_DEFS_DLSSD_H
#define NVSDK_NGX_DEFS_DLSSD_H
#pragma once

typedef enum NVSDK_NGX_DLSS_Denoise_Mode
{
    NVSDK_NGX_DLSS_Denoise_Mode_Off = 0,
    NVSDK_NGX_DLSS_Denoise_Mode_DLUnified = 1, // DL based unified upscaler
} NVSDK_NGX_DLSS_Denoise_Mode;

typedef enum NVSDK_NGX_DLSS_Roughness_Mode
{
    NVSDK_NGX_DLSS_Roughness_Mode_Unpacked = 0, // Read roughness separately
    NVSDK_NGX_DLSS_Roughness_Mode_Packed = 1, // Read roughness from normals.w
} NVSDK_NGX_DLSS_Roughness_Mode;

typedef enum NVSDK_NGX_DLSS_Depth_Type
{
    NVSDK_NGX_DLSS_Depth_Type_Linear = 0, // Linear Depth
    NVSDK_NGX_DLSS_Depth_Type_HW = 1,     // HW Depth
} NVSDK_NGX_DLSS_Depth_Type;


typedef enum NVSDK_NGX_RayReconstruction_Hint_Render_Preset
{
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_Default = 0,             // default behavior, may or may not change after OTA
                                                                            // NVSDK_NGX_RayReconstruction_Hint_Render_Preset_A removed, use preset D or E
                                                                            // NVSDK_NGX_RayReconstruction_Hint_Render_Preset_B removed, use preset D or E
                                                                            // NVSDK_NGX_RayReconstruction_Hint_Render_Preset_C removed, use preset D or E
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_D = 4,                   // Default model (transformer)
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_E = 5,                   // Latest transformer model (must use if DoF guide is needed)
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_F = 6,                   // Do not use. reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_G = 7,                   // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_H = 8,                   // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_I = 9,                   // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_J = 10,                  // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_K = 11,                  // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_L = 12,                  // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_M = 13,                  // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_N = 14,                  // Do not use, reverts to default behavior
    NVSDK_NGX_RayReconstruction_Hint_Render_Preset_O = 15,                  // Do not use, reverts to default behavior
} NVSDK_NGX_RayReconstruction_Hint_Render_Preset;

#define NVSDK_NGX_Parameter_DLSS_Denoise_Mode "DLSS.Denoise.Mode"
#define NVSDK_NGX_Parameter_DLSS_Roughness_Mode "DLSS.Roughness.Mode"
#define NVSDK_NGX_Parameter_DiffuseAlbedo  "DLSS.Input.DiffuseAlbedo"
#define NVSDK_NGX_Parameter_SpecularAlbedo "DLSS.Input.SpecularAlbedo"
#define NVSDK_NGX_Parameter_DLSS_Input_DiffuseAlbedo_Subrect_Base_X "DLSS.Input.DiffuseAlbedo.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSS_Input_DiffuseAlbedo_Subrect_Base_Y "DLSS.Input.DiffuseAlbedo.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSS_Input_SpecularAlbedo_Subrect_Base_X "DLSS.Input.SpecularAlbedo.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSS_Input_SpecularAlbedo_Subrect_Base_Y "DLSS.Input.SpecularAlbedo.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSS_Input_Normals_Subrect_Base_X "DLSS.Input.Normals.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSS_Input_Normals_Subrect_Base_Y "DLSS.Input.Normals.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSS_Input_Roughness_Subrect_Base_X "DLSS.Input.Roughness.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSS_Input_Roughness_Subrect_Base_Y "DLSS.Input.Roughness.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_ViewToClipMatrix "ViewToClipMatrix"
#define NVSDK_NGX_Parameter_GBuffer_Emissive "GBuffer.Emissive"
#define NVSDK_NGX_Parameter_Use_Folded_Network "DLSS.Use.Folded.Network"
#define NVSDK_NGX_Parameter_Diffuse_Ray_Direction "Diffuse.Ray.Direction"
#define NVSDK_NGX_Parameter_DLSS_WORLD_TO_VIEW_MATRIX "WorldToViewMatrix"
#define NVSDK_NGX_Parameter_DLSS_VIEW_TO_CLIP_MATRIX "ViewToClipMatrix"
#define NVSDK_NGX_Parameter_Use_HW_Depth "DLSS.Use.HW.Depth"
#define NVSDK_NGX_Parameter_DLSSD_ReflectedAlbedo "DLSSD.ReflectedAlbedo"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeParticles "DLSSD.ColorBeforeParticles"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterParticles "DLSSD.ColorAfterParticles"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeTransparency "DLSSD.ColorBeforeTransparency"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterTransparency "DLSSD.ColorAfterTransparency"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeFog "DLSSD.ColorBeforeFog"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterFog "DLSSD.ColorAfterFog"
#define NVSDK_NGX_Parameter_DLSSD_ScreenSpaceSubsurfaceScatteringGuide "DLSSD.ScreenSpaceSubsurfaceScatteringGuide"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeScreenSpaceSubsurfaceScattering "DLSSD.ColorBeforeScreenSpaceSubsurfaceScattering"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterScreenSpaceSubsurfaceScattering "DLSSD.ColorAfterScreenSpaceSubsurfaceScattering"
#define NVSDK_NGX_Parameter_DLSSD_ScreenSpaceRefractionGuide "DLSSD.ScreenSpaceRefractionGuide"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeScreenSpaceRefraction "DLSSD.ColorBeforeScreenSpaceRefraction"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterScreenSpaceRefraction "DLSSD.ColorAfterScreenSpaceRefraction"
#define NVSDK_NGX_Parameter_DLSSD_DepthOfFieldGuide "DLSSD.DepthOfFieldGuide"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeDepthOfField "DLSSD.ColorBeforeDepthOfField"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterDepthOfField "DLSSD.ColorAfterDepthOfField"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseHitDistance "DLSSD.DiffuseHitDistance"
#define NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance "DLSSD.SpecularHitDistance"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirection "DLSSD.DiffuseRayDirection"
#define NVSDK_NGX_Parameter_DLSSD_SpecularRayDirection "DLSSD.SpecularRayDirection"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirectionHitDistance "DLSSD.DiffuseRayDirectionHitDistance"
#define NVSDK_NGX_Parameter_DLSSD_SpecularRayDirectionHitDistance "DLSSD.SpecularRayDirectionHitDistance"
#define NVSDK_NGX_Parameter_DLSSD_Alpha "DLSSD.Alpha"
#define NVSDK_NGX_Parameter_DLSSD_OutputAlpha "DLSSD.OutputAlpha"
#define NVSDK_NGX_Parameter_DLSSD_ResponsivityMask "DLSSD.ResponsivityMask"
#define NVSDK_NGX_Parameter_DLSSD_Alpha_Subrect_Base_X "DLSSD.Alpha.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_Alpha_Subrect_Base_Y "DLSSD.Alpha.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_OutputAlpha_Subrect_Base_X "DLSSD.OutputAlpha.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_OutputAlpha_Subrect_Base_Y "DLSSD.OutputAlpha.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ReflectedAlbedo_Subrect_Base_X "DLSSD.ReflectedAlbedo.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ReflectedAlbedo_Subrect_Base_Y "DLSSD.ReflectedAlbedo.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeParticles_Subrect_Base_X "DLSSD.ColorBeforeParticles.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeParticles_Subrect_Base_Y "DLSSD.ColorBeforeParticles.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterParticles_Subrect_Base_X "DLSSD.ColorAfterParticles.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterParticles_Subrect_Base_Y "DLSSD.ColorAfterParticles.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeTransparency_Subrect_Base_X "DLSSD.ColorBeforeTransparency.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeTransparency_Subrect_Base_Y "DLSSD.ColorBeforeTransparency.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterTransparency_Subrect_Base_X "DLSSD.ColorAfterTransparency.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterTransparency_Subrect_Base_Y "DLSSD.ColorAfterTransparency.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeFog_Subrect_Base_X "DLSSD.ColorBeforeFog.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeFog_Subrect_Base_Y "DLSSD.ColorBeforeFog.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterFog_Subrect_Base_X "DLSSD.ColorAfterFog.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterFog_Subrect_Base_Y "DLSSD.ColorAfterFog.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ScreenSpaceSubsurfaceScatteringGuide_Subrect_Base_X "DLSSD.ScreenSpaceSubsurfaceScatteringGuide.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ScreenSpaceSubsurfaceScatteringGuide_Subrect_Base_Y "DLSSD.ScreenSpaceSubsurfaceScatteringGuide.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeScreenSpaceSubsurfaceScattering_Subrect_Base_X "DLSSD.ColorBeforeScreenSpaceSubsurfaceScattering.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeScreenSpaceSubsurfaceScattering_Subrect_Base_Y "DLSSD.ColorBeforeScreenSpaceSubsurfaceScattering.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterScreenSpaceSubsurfaceScattering_Subrect_Base_X "DLSSD.ColorAfterScreenSpaceSubsurfaceScattering.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterScreenSpaceSubsurfaceScattering_Subrect_Base_Y "DLSSD.ColorAfterScreenSpaceSubsurfaceScattering.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ScreenSpaceRefractionGuide_Subrect_Base_X "DLSSD.ScreenSpaceRefractionGuide.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ScreenSpaceRefractionGuide_Subrect_Base_Y "DLSSD.ScreenSpaceRefractionGuide.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeScreenSpaceRefraction_Subrect_Base_X "DLSSD.ColorBeforeScreenSpaceRefraction.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeScreenSpaceRefraction_Subrect_Base_Y "DLSSD.ColorBeforeScreenSpaceRefraction.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterScreenSpaceRefraction_Subrect_Base_X "DLSSD.ColorAfterScreenSpaceRefraction.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterScreenSpaceRefraction_Subrect_Base_Y "DLSSD.ColorAfterScreenSpaceRefraction.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_DepthOfFieldGuide_Subrect_Base_X "DLSSD.DepthOfFieldGuide.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_DepthOfFieldGuide_Subrect_Base_Y "DLSSD.DepthOfFieldGuide.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeDepthOfField_Subrect_Base_X "DLSSD.ColorBeforeDepthOfField.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorBeforeDepthOfField_Subrect_Base_Y "DLSSD.ColorBeforeDepthOfField.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterDepthOfField_Subrect_Base_X "DLSSD.ColorAfterDepthOfField.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ColorAfterDepthOfField_Subrect_Base_Y "DLSSD.ColorAfterDepthOfField.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseHitDistance_Subrect_Base_X "DLSSD.DiffuseHitDistance.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseHitDistance_Subrect_Base_Y "DLSSD.DiffuseHitDistance.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance_Subrect_Base_X "DLSSD.SpecularHitDistance.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_SpecularHitDistance_Subrect_Base_Y "DLSSD.SpecularHitDistance.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirection_Subrect_Base_X "DLSSD.DiffuseRayDirection.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirection_Subrect_Base_Y "DLSSD.DiffuseRayDirection.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_SpecularRayDirection_Subrect_Base_X "DLSSD.SpecularRayDirection.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_SpecularRayDirection_Subrect_Base_Y "DLSSD.SpecularRayDirection.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirectionHitDistance_Subrect_Base_X "DLSSD.DiffuseRayDirectionHitDistance.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_DiffuseRayDirectionHitDistance_Subrect_Base_Y "DLSSD.DiffuseRayDirectionHitDistance.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_SpecularRayDirectionHitDistance_Subrect_Base_X "DLSSD.SpecularRayDirectionHitDistance.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_SpecularRayDirectionHitDistance_Subrect_Base_Y "DLSSD.SpecularRayDirectionHitDistance.Subrect.Base.Y"
#define NVSDK_NGX_Parameter_DLSSD_ResponsivityMask_Subrect_Base_X "DLSSD.ResponsivityMask.Subrect.Base.X"
#define NVSDK_NGX_Parameter_DLSSD_ResponsivityMask_Subrect_Base_Y "DLSSD.ResponsivityMask.Subrect.Base.Y"

#define NVSDK_NGX_Parameter_SuperSamplingDenoising_Available             "SuperSamplingDenoising.Available"
#define NVSDK_NGX_Parameter_SuperSamplingDenoising_NeedsUpdatedDriver    "SuperSamplingDenoising.NeedsUpdatedDriver"
#define NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMajor "SuperSamplingDenoising.MinDriverVersionMajor"
#define NVSDK_NGX_Parameter_SuperSamplingDenoising_MinDriverVersionMinor "SuperSamplingDenoising.MinDriverVersionMinor"
#define NVSDK_NGX_Parameter_SuperSamplingDenoising_FeatureInitResult     "SuperSamplingDenoising.FeatureInitResult"
#define NVSDK_NGX_Parameter_DLSSDOptimalSettingsCallback "DLSSDOptimalSettingsCallback"
#define NVSDK_NGX_Parameter_DLSSDGetStatsCallback        "DLSSDGetStatsCallback"

#define NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_DLAA "RayReconstruction.Hint.Render.Preset.DLAA"
#define NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Quality "RayReconstruction.Hint.Render.Preset.Quality"
#define NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Balanced "RayReconstruction.Hint.Render.Preset.Balanced"
#define NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_Performance "RayReconstruction.Hint.Render.Preset.Performance"
#define NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraPerformance "RayReconstruction.Hint.Render.Preset.UltraPerformance"
#define NVSDK_NGX_Parameter_RayReconstruction_Hint_Render_Preset_UltraQuality "RayReconstruction.Hint.Render.Preset.UltraQuality"

#endif // NVSDK_NGX_DEFS_DLSSD_H
