// = INCLUDES ========
#include "Common.hlsl"
#include "Vertex.hlsl"
//====================

//= TEXTURES ===============================
Texture2D texAlbedo 		: register (t0);
Texture2D texRoughness 		: register (t1);
Texture2D texMetallic 		: register (t2);
Texture2D texNormal 		: register (t3);
Texture2D texHeight 		: register (t4);
Texture2D texOcclusion 		: register (t5);
Texture2D texEmission 		: register (t6);
Texture2D texMask 			: register (t7);
//==========================================

//= SAMPLERS =============================
SamplerState samplerAniso : register (s0);
//========================================

cbuffer PerObjectBuffer : register(b1)
{		
    float4 materialAlbedoColor;	
	float2 materialTiling;
	float2 materialOffset;
    float materialRoughness;
    float materialMetallic;
    float materialNormalStrength;
	float materialHeight;
	float materialShadingMode;
	float3 padding2;
	matrix mModel;
	matrix mMVP_current;
	matrix mMVP_previous;	
};

struct PixelInputType
{
    float4 positionCS 			: SV_POSITION;
    float2 uv 					: TEXCOORD;
    float3 normal 				: NORMAL;
    float3 tangent 				: TANGENT;
	float4 positionVS 			: POSITIONT0;
    float4 positionWS 			: POSITIONT1;
	float4 positionCS_Current 	: SCREEN_POS;
	float4 positionCS_Previous 	: SCREEN_POS_PREVIOUS;
};

struct PixelOutputType
{
	float4 albedo	: SV_Target0;
	float4 normal	: SV_Target1;
	float4 material	: SV_Target2;
	float2 velocity	: SV_Target3;
	float2 depth	: SV_Target4;
};

PixelInputType mainVS(Vertex_PosUvNorTan input)
{
    PixelInputType output;
    
    input.position.w 			= 1.0f;	
	output.positionWS 			= mul(input.position, mModel);
    output.positionVS   		= mul(output.positionWS, g_view);
    output.positionCS   		= mul(output.positionVS, g_projection);
	output.positionCS_Current 	= mul(input.position, mMVP_current);
	output.positionCS_Previous 	= mul(input.position, mMVP_previous);
	output.normal 				= normalize(mul(input.normal, (float3x3)mModel)).xyz;	
	output.tangent 				= normalize(mul(input.tangent, (float3x3)mModel)).xyz;
    output.uv 					= input.uv;
	
	return output;
}

PixelOutputType mainPS(PixelInputType input)
{
	PixelOutputType g_buffer;

    float depth_linear  	= input.positionVS.z / g_camera_far;
    float depth_cs      	= input.positionCS.z / input.positionVS.w;
	float2 texCoords 		= float2(input.uv.x * materialTiling.x + materialOffset.x, input.uv.y * materialTiling.y + materialOffset.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= clamp(materialRoughness, 0.0001f, 1.0f);
	float metallic 			= clamp(materialMetallic, 0.0001f, 1.0f);
	float3 normal			= input.normal.xyz;
	float normalIntensity	= clamp(materialNormalStrength, 0.012f, materialNormalStrength);
	float emission			= 0.0f;
	float occlusion			= 1.0f;	
	
	//= VELOCITY ==============================================================================
	float2 position_current 	= (input.positionCS_Current.xy / input.positionCS_Current.w);
	float2 position_previous 	= (input.positionCS_Previous.xy / input.positionCS_Previous.w);
	float2 position_delta		= position_current - position_previous;
    float2 velocity 			= (position_delta - g_taa_jitterOffset) * float2(0.5f, -0.5f);
	//=========================================================================================

	//= HEIGHT ==================================================================================
	#if HEIGHT_MAP
		// Parallax Mapping
		float height_scale 	= materialHeight * 0.01f;
		float3 viewDir 		= normalize(g_camera_position - input.positionWS.xyz);
		float height 		= texHeight.Sample(samplerAniso, texCoords).r;
		float2 offset 		= viewDir.xy * (height * height_scale);
		if(texCoords.x <= 1.0 && texCoords.y <= 1.0 && texCoords.x >= 0.0 && texCoords.y >= 0.0)
		{
			texCoords += offset;
		}
	#endif
	
	//= MASK ====================================================================================
	#if MASK_MAP
		float3 maskSample = texMask.Sample(samplerAniso, texCoords).rgb;
		float threshold = 0.6f;
		if (maskSample.r <= threshold && maskSample.g <= threshold && maskSample.b <= threshold)
			discard;
	#endif
	
	//= ALBEDO ==================================================================================
	#if ALBEDO_MAP
		albedo *= texAlbedo.Sample(samplerAniso, texCoords);
	#endif
	
	//= ROUGHNESS ===============================================================================
	#if ROUGHNESS_MAP
		roughness *= texRoughness.Sample(samplerAniso, texCoords).r;
	#endif
	
	//= METALLIC ================================================================================
	#if METALLIC_MAP
		metallic *= texMetallic.Sample(samplerAniso, texCoords).r;
	#endif
	
	//= NORMAL ================================================================================
	#if NORMAL_MAP
		// Make TBN
		float3x3 TBN = MakeTBN(input.normal, input.tangent);
	
		// Get tangent space normal and apply intensity
		float3 normalSample = normalize(Unpack(texNormal.Sample(samplerAniso, texCoords).rgb));
		normalIntensity		= clamp(normalIntensity, 0.01f, 1.0f);
		normalSample.x 		*= normalIntensity;
		normalSample.y 		*= normalIntensity;
		
		// Transform to world space
		normal = normalize(mul(normalSample, TBN).xyz);
	#endif
	//=========================================================================================
	
	//= OCCLUSION ================================================================================
	#if OCCLUSION_MAP
		occlusion = texOcclusion.Sample(samplerAniso, texCoords).r;
	#endif
	
	//= EMISSION ================================================================================
	#if EMISSION_MAP
		emission = texEmission.Sample(samplerAniso, texCoords).r;
	#endif

	// Write to G-Buffer
	g_buffer.albedo		= albedo;
	g_buffer.normal 	= float4(Normal_Encode(normal), occlusion);
	g_buffer.material	= float4(roughness, metallic, emission, materialShadingMode);
	g_buffer.velocity	= velocity;
	g_buffer.depth      = float2(depth_linear, depth_cs);

    return g_buffer;
}