/*
Copyright (c) 2022, NVIDIA CORPORATION. All rights reserved.

NVIDIA CORPORATION and its licensors retain all intellectual property
and proprietary rights in and to this software, related documentation
and any modifications thereto. Any use, reproduction, disclosure or
distribution of this software and related documentation without an express
license agreement from NVIDIA CORPORATION is strictly prohibited.
*/

#pragma once

#define NRD_SETTINGS_VERSION_MAJOR 4
#define NRD_SETTINGS_VERSION_MINOR 16

static_assert(NRD_VERSION_MAJOR == NRD_SETTINGS_VERSION_MAJOR && NRD_VERSION_MINOR == NRD_SETTINGS_VERSION_MINOR, "Please, update all NRD SDK files");

namespace nrd
{
    //====================================================================================================================================================
    // COMMON
    //====================================================================================================================================================

    // IMPORTANT: despite that all NRD accumulation related settings are measured in "frames" (for simplicity), it's recommended to recalculate the
    // number of accumulated frames from the accumulation time (in seconds). It allows to minimize lags if FPS is low and maximize IQ if FPS is high.
    // All default values provided for 60 FPS. Each denoiser has a recommended accumulation time constant and absolute maximum of accumulated frames
    // to clamp to:
    inline uint32_t GetMaxAccumulatedFrameNum(float accumulationTime, float fps)
    {
        return (uint32_t)(accumulationTime * fps + 0.5f);
    }

    // Sequence is based on "CommonSettings::frameIndex":
    //     Even frame (0)  Odd frame (1)   ...
    //         B W             W B
    //         W B             B W
    //     BLACK and WHITE modes define cells with VALID data
    // Checkerboard can be only horizontal
    // Notes:
    //     - if checkerboarding is enabled, "mode" defines the orientation of even numbered frames
    //     - all inputs have the same resolution - logical FULL resolution
    //     - noisy input signals ("IN_DIFF_XXX / IN_SPEC_XXX") are tightly packed to the LEFT HALF of the texture (the input pixel = 2x1 screen pixel)
    //     - for others the input pixel = 1x1 screen pixel
    //     - upsampling will be handled internally in checkerboard mode
    enum class CheckerboardMode : uint8_t
    {
        OFF,
        BLACK,
        WHITE,

        MAX_NUM
    };

    enum class AccumulationMode : uint8_t
    {
        // Common mode (accumulation continues normally)
        CONTINUE,

        // Discards history and resets accumulation
        RESTART,

        // Like RESTART, but additionally clears resources from potential garbage
        CLEAR_AND_RESTART,

        MAX_NUM
    };

    enum class HitDistanceReconstructionMode : uint8_t
    {
        // Probabilistic split at primary hit is not used, hence hit distance is always valid (reconstruction is not needed)
        OFF,

        // If hit distance is invalid due to probabilistic sampling, it's reconstructed using 3x3 (or 5x5) neighbors.
        // Probability at primary hit must be clamped to [1/4; 3/4] (or [1/16; 15/16) range to guarantee a sample in this area.
        // White noise must be replaced with Bayer dithering to gurantee a sample in this area (see NRD sample)
        AREA_3X3, // RECOMMENDED
        AREA_5X5,

        MAX_NUM
    };

    // IMPORTANT: if "unit" is not "meter", all default values must be converted from "meters" to "units"!

    struct CommonSettings
    {
        // Matrix requirements:
        //     - usage - vector is a column
        //     - layout - column-major
        //     - non jittered!
        // LH / RH projection matrix (INF far plane is supported) with non-swizzled rows, i.e. clip-space depth = z / w
        float viewToClipMatrix[16] = {};

        // Previous projection matrix
        float viewToClipMatrixPrev[16] = {};

        // World-space to camera-space matrix
        float worldToViewMatrix[16] = {};

        // If coordinate system moves with the camera, camera delta must be included to reflect camera motion
        float worldToViewMatrixPrev[16] = {};

