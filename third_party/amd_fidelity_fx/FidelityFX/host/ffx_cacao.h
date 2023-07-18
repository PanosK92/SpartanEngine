// This file is part of the FidelityFX SDK.
//
// Copyright © 2023 Advanced Micro Devices, Inc.
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy 
// of this software and associated documentation files(the “Software”), to deal 
// in the Software without restriction, including without limitation the rights 
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell 
// copies of the Software, and to permit persons to whom the Software is 
// furnished to do so, subject to the following conditions :
// 
// The above copyright notice and this permission notice shall be included in 
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR 
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE 
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, 
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN 
// THE SOFTWARE.

#pragma once

#include <stdint.h>

///Include the interface for the backend of the API. 
#include <FidelityFX/host/ffx_interface.h>

/// @defgroup FfxCacao FidelityFX CACAO
/// FidelityFX Combined Adaptive Compute Ambient Occlusion runtime library.
///
/// @ingroup SDKComponents

/// FidelityFX CACAO major version.
///
/// @ingroup FfxCacao
#define FFX_CACAO_VERSION_MAJOR (1)

/// FidelityFX CACAO minor version.
///
/// @ingroup FfxCacao
#define FFX_CACAO_VERSION_MINOR (3)

/// FidelityFX CACAO patch version.
///
/// @ingroup FfxCacao
#define FFX_CACAO_VERSION_PATCH (0)

// ============================================================================
// Prepare

/// Width of the PREPARE_DEPTHS_AND_MIPS pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_WIDTH  8
/// Height of the PREPARE_DEPTHS_AND_MIPS pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_DEPTHS_AND_MIPS_HEIGHT 8

/// Width of the PREPARE_DEPTHS pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_DEPTHS_WIDTH  8
/// Height of the PREPARE_DEPTHS pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_DEPTHS_HEIGHT 8

/// Width of the PREPARE_DEPTHS_HALF pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_DEPTHS_HALF_WIDTH  8
/// Height of the PREPARE_DEPTHS_HALF pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_DEPTHS_HALF_HEIGHT 8

/// Width of the PREPARE_NORMALS pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_NORMALS_WIDTH  8
/// Height of the PREPARE_NORMALS pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_PREPARE_NORMALS_HEIGHT 8

/// Width of the PREPARE_NORMALS_FROM_INPUT_NORMALS pass tile size.
///
/// @ingroup FfxCacao
#define PREPARE_NORMALS_FROM_INPUT_NORMALS_WIDTH  8
/// Height of the PREPARE_NORMALS_FROM_INPUT_NORMALS pass tile size.
///
/// @ingroup FfxCacao
#define PREPARE_NORMALS_FROM_INPUT_NORMALS_HEIGHT 8

// ============================================================================
// SSAO Generation

/// Width of the GENERATE_SPARSE pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_GENERATE_SPARSE_WIDTH  4
/// Height of the GENERATE_SPARSE pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_GENERATE_SPARSE_HEIGHT 16

/// Width of the GENERATE pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_GENERATE_WIDTH  8
/// Height of the GENERATE pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_GENERATE_HEIGHT 8

// ============================================================================
// Importance Map

/// Width of the IMPORTANCE_MAP pass tile size.
///
/// @ingroup FfxCacao
#define IMPORTANCE_MAP_WIDTH  8
/// Height of the IMPORTANCE_MAP pass tile size.
///
/// @ingroup FfxCacao
#define IMPORTANCE_MAP_HEIGHT 8

/// Width of the IMPORTANCE_MAP_A pass tile size.
///
/// @ingroup FfxCacao
#define IMPORTANCE_MAP_A_WIDTH  8
/// Height of the IMPORTANCE_MAP_A pass tile size.
///
/// @ingroup FfxCacao
#define IMPORTANCE_MAP_A_HEIGHT 8

