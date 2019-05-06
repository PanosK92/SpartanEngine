// = INCLUDES ========
#include "Common.hlsl"
//====================

//= TEXTURES ===========================
Texture2D texAlbedo 	: register (t0);
Texture2D texRoughness 	: register (t1);
Texture2D texMetallic 	: register (t2);
Texture2D texNormal 	: register (t3);
Texture2D texHeight 	: register (t4);
Texture2D texOcclusion 	: register (t5);
Texture2D texEmission 	: register (t6);
Texture2D texMask 		: register (t7);
//======================================

//= SAMPLERS =============================
SamplerState samplerAniso : register (s0);
//========================================

cbuffer MaterialBuffer : register(b1)
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
};

cbuffer ObjectBuffer : register(b2)
{		
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

	float2 texCoords 		= float2(input.uv.x * materialTiling.x + materialOffset.x, input.uv.y * materialTiling.y + materialOffset.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= abs(materialRoughness); // roughness can be negative - little trick that signifies a specular texture
	float metallic 			= saturate(materialMetallic);
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
	
	#if MASK_MAP
		float3 maskSample = texMask.Sample(samplerAniso, texCoords).rgb;
		float threshold = 0.6f;
		if (maskSample.r <= threshold && maskSample.g <= threshold && maskSample.b <= threshold)
			discard;
	#endif
	
	#if ALBEDO_MAP
		albedo *= texAlbedo.Sample(samplerAniso, texCoords);
	#endif
	
	#if ROUGHNESS_MAP
		if (materialRoughness >= 0.0f)
		{
			roughness *= texRoughness.Sample(samplerAniso, texCoords).r;
		}
		else
		{
			roughness *= 1.0f - texRoughness.Sample(samplerAniso, texCoords).r;
		}
	#endif
	
	#if METALLIC_MAP
		metallic *= texMetallic.Sample(samplerAniso, texCoords).r;
	#endif
	
	#if NORMAL_MAP
		// Make TBN
		float3x3 TBN = makeTBN(input.normal, input.tangent);
	
		// Get tangent space normal and apply intensity
		float3 normalSample = normalize(unpack(texNormal.Sample(samplerAniso, texCoords).rgb));
		normalIntensity		= saturate(normalIntensity);
		normalSample.xy 	*= normalIntensity;
		normal 				= normalize(mul(normalSample, TBN).xyz); // Transform to world space
	#endif

	#if OCCLUSION_MAP
		occlusion = texOcclusion.Sample(samplerAniso, texCoords).r;
	#endif
	
	#if EMISSION_MAP
		emission = texEmission.Sample(samplerAniso, texCoords).r;
	#endif

	// Write to G-Buffer
	g_buffer.albedo		= albedo;
	g_buffer.normal 	= float4(normal_encode(normal), occlusion);
	g_buffer.material	= float4(roughness, metallic, emission, materialShadingMode);
	g_buffer.velocity	= velocity;

    return g_buffer;
}