        // (Optional) previous world-space to current world-space matrix. It is for virtual normals, where a coordinate
        // system of the virtual space changes frame to frame, such as in a case of animated intermediary reflecting
        // surfaces when primary surface replacement is used for them.
        float worldPrevToWorldMatrix[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f
        };

        // Used as "mv = IN_MV * motionVectorScale" (use .z = 0 for 2D screen-space motion)
        // Expected usage: "pixelUvPrev = pixelUv + mv.xy" (where "pixelUv" is in (0; 1) range)
        float motionVectorScale[3] = {1.0f, 1.0f, 0.0f};

        // [-0.5; 0.5] - sampleUv = pixelUv + cameraJitter
        float cameraJitter[2] = {};
        float cameraJitterPrev[2] = {};

        // Flexible dynamic resolution scaling support
        uint16_t resourceSize[2] = {};
        uint16_t resourceSizePrev[2] = {};
        uint16_t rectSize[2] = {};
        uint16_t rectSizePrev[2] = {};

        // (>0) - "viewZ = IN_VIEWZ * viewZScale" (mostly for FP16 viewZ)
        float viewZScale = 1.0f;

        // (Optional) (ms) - user provided if > 0, otherwise - tracked internally
        float timeDeltaBetweenFrames = 0.0f;

        // (units > 0) - use TLAS or tracing range
        // It's highly recommended to use "viewZ > denoisingRange" for INF (sky) pixels
        float denoisingRange = 500000.0f;

        // [0.01; 0.02] - two samples considered occluded if relative distance difference is greater than this slope-scaled threshold
        float disocclusionThreshold = 0.01f;

        // (Optional) [0.02; 0.2] - an alternative disocclusion threshold, which is mixed to based on:
        // - "strandThickness", if there is "strandMaterialID" match
        // - "IN_DISOCCLUSION_THRESHOLD_MIX" texture, if "isDisocclusionThresholdMixAvailable = true" (has higher priority and ignores "strandMaterialID")
        float disocclusionThresholdAlternate = 0.05f;

        // (Optional) (>=0) - marks reflections of camera attached objects (requires "NormalEncoding::R10_G10_B10_A2_UNORM")
        // This material ID marks reflections of objects attached to the camera, not objects themselves. Unfortunately, this is only an improvement
        // for critical cases, but not a generic solution. A generic solution requires reflection MVs, which NRD currently doesn't ask for
        float cameraAttachedReflectionMaterialID = 999.0f;

        // (Optional) (>=0) - marks hair (grass) geometry to enable "under-the-hood" tweaks (requires "NormalEncoding::R10_G10_B10_A2_UNORM")
        float strandMaterialID = 999.0f;

        // (Optional) (>=0) - marks pixels using "historyFixAlternatePixelStride" instead of "historyFixBasePixelStride". This is the last resort
        // setting improving behavior on moving objects (like protagonist's weapon) constantly getting a history reset for some reasons
        float historyFixAlternatePixelStrideMaterialID = 999.0f;

        // (units > 0) - defines how "disocclusionThreshold" blends into "disocclusionThresholdAlternate" = pixelSize / (pixelSize + strandThickness)
        float strandThickness = 80e-6f;

        // [0; 1] - enables "noisy input / denoised output" comparison
        float splitScreen = 0.0f;

        // (Optional) for internal needs
        uint16_t printfAt[2] = {9999, 9999}; // thread (pixel) position
        float debug = 0.0f;

        // (Optional) (pixels) - viewport origin
        // IMPORTANT: gets applied only to non-noisy guides (aka g-buffer):
        // - including: "IN_BASECOLOR_METALNESS"
        // - excluding: "IN_DIFF_CONFIDENCE", "IN_SPEC_CONFIDENCE" and "IN_DISOCCLUSION_THRESHOLD_MIX"
        // Used only if "NRD_SUPPORTS_VIEWPORT_OFFSET = 1"
        uint32_t rectOrigin[2] = {};