/// Width of the IMPORTANCE_MAP_B pass tile size.
///
/// @ingroup FfxCacao
#define IMPORTANCE_MAP_B_WIDTH  8
/// Height of the IMPORTANCE_MAP_B pass tile size.
///
/// @ingroup FfxCacao
#define IMPORTANCE_MAP_B_HEIGHT 8

// ============================================================================
// Edge Sensitive Blur

/// Width of the BLUR pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_BLUR_WIDTH  16
/// Height of the BLUR pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_BLUR_HEIGHT 16

// ============================================================================
// Apply

/// Width of the APPLY pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_APPLY_WIDTH  8
/// Height of the APPLY pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_APPLY_HEIGHT 8

// ============================================================================
// Bilateral Upscale

/// Width of the BILATERAL_UPSCALE pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_BILATERAL_UPSCALE_WIDTH  8
/// Height of the BILATERAL_UPSCALE pass tile size.
///
/// @ingroup FfxCacao
#define FFX_CACAO_BILATERAL_UPSCALE_HEIGHT 8

/// The size of the context specified in 32bit values.
///
/// @ingroup FfxCacao
#define FFX_CACAO_CONTEXT_SIZE (320000)

/// FidelityFX CACAO context count.
///
/// Defines the number of internal effect contexts required by CACAO.
///
/// @ingroup FfxCacao
#define FFX_CACAO_CONTEXT_COUNT 1

/// An enumeration of the passes which constitutes the CACAO algorithm.
///
/// @ingroup FfxCacao
typedef enum FfxCacaoPass
{
    FFX_CACAO_PASS_CLEAR_LOAD_COUNTER = 0,  ///< 

    FFX_CACAO_PASS_PREPARE_DOWNSAMPLED_DEPTHS                 = 1,  ///< 
    FFX_CACAO_PASS_PREPARE_NATIVE_DEPTHS                     = 2,  ///< 
    FFX_CACAO_PASS_PREPARE_DOWNSAMPLED_DEPTHS_AND_MIPS        = 3,  ///< 
    FFX_CACAO_PASS_PREPARE_NATIVE_DEPTHS_AND_MIPS            = 4,  ///< 
    FFX_CACAO_PASS_PREPARE_DOWNSAMPLED_NORMALS               = 5,  ///< 
    FFX_CACAO_PASS_PREPARE_NATIVE_NORMALS                    = 6,  ///< 
    FFX_CACAO_PASS_PREPARE_DOWNSAMPLED_NORMALS_FROM_INPUT_NORMALS = 7,  ///< 
    FFX_CACAO_PASS_PREPARE_NATIVE_NORMALS_FROM_INPUT_NORMALS      = 8,  ///< 
    FFX_CACAO_PASS_PREPARE_DOWNSAMPLED_DEPTHS_HALF                = 9,  ///< 
    FFX_CACAO_PASS_PREPARE_NATIVE_DEPTHS_HALF                     = 10,  ///< 

    FFX_CACAO_PASS_GENERATE_Q0 = 11,  ///< 
    FFX_CACAO_PASS_GENERATE_Q1 = 12,  ///< 
    FFX_CACAO_PASS_GENERATE_Q2 = 13,  ///< 
    FFX_CACAO_PASS_GENERATE_Q3 = 14,  ///< 
    FFX_CACAO_PASS_GENERATE_Q3_BASE = 15,  ///< 

    FFX_CACAO_PASS_GENERATE_IMPORTANCE_MAP = 16,  ///< 
    FFX_CACAO_PASS_POST_PROCESS_IMPORTANCE_MAP_A = 17,  ///< 
    FFX_CACAO_PASS_POST_PROCESS_IMPORTANCE_MAP_B = 18,  ///< 

    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_1 = 19,  ///< 
    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_2 = 20,  ///< 
    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_3 = 21,  ///< 
    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_4 = 22,  ///< 
    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_5 = 23,  ///< 
    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_6 = 24,  ///< 
    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_7 = 25,  ///< 
    FFX_CACAO_PASS_EDGE_SENSITIVE_BLUR_8 = 26,  ///< 

    FFX_CACAO_PASS_APPLY_NON_SMART_HALF = 27,  ///< 
    FFX_CACAO_PASS_APPLY_NON_SMART = 28,  ///< 
    FFX_CACAO_PASS_APPLY = 29,  ///< 
      
    FFX_CACAO_PASS_UPSCALE_BILATERAL_5X5 = 30,

    FFX_CACAO_PASS_COUNT  ///< The number of passes in CACAO
} FfxCacaoPass;

