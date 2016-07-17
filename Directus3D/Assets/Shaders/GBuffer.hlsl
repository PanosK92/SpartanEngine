// = INCLUDES ===============
#include "Helper.hlsl"
#include "ShadowMapping.hlsl"
//===========================

//= TEXTURES ===============================
Texture2D texAlbedo 		: register (t0);
Texture2D texRoughness 		: register (t1);
Texture2D texMetallic 		: register (t2);
Texture2D texOcclusion 		: register (t3);
Texture2D texNormal 		: register (t4);
Texture2D texHeight 		: register (t5);
Texture2D texMask 			: register (t6);
Texture2D lightDepth 		: register (t7);
//==========================================

//= SAMPLERS ===========================================
SamplerState samplerAniso 				: register (s0);
SamplerComparisonState samplerShadow	: register (s1);
//======================================================

//= BUFFERS ==================================
cbuffer DefaultBuffer : register(b0)
{
    matrix mWorld;
    matrix mWorldView;
    matrix mWorldViewProjection;
	matrix mLightViewProjection;
    float4 materialAlbedoColor;
    float materialRoughness;
    float materialMetallic;
    float materialOcclusion;
    float materialNormalStrength;
    float materialReflectivity;
    float materialShadingMode;
    float2 materialTiling;
	float2 viewport;
    float2 padding;
};
//===========================================

//= STRUCTS =================================
struct VertexInputType
{
    float4 position : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

struct PixelInputType
{
    float4 positionCS : SV_POSITION;
    float4 positionVS : POSITIONT0;
    float4 positionWS : POSITIONT1;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
};

struct PixelOutputType
{
	float4 albedo		: SV_Target0;
	float4 normal		: SV_Target1;
	float4 depth		: SV_Target2;
	float4 material		: SV_Target3;
};
//===========================================

PixelInputType DirectusVertexShader(VertexInputType input)
{
    PixelInputType output;
    
    input.position.w = 1.0f;	
	output.positionWS 	= mul(input.position, mWorld);
	output.positionVS 	= mul(input.position, mWorldView);
	output.positionCS 	= mul(input.position, mWorldViewProjection);	
	output.normal 		= normalize(mul(input.normal, mWorld));	
	output.tangent 		= normalize(mul(input.tangent, mWorld));
    output.uv = input.uv;
	
	return output;
}

PixelOutputType DirectusPixelShader(PixelInputType input) : SV_TARGET
{
	PixelOutputType output;

	float2 texel			= float2(1.0f / viewport.x, 1.0f / viewport.y);
	float depth 			= input.positionCS.z /  input.positionCS.w;
	float2 texCoord 		= float2(input.uv.x * materialTiling.x, input.uv.y * materialTiling.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= materialRoughness;
	float metallic 			= materialMetallic;
	float reflectivity 		= clamp(materialReflectivity, 0.03f, 1.0f);
	float occlusion			= 1.0f - materialOcclusion;
	float4 normal			= float4(PackNormal(input.normal.xyz), occlusion);

	//= SAMPLING ================================================================================
#if HEIGHT_MAP
#endif
	
	//= SAMPLING ================================================================================
#if MASK_MAP
		float3 maskSample = texMask.Sample(samplerAniso, texCoord).rgb;
		float threshold = 0.6f;
		if (maskSample.r <= threshold && maskSample.g <= threshold && maskSample.b <= threshold)
			discard;
#endif
	
	//= SAMPLING ================================================================================
#if ALBEDO_MAP
		albedo *= texAlbedo.Sample(samplerAniso, texCoord);
#endif
	
	//= SAMPLING ================================================================================
#if ROUGHNESS_MAP
		roughness *= texRoughness.Sample(samplerAniso, texCoord).r;
#endif
	
	//= SAMPLING ================================================================================
#if METALLIC_MAP
		metallic *= texMetallic.Sample(samplerAniso, texCoord).r;
#endif
	
	//= SAMPLING ================================================================================
#if OCCLUSION_MAP
		occlusion = clamp(texOcclusion.Sample(samplerAniso, texCoord).r * (1 / materialOcclusion), 0.0f, 1.0f);
#endif
	
	//= SAMPLING ================================================================================
#if NORMAL_MAP
		normal 	= texNormal.Sample(samplerAniso, texCoord); // sample
		normal 	= float4(NormalSampleToWorldSpace(normal, input.normal, input.tangent, materialNormalStrength), 1.0f); // transform to world space
		normal 	= float4(PackNormal(normal), 1.0f);
#endif
	//===========================================================================================
	
	//= DETERMINE RENDER QUALITY ================================================================
	float textureMapping	= 0.0f; // This has been implemented
	float shadingLow		= 0.25f;
	float shadingMedium		= 0.5f;
	float shadingHigh 		= 0.75f;
	float shadingUltra 		= 1.0f; // This has been implemented
	float renderMode		= shadingUltra;
#if CUBE_MAP
		renderMode			= textureMapping;
#endif
	//============================================================================================
	
	// SHADOW MAPPING
	float bias = padding.x;
	float4 lightPos = mul(input.positionWS, mLightViewProjection);
	float shadowing	= ShadowMappingPCF(lightDepth, samplerShadow, lightPos, bias, texel);
	
	// Write to G-Buffer
	output.albedo 		= albedo;
	output.normal 		= float4(normal.rgb, 1.0f);
	output.depth 		= float4(depth, 1.0f, shadowing, 1.0f);
	output.material		= float4(roughness, metallic, reflectivity, renderMode);
		
    return output;
}