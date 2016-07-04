#include "Normal.hlsl"
#include "ShadowMapping.hlsl"

//= TEXTURES =========
Texture2D textures[8];
// 0 - Albedo
// 1 - Roughness
// 2 - Metallic
// 3 - Occlusion
// 4 - Normal
// 5 - Height
// 6 - Mask
// 7 - DirLightDepth
//====================

//= SAMPLERS =================================
SamplerState samplerAnisoWrap : register (s0);
SamplerState shadowSampler    : register (s1);
//============================================

//= BUFFERS ==================================
cbuffer DefaultBuffer : register(b0)
{
    matrix mWorld;
    matrix mWorldView;
    matrix mWorldViewProjection;
    matrix mViewProjectionDirLight;
    float4 materialAlbedoColor;
    float materialRoughness;
    float materialMetallic;
    float materialOcclusion;
    float materialNormalStrength;
    float materialReflectivity;
    float materialShadingMode;
    float2 materialTiling;
    float bias;
    float3 lightDirection;
    float3 cameraPosition;
    float padding;
};
//===========================================

//= STRUCTS ====================
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
//==============================

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

	float depth 			= input.positionCS.z /  input.positionCS.w; // default depth
	float depthLinear		= input.positionCS.z /  input.positionVS.w; // linear depth	
	float2 texCoord 		= float2(input.uv.x * materialTiling.x, input.uv.y * materialTiling.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= materialRoughness;
	float metallic 			= materialMetallic;
	float reflectivity 		= clamp(materialReflectivity, 0.03f, 1.0f);
	float occlusion			= 1.0f - materialOcclusion;
	float4 normal			= float4(PackNormal(input.normal.xyz), occlusion);

	//= SAMPLING ================================================================================
#if HEIGHT_MAP == 1	
#endif
	
	//= SAMPLING ================================================================================
#if MASK_MAP == 1
		float3 maskSample = textures[6].Sample(samplerAnisoWrap, texCoord).rgb;
		float threshold = 0.6f;
		if (maskSample.r <= threshold && maskSample.g <= threshold && maskSample.b <= threshold)
			discard;
#endif
	
	//= SAMPLING ================================================================================
#if ALBEDO_MAP == 1
		albedo *= textures[0].Sample(samplerAnisoWrap, texCoord);
#endif
	
	//= SAMPLING ================================================================================
#if ROUGHNESS_MAP == 1
		roughness *= textures[1].Sample(samplerAnisoWrap, texCoord).r;
#endif
	
	//= SAMPLING ================================================================================
#if METALLIC_MAP == 1
		metallic *= textures[2].Sample(samplerAnisoWrap, texCoord).r;
#endif
	
	//= SAMPLING ================================================================================
#if OCCLUSION_MAP == 1
		occlusion = clamp(textures[3].Sample(samplerAnisoWrap, texCoord).r * (1 / materialOcclusion), 0.0f, 1.0f);
#endif
	
	//= SAMPLING ================================================================================
#if NORMAL_MAP == 1
		normal 	= textures[4].Sample(samplerAnisoWrap, texCoord); // sample
		normal 	= float4(NormalSampleToWorldSpace(normal, input.normal, input.tangent, materialNormalStrength), 1.0f); // transform to world space
		normal 	= float4(PackNormal(normal), 1.0f);
#endif
	//===========================================================================================
	
	//= SHADOWING ===============================================================================
	bool PCF = true;
	float shadow = 1.0f;
	
    float slopeScaledBias = bias * tan(acos(dot(normal.rgb, -lightDirection.rgb)));
	float4 lightClipSpace = mul(input.positionWS, mViewProjectionDirLight);
    shadow = ShadowMapping(textures[7], shadowSampler, lightClipSpace, slopeScaledBias, PCF);
	//===========================================================================================
	
	//= DETERMINE RENDER QUALITY ================================================================
	float textureMapping	= 0.0f; // This has been implemented
	float shadingLow		= 0.25f;
	float shadingMedium		= 0.5f;
	float shadingHigh 		= 0.75f;
	float shadingUltra 		= 1.0f; // This has been implemented
	float renderMode		= shadingUltra;
#if CUBE_MAP == 1
		renderMode			= textureMapping;
#endif
	//============================================================================================
	
	// Write to G-Buffer
	output.albedo 		= albedo;
	output.normal 		= float4(normal.rgb, shadow);
	output.depth 		= float4(depth, depthLinear, 1.0f, 1.0f);
	output.material		= float4(roughness, metallic, reflectivity, renderMode);
		
    return output;
}