///	The quality levels that FidelityFX CACAO can generate SSAO at. This affects the number of samples taken for generating SSAO.
///
/// @ingroup FfxCacao
typedef enum FfxCacaoQuality {
	FFX_CACAO_QUALITY_LOWEST  = 0,
	FFX_CACAO_QUALITY_LOW     = 1,
	FFX_CACAO_QUALITY_MEDIUM  = 2,
	FFX_CACAO_QUALITY_HIGH    = 3,
	FFX_CACAO_QUALITY_HIGHEST = 4,
} FfxCacaoQuality;

/// An enumeration of bit flags used when creating a
/// <c><i>FfxCacaoContext</i></c>. See <c><i>FfxCacaoContextDescription</i></c>.
///
/// @ingroup FfxCacao
typedef enum FfxCacaoInitializationFlagBits {
    FFX_CACAO_ENABLE_APPLY_SMART                  = (1<<0),   ///< A bit indicating to use smart application
} FfxCacaoInitializationFlagBits;

///	A structure representing a 4x4 matrix of floats. The matrix is stored in row major order in memory.
///
/// @ingroup FfxCacao
typedef struct FfxCacaoMat4x4 {
	float elements[4][4];
} FfxCacaoMat4x4;

///	A structure for the settings used by FidelityFX CACAO. These settings may be updated with each draw call.
///
/// @ingroup FfxCacao
typedef struct FfxCacaoSettings {
	float           radius;                            ///< [0.0,  ~ ] World (view) space size of the occlusion sphere.
	float           shadowMultiplier;                  ///< [0.0, 5.0] Effect strength linear multiplier.
	float           shadowPower;                       ///< [0.5, 5.0] Effect strength pow modifier.
	float           shadowClamp;                       ///< [0.0, 1.0] Effect max limit (applied after multiplier but before blur).
	float           horizonAngleThreshold;             ///< [0.0, 0.2] Limits self-shadowing (makes the sampling area less of a hemisphere, more of a spherical cone, to avoid self-shadowing and various artifacts due to low tessellation and depth buffer imprecision, etc.).
	float           fadeOutFrom;                       ///< [0.0,  ~ ] Distance to start fading out the effect.
	float           fadeOutTo;                         ///< [0.0,  ~ ] Distance at which the effect is faded out.
	FfxCacaoQuality qualityLevel;                      ///<            Effect quality, affects number of taps etc.
	float           adaptiveQualityLimit;              ///< [0.0, 1.0] (only for quality level FFX_CACAO_QUALITY_HIGHEST).
	uint32_t        blurPassCount;                     ///< [  0,   8] Number of edge-sensitive smart blur passes to apply.
	float           sharpness;                         ///< [0.0, 1.0] How much to bleed over edges; 1: not at all, 0.5: half-half; 0.0: completely ignore edges.
	float           temporalSupersamplingAngleOffset;  ///< [0.0,  PI] Used to rotate sampling kernel; If using temporal AA / supersampling, suggested to rotate by ( (frame%3)/3.0*PI ) or similar. Kernel is already symmetrical, which is why we use PI and not 2*PI.
	float           temporalSupersamplingRadiusOffset; ///< [0.0, 2.0] Used to scale sampling kernel; If using temporal AA / supersampling, suggested to scale by ( 1.0f + (((frame%3)-1.0)/3.0)*0.1 ) or similar.
	float           detailShadowStrength;              ///< [0.0, 5.0] Used for high-res detail AO using neighboring depth pixels: adds a lot of detail but also reduces temporal stability (adds aliasing).
	bool            generateNormals;                   ///< This option should be set to FFX_CACAO_TRUE if FidelityFX-CACAO should reconstruct a normal buffer from the depth buffer. It is required to be FFX_CACAO_TRUE if no normal buffer is provided.
	float           bilateralSigmaSquared;             ///< [0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving Gaussian blur term. Should be greater than 0.0.
	float           bilateralSimilarityDistanceSigma;  ///< [0.0,  ~ ] Sigma squared value for use in bilateral upsampler giving similarity weighting for neighbouring pixels. Should be greater than 0.0.
} FfxCacaoSettings;