        // A consecutively growing number. Valid usage:
        // - must be incremented by 1 on each frame (not by 1 on each "SetCommonSettings" call)
        // - sequence can be restarted after passing "AccumulationMode != CONTINUE"
        // - must be in sync with "CheckerboardMode" (if not OFF)
        uint32_t frameIndex = 0;

        // To reset history set to RESTART or CLEAR_AND_RESTART for one frame
        AccumulationMode accumulationMode = AccumulationMode::CONTINUE;

        // If "true" "IN_MV" is 3D motion in world-space (0 should be everywhere if the scene is static, camera motion must not be included),
        // otherwise it's 2D (+ optional Z delta) screen-space motion (0 should be everywhere if the camera doesn't move)
        bool isMotionVectorInWorldSpace = false;

        // If "true" "IN_DIFF_CONFIDENCE" and "IN_SPEC_CONFIDENCE" are available
        bool isHistoryConfidenceAvailable = false;

        // If "true" "IN_DISOCCLUSION_THRESHOLD_MIX" is available
        bool isDisocclusionThresholdMixAvailable = false;

        // If "true" "IN_BASECOLOR_METALNESS" is available
        bool isBaseColorMetalnessAvailable = false;

        // Enables debug overlay in OUT_VALIDATION
        bool enableValidation = false;
    };

    //====================================================================================================================================================
    // REBLUR
    //====================================================================================================================================================

    const uint32_t REBLUR_MAX_HISTORY_FRAME_NUM = 63;
    const float REBLUR_DEFAULT_ACCUMULATION_TIME = 0.5f; // sec

    // "Normalized hit distance" = saturate( "hit distance" / f ), where:
    // f = ( A + viewZ * B ) * lerp( 1.0, C, exp2( D * roughness ^ 2 ) ), see "NRD.hlsl/REBLUR_FrontEnd_GetNormHitDist"
    struct HitDistanceParameters
    {
        // (units > 0) - constant value
        float A = 3.0f;

        // (> 0) - viewZ based linear scale (1 m - 10 cm, 10 m - 1 m, 100 m - 10 m)
        float B = 0.1f;

        // (>= 1) - roughness based scale, use values > 1 to get bigger hit distance for low roughness
        float C = 20.0f;

        // (<= 0) - absolute value should be big enough to collapse "exp2( D * roughness ^ 2 )" to "~0" for roughness = 1
        float D = -25.0f;
    };

    struct ReblurAntilagSettings
    {
        // [1; 5] - delta is reduced by local variance multiplied by this value
        float luminanceSigmaScale = 4.0f; // can be 3.0 or even less if signal is good

        // [1; 5] - antilag sensitivity (smaller values increase sensitivity)
        float luminanceSensitivity = 3.0f; // can be 2.0 or even less if signal is good
    };

    struct ResponsiveAccumulationSettings
    {
        // [0; 1] - if roughness < roughnessThreshold, temporal accumulation becomes responsive and driven by roughness (useful for animated water)
        // maxAccumulatedFrameNum *= smoothstep( 0, 1, max( roughness, 1e-3 ) / max( roughnessThreshold, 1e-3 ) )
        float roughnessThreshold = 0.0f;

        // [0; historyFixFrameNum] - preserves a few frames in history even for 0-roughness
        // If the signal is clean this value can be reduced to 0 or 1
        uint32_t minAccumulatedFrameNum = 3;
    };

    struct ReblurSettings
    {
        HitDistanceParameters hitDistanceParameters = {};
        ReblurAntilagSettings antilagSettings = {};
        ResponsiveAccumulationSettings responsiveAccumulationSettings = {};

        // [0; REBLUR_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames
        // Always accumulate in "seconds" not in "frames", use "GetMaxAccumulatedFrameNum" for conversion
        uint32_t maxAccumulatedFrameNum = 30;

