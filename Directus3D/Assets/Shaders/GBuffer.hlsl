// = INCLUDES ===============
#include "Helper.hlsl"
#include "ShadowMapping.hlsl"
//===========================

//= TEXTURES ===============================
Texture2D texAlbedo 		: register (t0);
Texture2D texRoughness 		: register (t1);
Texture2D texMetallic 		: register (t2);
Texture2D texNormal 		: register (t3);
Texture2D texHeight 		: register (t4);
Texture2D texOcclusion 		: register (t5);
Texture2D texEmission 		: register (t6);
Texture2D texMask 			: register (t7);
Texture2D lightDepthTex[3] 	: register (t8);
//==========================================

//= SAMPLERS =============================
SamplerState samplerAniso : register (s0);
//========================================

//= DEFINES ======
#define CASCADES 3
//================

//= BUFFERS ==================================
cbuffer PerFrameBuffer : register(b0)
{		
	float2 viewport;
	float nearPlane;
	float farPlane;
	matrix mLightViewProjection[CASCADES];
	float4 shadowSplits;		
	float3 lightDir;
	float shadowBias;
	float shadowMapResolution;
	float shadowMappingQuality;	
	float2 padding3;
};

cbuffer PerMaterialBuffer : register(b1)
{		
    float4 materialAlbedoColor;
	float2 materialTiling;
	float2 materialOffset;
    float materialRoughness;
    float materialMetallic;
    float materialOcclusion;
    float materialNormalStrength;
    float materialSpecular;
    float materialShadingMode; 
	float2 padding2;
};

cbuffer PerObjectBuffer : register(b2)
{
	matrix mWorld;
    matrix mWorldView;
    matrix mWorldViewProjection;
	float receiveShadows;
	float3 padding1;
}
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
	float depth1 			= input.positionCS.z / input.positionCS.w;
	float depth2 			= input.positionCS.z / input.positionWS.w;
	float2 texCoord 		= float2(input.uv.x * materialTiling.x + materialOffset.x, input.uv.y * materialTiling.y + materialOffset.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= materialRoughness;
	float metallic 			= materialMetallic;
	float specular 			= clamp(materialSpecular, 0.03f, 1.0f);
	float emission			= 0.0f;
	float flippedOcclusion	= 1.0f - materialOcclusion;
	float4 normal			= float4(PackNormal(input.normal.xyz), flippedOcclusion);
	float type				= 0.0f; // pbr mesh
	
	//= TYPE CODES ============================
	// 0.0 = Default mesh 	-> PBR
	// 0.1 = CubeMap 		-> texture mapping
	//=========================================

	//= HEIGHT ==================================================================================
#if HEIGHT_MAP
#endif
	
	//= MASK ====================================================================================
#if MASK_MAP
		float3 maskSample = texMask.Sample(samplerAniso, texCoord).rgb;
		float threshold = 0.6f;
		if (maskSample.r <= threshold && maskSample.g <= threshold && maskSample.b <= threshold)
			discard;
#endif
	
	//= ALBEDO ==================================================================================
#if ALBEDO_MAP
		albedo *= texAlbedo.Sample(samplerAniso, texCoord);
#endif
	
	//= ROUGHNESS ===============================================================================
#if ROUGHNESS_MAP
		roughness *= texRoughness.Sample(samplerAniso, texCoord).r;
#endif
	
	//= METALLIC ================================================================================
#if METALLIC_MAP
		metallic *= texMetallic.Sample(samplerAniso, texCoord).r;
#endif
	
	//= NORMAL ==================================================================================
#if NORMAL_MAP
		normal 	= texNormal.Sample(samplerAniso, texCoord); // sample
		normal 	= float4(NormalSampleToWorldSpace(normal, input.normal, input.tangent, materialNormalStrength), 1.0f); // transform to world space
		normal 	= float4(PackNormal(normal), 1.0f);
#endif
	//============================================================================================
	
	//= OCCLUSION ================================================================================
#if OCCLUSION_MAP
		flippedOcclusion = clamp(texOcclusion.Sample(samplerAniso, texCoord).r * (1.0f / (flippedOcclusion)), 0.0f, 1.0f);
#endif
	
	//= EMISSION ================================================================================
#if EMISSION_MAP
		emission = texEmission.Sample(samplerAniso, texCoord).r;
#endif
		
	//= CUBEMAP ==================================================================================
#if CUBE_MAP
		type = 0.1f;
#endif
	//============================================================================================

	//= SHADOW MAPPING ===========================================================================	
	float shadowing = 1.0f;
	int cascadeIndex;	
	if (receiveShadows == 1.0f && shadowMappingQuality != 0.0f)
	{
		float z = depth2;
		
		if (z <= shadowSplits.x)
			cascadeIndex = 0;		
		else if (z <= shadowSplits.y)
			cascadeIndex = 1;	
		else
			cascadeIndex = 2;		
		
		if (cascadeIndex == 0)
		{
			float4 lightPos = mul(input.positionWS, mLightViewProjection[0]);
			shadowing		= ShadowMapping(lightDepthTex[0], samplerAniso, shadowMapResolution, shadowMappingQuality, lightPos, shadowBias, normal.rgb, lightDir);
		}
		else if (cascadeIndex == 1)
		{
			float4 lightPos = mul(input.positionWS, mLightViewProjection[1]);
			shadowing		= ShadowMapping(lightDepthTex[1], samplerAniso, shadowMapResolution, shadowMappingQuality, lightPos, shadowBias, normal.rgb, lightDir);
		}
		else if (cascadeIndex == 2)
		{
			float4 lightPos = mul(input.positionWS, mLightViewProjection[2]);
			shadowing		= ShadowMapping(lightDepthTex[2], samplerAniso, shadowMapResolution, shadowMappingQuality, lightPos, shadowBias, normal.rgb, lightDir);
		}
	}
	//============================================================================================
	
	float totalShadowing = clamp(flippedOcclusion * shadowing, 0.0f, 1.0f);

	// Write to G-Buffer
	output.albedo		= albedo;
	output.normal 		= float4(normal.rgb, totalShadowing);
	output.depth 		= float4(depth1, depth2, emission, 0.0f);
	output.material		= float4(roughness, metallic, specular, type);
		
	// Uncomment to vizualize cascade splits 
	if (cascadeIndex == 0)
		output.albedo		= float4(1,0,0,1);
	if (cascadeIndex == 1)
		output.albedo		= float4(0,1,0,1);
	if (cascadeIndex == 2)
		output.albedo		= float4(0,0,1,1);
		
    return output;
}