/// The default settings used by FidelityFX CACAO.
///
/// @ingroup FfxCacao
static const FfxCacaoSettings FFX_CACAO_DEFAULT_SETTINGS = {
	/* radius                            */ 1.2f,
	/* shadowMultiplier                  */ 1.0f,
	/* shadowPower                       */ 1.50f,
	/* shadowClamp                       */ 0.98f,
	/* horizonAngleThreshold             */ 0.06f,
	/* fadeOutFrom                       */ 50.0f,
	/* fadeOutTo                         */ 300.0f,
	/* qualityLevel                      */ FFX_CACAO_QUALITY_HIGHEST,
	/* adaptiveQualityLimit              */ 0.45f,
	/* blurPassCount                     */ 2,
	/* sharpness                         */ 0.98f,
	/* temporalSupersamplingAngleOffset  */ 0.0f,
	/* temporalSupersamplingRadiusOffset */ 0.0f,
	/* detailShadowStrength              */ 0.5f,
	/* generateNormals                   */ false,
	/* bilateralSigmaSquared             */ 5.0f,
	/* bilateralSimilarityDistanceSigma  */ 0.01f,
};

///	A structure for the constant buffer used by FidelityFX CACAO.
///
/// @ingroup FfxCacao
typedef struct FfxCacaoConstants {
	float                   DepthUnpackConsts[2];
	float                   CameraTanHalfFOV[2];

	float                   NDCToViewMul[2];
	float                   NDCToViewAdd[2];

	float                   DepthBufferUVToViewMul[2];
	float                   DepthBufferUVToViewAdd[2];

	float                   EffectRadius;
	float                   EffectShadowStrength;
	float                   EffectShadowPow;
	float                   EffectShadowClamp;

	float                   EffectFadeOutMul;
	float                   EffectFadeOutAdd;
	float                   EffectHorizonAngleThreshold;
	float                   EffectSamplingRadiusNearLimitRec;

	float                   DepthPrecisionOffsetMod;
	float                   NegRecEffectRadius;
	float                   LoadCounterAvgDiv;
	float                   AdaptiveSampleCountLimit;

	float                   InvSharpness;
	int                     BlurNumPasses;
	float                   BilateralSigmaSquared;
	float                   BilateralSimilarityDistanceSigma;

	float                   PatternRotScaleMatrices[4][5][4];

	float                   NormalsUnpackMul;
	float                   NormalsUnpackAdd;
	float                   DetailAOStrength;
	float                   Dummy0;

	float                   SSAOBufferDimensions[2];
	float                   SSAOBufferInverseDimensions[2];

	float                   DepthBufferDimensions[2];
	float                   DepthBufferInverseDimensions[2];

	int                     DepthBufferOffset[2];
    int						Pad[2];
	float                   PerPassFullResUVOffset[4*4];

	float                   InputOutputBufferDimensions[2];
	float                   InputOutputBufferInverseDimensions[2];

	float                   ImportanceMapDimensions[2];
	float                   ImportanceMapInverseDimensions[2];

	float                   DeinterleavedDepthBufferDimensions[2];
	float                   DeinterleavedDepthBufferInverseDimensions[2];

	float                   DeinterleavedDepthBufferOffset[2];
	float                   DeinterleavedDepthBufferNormalisedOffset[2];

	FfxCacaoMat4x4       NormalsWorldToViewspaceMatrix;

} FfxCacaoConstants;