        // [0; maxAccumulatedFrameNum) - maximum number of linearly accumulated frames for fast history
        // Values ">= maxAccumulatedFrameNum" disable fast history
        // Usually 5x-7x times shorter than the main history (casting more rays, using SHARC or other signal improving techniques help to accumulate less)
        uint32_t maxFastAccumulatedFrameNum = 6;

        // [0; maxAccumulatedFrameNum] - maximum number of linearly accumulated frames for stabilized radiance
        // "0" disables the stabilization pass
        // Values ">= maxAccumulatedFrameNum"  get clamped to "maxAccumulatedFrameNum"
        uint32_t maxStabilizedFrameNum = REBLUR_MAX_HISTORY_FRAME_NUM;

        // [0; 3] - number of reconstructed frames after history reset (less than "maxFastAccumulatedFrameNum")
        uint32_t historyFixFrameNum = 3;

        // (> 0) - base stride between pixels in 5x5 history reconstruction kernel
        uint32_t historyFixBasePixelStride = 14;
        uint32_t historyFixAlternatePixelStride = 14; // see "historyFixAlternatePixelStrideMaterialID"

        // [1; 3] - standard deviation scale of the color box for clamping slow "main" history to responsive "fast" history
        // REBLUR clamps the spatially processed "main" history to the spatially unprocessed "fast" history. It implies using smaller variance scaling than in RELAX.
        // A bit smaller values (> 1) may be used with clean signals. The implementation will adjust this under the hood if spatial sampling is disabled
        float fastHistoryClampingSigmaScale = 2.0f; // 2 is old default, 1.5 works well even for dirty signals, 1.1 is a safe value for occlusion denoising

        // (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of badly defined signals and probabilistic sampling)
        float diffusePrepassBlurRadius = 30.0f;
        float specularPrepassBlurRadius = 50.0f;

        // (0; 0.2] - bigger values reduce sensitivity to shadows in spatial passes, smaller values are recommended for signals with relatively clean hit distance (like RTXDI/RESTIR)
        float minHitDistanceWeight = 0.1f;

        // (pixels) - min denoising radius (for converged state)
        float minBlurRadius = 1.0f;

        // (pixels) - base (max) denoising radius (gets reduced over time)
        float maxBlurRadius = 30.0f;

        // (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
        float lobeAngleFraction = 0.15f;

        // (normalized %) - base fraction of center roughness used to drive roughness based rejection
        float roughnessFraction = 0.15f;

        // (normalized %) - represents maximum allowed deviation from the local tangent plane
        float planeDistanceSensitivity = 0.02f;

        // "IN_MV = lerp(IN_MV, specularMotion, smoothstep(this[0], this[1], specularProbability))"
        float specularProbabilityThresholdsForMvModification[2] = {0.5f, 0.9f};

        // [1; 3] - undesired sporadic outliers suppression to keep output stable (smaller values maximize suppression in exchange of bias)
        float fireflySuppressorMinRelativeScale = 2.0f;

        // (Optional) material ID comparison: max(m0, minMaterial) == max(m1, minMaterial) (requires "NormalEncoding::R10_G10_B10_A2_UNORM")
        float minMaterialForDiffuse = 4.0f;
        float minMaterialForSpecular = 4.0f;

        // If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite. Used only if "NRD_SUPPORTS_CHECKERBOARD = 1"
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Must be used only in case of probabilistic sampling (not checkerboarding), when a pixel can be skipped and have "0" (invalid) hit distance
        HitDistanceReconstructionMode hitDistanceReconstructionMode = HitDistanceReconstructionMode::OFF;

        // Helps to mitigate fireflies emphasized by DLSS. Very cheap and unbiased in most of the cases, better keep in enabled to maximize quality
        bool enableAntiFirefly = true;

