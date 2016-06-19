#if COMPILE_PS == 1

Texture2D sourceTexture			: register(t0);
SamplerState anisotropicSampler	: register(s0);
SamplerState bilinearSampler	: register(s1);

#if FXAA == 1
#define FXAA_PC 1
#define FXAA_HLSL_5 1
#define FXAA_QUALITY__PRESET 29
#include "FXAA.hlsl"
#endif

#endif

//= Constant Buffers ===============
cbuffer MiscBuffer : register(b0)
{
	matrix mWorldViewProjection;
};

//= Structs ========================
struct VertexInputType
{
    float4 position : POSITION;
    float2 uv : TEXCOORD;
};

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

/*------------------------------------------------------------------------------
								[VS()]
------------------------------------------------------------------------------*/
#if COMPILE_VS == 1
PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output; 
	
    input.position.w = 1.0f;
	
	// Calculate the world space position for a full-screen quad
    output.position = mul(input.position, mWorldViewProjection);
	
	// UV
	output.uv = input.uv;
	
	return output;
}
#endif

#if COMPILE_PS == 1
/*------------------------------------------------------------------------------
                             [GLOBALS|FUNCTIONS]
------------------------------------------------------------------------------*/
static float2 resolution 				= float2(RESOLUTION_WIDTH, RESOLUTION_HEIGHT);
static float2 texelSize 				= float2(1.0f / RESOLUTION_WIDTH, 1.0f / RESOLUTION_HEIGHT);

/*------------------------------------------------------------------------------
                          [FXAA CODE SECTION]
------------------------------------------------------------------------------*/
#if FXAA == 1
float4 FXAAPass(float2 texCoord)
{
	FxaaTex tex 						= { bilinearSampler, sourceTexture };	
    float2 fxaaQualityRcpFrame			= texelSize;
    float fxaaQualitySubpix				= 0.75f; // 0.75f // The amount of sub-pixel aliasing removal.
    float fxaaQualityEdgeThreshold		= 0.0125f; // 0.125f; // Edge detection threshold. The minimum amount of local contrast required to apply algorithm.
    float fxaaQualityEdgeThresholdMin	= 0.0833f; // 0.0833f // Darkness threshold. Trims the algorithm from processing darks.
	
	float3 fxaa = FxaaPixelShader
	( 
		texCoord, 0, tex, tex, tex,
		fxaaQualityRcpFrame, 0, 0, 0,
		fxaaQualitySubpix,
		fxaaQualityEdgeThreshold,
		fxaaQualityEdgeThresholdMin,
		0, 0, 0, 0
	).xyz;
	
    return float4(fxaa, 1.0f);
}
#endif

/*------------------------------------------------------------------------------
						[SHARPENING CODE SECTION]
------------------------------------------------------------------------------*/
#if SHARPENING == 1 
float4 SharpeningPass(float2 texCoord)
{
  	float val0 = 2.0f;
	float val1 = -0.125f;
	float effect_width = 0.6f;
	float dx = effect_width / RESOLUTION_WIDTH;
	float dy = effect_width / RESOLUTION_HEIGHT;

	float4 c1 = sourceTexture.Sample(anisotropicSampler, texCoord + float2(-dx, -dy)) * val1;
	float4 c2 = sourceTexture.Sample(anisotropicSampler, texCoord + float2(  0, -dy)) * val1;
	float4 c3 = sourceTexture.Sample(anisotropicSampler, texCoord + float2(-dx,   0)) * val1;
	float4 c4 = sourceTexture.Sample(anisotropicSampler, texCoord + float2( dx,   0)) * val1;
	float4 c5 = sourceTexture.Sample(anisotropicSampler, texCoord + float2(  0,  dy)) * val1;
	float4 c6 = sourceTexture.Sample(anisotropicSampler, texCoord + float2( dx,  dy)) * val1;
	float4 c7 = sourceTexture.Sample(anisotropicSampler, texCoord + float2(-dx, +dy)) * val1;
	float4 c8 = sourceTexture.Sample(anisotropicSampler, texCoord + float2(+dx, -dy)) * val1;
	float4 c9 = sourceTexture.Sample(anisotropicSampler, texCoord) * val0;

	return (c1 + c2 + c3 + c4 + c5 + c6 + c7 + c8 + c9);
}
#endif

/*------------------------------------------------------------------------------
						[BLUR]
------------------------------------------------------------------------------*/
#if BLUR == 1
float4 BlurPass(float2 texCoord)
{
	int uBlurSize = 5; // use size of noise texture
	
	float result = 0.0f;
	float temp = float(-uBlurSize) * 0.5f + 0.5f;
	float2 hlim = float2(temp, temp);
	for (int i = 0; i < uBlurSize; ++i) 
		for (int j = 0; j < uBlurSize; ++j) 
		{
			float2 offset = (hlim + float2(float(i), float(j))) * texelSize;
			result += sourceTexture.Sample(anisotropicSampler, texCoord + offset).r;
		}
		
	result = result / float(uBlurSize * uBlurSize);
	   
	return result;
}
#endif

/*------------------------------------------------------------------------------
									[PS()]
------------------------------------------------------------------------------*/
float4 DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	float2 texCoord = input.uv;
	float4 color;

	#if FXAA == 1
	color = FXAAPass(texCoord);
	#endif
	
	#if SHARPENING == 1 
	color = SharpeningPass(texCoord);
	#endif
	
	#if BLUR == 1 
	color = BlurPass(texCoord);
	#endif
	
	return color;
}
#endif