/// A structure encapsulating the parameters required to initialize FidelityFX CACAO.
///
/// @ingroup FfxCacao
typedef struct FfxCacaoContextDescription
{
    FfxInterface                      backendInterface;
    uint32_t                          width;                ///< width of the input/output buffers
	uint32_t                          height;               ///< height of the input/output buffers
	bool                              useDownsampledSsao;   ///< Whether SSAO should be generated at native resolution or half resolution. It is recommended to enable this setting for improved performance.
} FfxCacaoContextDescription;

/// A structure encapsulating the parameters and resources required to dispatch FidelityFX CACAO.
///
/// @ingroup FfxCacao
typedef struct FfxCacaoDispatchDescription
{
    FfxCommandList             commandList;
    FfxResource                depthBuffer;
    FfxResource                normalBuffer;
    FfxResource                outputBuffer;
    FfxCacaoMat4x4*       proj;
    FfxCacaoMat4x4*       normalsToView;
    float                      normalUnpackMul;
    float                      normalUnpackAdd;
} FfxCacaoDispatchDescription;

///	A structure containing sizes of each of the buffers used by FidelityFX CACAO.
///
/// @ingroup FfxCacao
typedef struct FfxCacaoBufferSizeInfo {
	uint32_t inputOutputBufferWidth;
	uint32_t inputOutputBufferHeight;

	uint32_t ssaoBufferWidth;
	uint32_t ssaoBufferHeight;

	uint32_t depthBufferXOffset;
	uint32_t depthBufferYOffset;

	uint32_t depthBufferWidth;
	uint32_t depthBufferHeight;

	uint32_t deinterleavedDepthBufferXOffset;
	uint32_t deinterleavedDepthBufferYOffset;

	uint32_t deinterleavedDepthBufferWidth;
	uint32_t deinterleavedDepthBufferHeight;

	uint32_t importanceMapWidth;
	uint32_t importanceMapHeight;

	uint32_t downsampledSsaoBufferWidth;
	uint32_t downsampledSsaoBufferHeight;
} FfxCacaoBufferSizeInfo;

/// An enumeration of bit flags used when dispatching FidelityFX CACAO
///
/// @ingroup FfxCacao
typedef enum FfxCacaoDispatchFlagsBits {

    FFX_CACAO_SRV_SSAO_REMAP_TO_PONG                  = (1<<0),   ///< A bit indicating the SRV maps to pong texture
    FFX_CACAO_UAV_SSAO_REMAP_TO_PONG                  = (1<<1),   ///< A bit indicating the UAV maps to pong texture
} FfxCacaoDispatchFlagsBits;

/// A structure encapsulating the FidelityFX CACAO context.
///
/// This sets up an object which contains all persistent internal data and
/// resources that are required by CACAO.
///
/// The <c><i>FfxCacaoContext</i></c> object should have a lifetime matching
/// your use of CACAO. Before destroying the CACAO context care should be taken
/// to ensure the GPU is not accessing the resources created or used by CACAO.
/// It is therefore recommended that the GPU is idle before destroying the
/// CACAO context.
///
/// @ingroup FfxCacao
typedef struct FfxCacaoContext
{
    uint32_t data[FFX_CACAO_CONTEXT_SIZE];  ///< An opaque set of <c>uint32_t</c> which contain the data for the context.
} FfxCacaoContext;