        // In rare cases, when bright samples are so sparse that any other bright neighbor can't
        // be reached, pre-pass transforms a standalone bright pixel into a standalone bright blob,
        // worsening the situation. Despite that it's a problem of sampling, the denoiser needs to
        // handle it somehow on its side too. Diffuse pre-pass can be just disabled, but for specular
        // it's still needed to find optimal hit distance for tracking. This boolean allow to use
        // specular pre-pass for tracking purposes only (use with care)
        bool usePrepassOnlyForSpecularMotionEstimation = false;

        // Allows to get diffuse or specular history length in ".w" channel of the output instead of denoised ambient/specular occlusion (normalized hit distance).
        // Diffuse history length shows disocclusions, specular history length is more complex and includes accelerations of various kinds caused by specular tracking.
        // History length is measured in frames, it can be in "[0; maxAccumulatedFrameNum]" range
        bool returnHistoryLengthInsteadOfOcclusion = false;
    };

    //====================================================================================================================================================
    // RELAX
    //====================================================================================================================================================

    const uint32_t RELAX_MAX_HISTORY_FRAME_NUM = 255;
    const float RELAX_DEFAULT_ACCUMULATION_TIME = 0.5f; // sec

    struct RelaxAntilagSettings
    {
        // [0; 1] - amount of history acceleration if history clamping happened in pixel
        float accelerationAmount = 0.3f;

        // (> 0) - history is being reset if delta between history and raw input is larger than spatial sigma + temporal sigma
        float spatialSigmaScale = 4.5f;
        float temporalSigmaScale = 0.5f;

        // [0; 1] - amount of history reset, 0.0 - no reset, 1.0 - full reset
        float resetAmount = 0.5f;
    };

    struct RelaxSettings
    {
        RelaxAntilagSettings antilagSettings = {};

        // [0; RELAX_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames
        // Always accumulate in "seconds" not in "frames", use "GetMaxAccumulatedFrameNum" for conversion
        uint32_t diffuseMaxAccumulatedFrameNum = 30;
        uint32_t specularMaxAccumulatedFrameNum = 30;

        // [0; diffuseMaxAccumulatedFrameNum/specularMaxAccumulatedFrameNum) - maximum number of linearly accumulated frames for fast history
        // Values ">= diffuseMaxAccumulatedFrameNum/specularMaxAccumulatedFrameNum" disable fast history
        // Usually 5x-7x times shorter than the main history (casting more rays, using SHARC or other signal improving techniques help to accumulate less)
        uint32_t diffuseMaxFastAccumulatedFrameNum = 6;
        uint32_t specularMaxFastAccumulatedFrameNum = 6;

        // [0; 3] - number of reconstructed frames after history reset (less than "maxFastAccumulatedFrameNum")
        uint32_t historyFixFrameNum = 3;

        // (> 0) - base stride between pixels in 5x5 history reconstruction kernel
        uint32_t historyFixBasePixelStride = 14;
        uint32_t historyFixAlternatePixelStride = 14; // see "historyFixAlternatePixelStrideMaterialID"

        // (> 0) - normal edge stopper for history reconstruction pass
        float historyFixEdgeStoppingNormalPower = 8.0f;

        // [1; 3] - standard deviation scale of the color box for clamping slow "main" history to responsive "fast" history
        float fastHistoryClampingSigmaScale = 2.0f;

        // (pixels) - pre-accumulation spatial reuse pass blur radius (0 = disabled, must be used in case of probabilistic sampling)
        float diffusePrepassBlurRadius = 30.0f;
        float specularPrepassBlurRadius = 50.0f;

        // (0; 0.2] - bigger values reduce sensitivity to shadows in spatial passes, smaller values are recommended for signals with relatively clean hit distance (like RTXDI/RESTIR)
        float minHitDistanceWeight = 0.1f;

        // (>= 0) - history length threshold below which spatial variance estimation will be executed
        uint32_t spatialVarianceEstimationHistoryThreshold = 3;

        // A-trous edge stopping luminance sensitivity
        float diffusePhiLuminance = 2.0f;
        float specularPhiLuminance = 1.0f;

