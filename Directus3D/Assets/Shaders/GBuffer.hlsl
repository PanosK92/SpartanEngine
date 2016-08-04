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
Texture2D lightDepthTex 	: register (t7);
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
    float materialSpecular;
    float materialShadingMode;
    float2 materialTiling;
	float2 viewport;
    float2 receiveShadowsBias;
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
	float depth1 			= input.positionCS.z / input.positionCS.w;
	float depth2 			= input.positionCS.z / input.positionWS.w;
	float2 texCoord 		= float2(input.uv.x * materialTiling.x, input.uv.y * materialTiling.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= materialRoughness;
	float metallic 			= materialMetallic;
	float specular 			= clamp(materialSpecular, 0.03f, 1.0f);
	float occlusion			= materialOcclusion;
	float4 normal			= float4(PackNormal(input.normal.xyz), occlusion);
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
	
	//= OCCLUSION ================================================================================
#if OCCLUSION_MAP
		occlusion = clamp(texOcclusion.Sample(samplerAniso, texCoord).r * (1 / materialOcclusion), 0.0f, 1.0f);
#endif
	
	//= NORMAL ==================================================================================
#if NORMAL_MAP
		normal 	= texNormal.Sample(samplerAniso, texCoord); // sample
		normal 	= float4(NormalSampleToWorldSpace(normal, input.normal, input.tangent, materialNormalStrength), 1.0f); // transform to world space
		normal 	= float4(PackNormal(normal), 1.0f);
#endif
	//============================================================================================
		
	//= CUBEMAP ==================================================================================
#if CUBE_MAP
		type = 0.1f;
#endif
	//============================================================================================
	
	
	//= SHADOW MAPPING ===========================================================================
	bool receiveShadows = receiveShadowsBias.x;
	float shadowing = 1.0f;
	if (receiveShadows)
	{
		float bias 		= receiveShadowsBias.y;
		float4 lightPos = mul(input.positionWS, mLightViewProjection);
		shadowing		= 1.0f - ShadowMappingPCF(lightDepthTex, samplerShadow, lightPos, bias);
	}
	//============================================================================================
	// Write to G-Buffer
	output.albedo 		= albedo;
	output.normal 		= float4(normal.rgb, occlusion);
	output.depth 		= float4(depth1, depth2, shadowing, 0.0f);
	output.material		= float4(roughness, metallic, specular, type);
		
    return output;
}