/// Create a FidelityFX CACAO context from the parameters
/// programmed to the <c><i>FfxCacaoContextDescription</i></c> structure.
///
/// The context structure is the main object used to interact with the CACAO
/// API, and is responsible for the management of the internal resources
/// used by the CACAO algorithm. When this API is called, multiple calls
/// will be made via the pointers contained in the <c><i>backendInterface</i></c>
/// structure. This backend will attempt to retrieve the device capabilities,
/// and create the internal resources, and pipelines required by CACAO to function.
/// Depending on the precise configuration used when
/// creating the <c><i>FfxCacaoContext</i></c> a different set of resources and
/// pipelines might be requested via the callback functions.
///
/// The <c><i>FfxCacaoContext</i></c> should be destroyed when use of it is
/// completed, typically when an application is unloaded or CACAO
/// upscaling is disabled by a user. To destroy the CACAO context you
/// should call <c><i>ffxCacaoContextDestroy</i></c>.
///
/// @param [out] pContext                A pointer to a <c><i>FfxCacaoContext</i></c> structure to populate.
/// @param [in]  pContextDescription     A pointer to a <c><i>FfxCacaoContextDescription</i></c> structure.
///
/// @retval
/// FFX_OK                              The operation completed successfully.
/// @retval
/// FFX_ERROR_CODE_NULL_POINTER         The operation failed because either <c><i>context</i></c> or <c><i>contextDescription</i></c> was <c><i>NULL</i></c>.
/// @retval
/// FFX_ERROR_INCOMPLETE_INTERFACE      The operation failed because the <c><i>FfxCacaoContextDescription.callbacks</i></c>  was not fully specified.
/// @retval
/// FFX_ERROR_BACKEND_API_ERROR         The operation failed because of an error returned from the backend.
///
/// @ingroup FfxCacao
FFX_API FfxErrorCode ffxCacaoContextCreate(FfxCacaoContext* context, const FfxCacaoContextDescription* contextDescription);

/// Dispatches work to the FidelityFX CACAO context
///
/// @param [out] pContext                A pointer to a <c><i>FfxCacaoContext</i></c> structure to populate.
/// @param [in]  pDispatchDescription    A pointer to a <c><i>FfxCacaoDispatchDescription</i></c> structure.
///
/// @retval
/// FFX_OK                              The operation completed successfully.
/// @retval
/// FFX_ERROR_CODE_NULL_POINTER         The operation failed because either <c><i>context</i></c> or <c><i>dispatchDescription</i></c> was <c><i>NULL</i></c>.
/// @retval
/// FFX_ERROR_BACKEND_API_ERROR         The operation failed because of an error returned from the backend.
///
/// @ingroup FfxCacao
FFX_API FfxErrorCode ffxCacaoContextDispatch(FfxCacaoContext* context, const FfxCacaoDispatchDescription* dispatchDescription);

/// Destroy the FidelityFX CACAO context.
///
/// @param [out] pContext                A pointer to a <c><i>FfxCacaoContext</i></c> structure to destroy.
///
/// @retval
/// FFX_OK                              The operation completed successfully.
/// @retval
/// FFX_ERROR_CODE_NULL_POINTER         The operation failed because <c><i>context</i></c> was <c><i>NULL</i></c>.
///
/// @ingroup FfxCacao
FFX_API FfxErrorCode ffxCacaoContextDestroy(FfxCacaoContext* context);

/// Updates the settings used by CACAO.
/// @param [in] context A pointer to a <c><i>FfxCacaoContext</i></c> structure to change settings for.
/// @param [in] settings A pointer to a <c><i>FfxCacaoSettings</i></c> structure.
///
/// @retval
/// FFX_OK                              The operation completed successfully.
/// @retval
/// FFX_ERROR_CODE_NULL_POINTER         The operation failed because either <c><i>context</i></c> or <c><i>settings</i></c> was <c><i>NULL</i></c>.
///
/// @ingroup FfxCacao
FFX_API FfxErrorCode ffxCacaoUpdateSettings(FfxCacaoContext* context, const FfxCacaoSettings* settings, const bool useDownsampledSsao);