        // (normalized %) - base fraction of diffuse or specular lobe angle used to drive normal based rejection
        float lobeAngleFraction = 0.5f;

        // (normalized %) - base fraction of center roughness used to drive roughness based rejection
        float roughnessFraction = 0.15f;

        // (>= 0) - how much variance we inject to specular if reprojection confidence is low
        float specularVarianceBoost = 0.0f;

        // (degrees) - slack for the specular lobe angle used in normal based rejection of specular during A-Trous passes
        float specularLobeAngleSlack = 0.15f;

        // [2; 8] - number of iterations for A-Trous wavelet transform
        uint32_t atrousIterationNum = 5;

        // [0; 1] - A-trous edge stopping Luminance weight minimum
        float diffuseMinLuminanceWeight = 0.0f;
        float specularMinLuminanceWeight = 0.0f;

        // (normalized %) - depth threshold for spatial passes
        float depthThreshold = 0.003f;

        // Confidence inputs can affect spatial blurs, relaxing some weights in areas with low confidence
        float confidenceDrivenRelaxationMultiplier = 0.0f;
        float confidenceDrivenLuminanceEdgeStoppingRelaxation = 0.0f;
        float confidenceDrivenNormalEdgeStoppingRelaxation = 0.0f;

        // How much we relax roughness based rejection for spatial filter in areas where specular reprojection is low
        float luminanceEdgeStoppingRelaxation = 0.5f;
        float normalEdgeStoppingRelaxation = 0.3f;

        // How much we relax rejection for spatial filter based on roughness and view vector
        float roughnessEdgeStoppingRelaxation = 1.0f;

        // If not OFF and used for DIFFUSE_SPECULAR, defines diffuse orientation, specular orientation is the opposite. Used only if "NRD_SUPPORTS_CHECKERBOARD = 1"
        CheckerboardMode checkerboardMode = CheckerboardMode::OFF;

        // Must be used only in case of probabilistic sampling (not checkerboarding), when a pixel can be skipped and have "0" (invalid) hit distance
        HitDistanceReconstructionMode hitDistanceReconstructionMode = HitDistanceReconstructionMode::OFF;

        // (Optional) material ID comparison: max(m0, minMaterial) == max(m1, minMaterial) (requires "NormalEncoding::R10_G10_B10_A2_UNORM")
        float minMaterialForDiffuse = 4.0f;
        float minMaterialForSpecular = 4.0f;

        // Firefly suppression
        bool enableAntiFirefly = false;

        // Roughness based rejection
        bool enableRoughnessEdgeStopping = true;
    };

    //====================================================================================================================================================
    // SIGMA
    //====================================================================================================================================================

    const uint32_t SIGMA_MAX_HISTORY_FRAME_NUM = 7;
    const float SIGMA_DEFAULT_ACCUMULATION_TIME = 0.084f; // sec

    struct SigmaSettings
    {
        // Direction to the light source
        // IMPORTANT: it is needed only for directional light sources (sun)
        float lightDirection[3] = {0.0f, 0.0f, 0.0f};

        // (normalized %) - represents maximum allowed deviation from the local tangent plane
        float planeDistanceSensitivity = 0.02f;

        // [0; SIGMA_MAX_HISTORY_FRAME_NUM] - maximum number of linearly accumulated frames
        // 0 - disables the stabilization pass
        // Always accumulate in "seconds" not in "frames", use "GetMaxAccumulatedFrameNum" for conversion
        uint32_t maxStabilizedFrameNum = 5;
    };

    //====================================================================================================================================================
    // REFERENCE
    //====================================================================================================================================================

    const uint32_t REFERENCE_MAX_HISTORY_FRAME_NUM = 4095;
    const float REFERENCE_DEFAULT_ACCUMULATION_TIME = 2.0f; // sec

    struct ReferenceSettings
    {
        // (>= 0) - maximum number of linearly accumulated frames
        uint32_t maxAccumulatedFrameNum = 120;
    };
}
