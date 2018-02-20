// = INCLUDES ===============
#include "Helper.hlsl"
#include "ShadowMapping.hlsl"
//===========================

//= DEFINES ======
#define CASCADES 3
//================

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

//= SAMPLERS ==============================
SamplerState samplerAniso : register (s0);
SamplerState samplerLinear : register (s1);
//=========================================

//= BUFFERS ==================================
cbuffer PerFrameBuffer : register(b0)
{		
	float2 resolution;
	float nearPlane;
	float farPlane;
	matrix mLightViewProjection[CASCADES];
	float4 shadowSplits;		
	float3 lightDir;
	float shadowMapResolution;
	float doShadowMapping;	
	float3 cameraPosWS;
};

cbuffer PerMaterialBuffer : register(b1)
{		
    float4 materialAlbedoColor;
	float2 materialTiling;
	float2 materialOffset;
    float materialRoughness;
    float materialMetallic;
    float materialNormalStrength;
	float materialHeight;
    float materialShadingMode;
	float3 padding1;
};

cbuffer PerObjectBuffer : register(b2)
{
	matrix mWorld;
    matrix mWorldView;
    matrix mWorldViewProjection;
	float receiveShadows;
	float3 padding2;
}
//===========================================

//= STRUCTS =================================
struct VertexInputType
{
    float4 position : POSITION;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
	float3 bitangent : BITANGENT;
};

struct PixelInputType
{
    float4 positionCS : SV_POSITION;
    float4 positionVS : POSITIONT0;
    float4 positionWS : POSITIONT1;
    float2 uv : TEXCOORD;
    float3 normal : NORMAL;
    float3 tangent : TANGENT;
	float3 bitangent : BITANGENT;
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
    
    input.position.w 	= 1.0f;	
	output.positionWS 	= mul(input.position, mWorld);
	output.positionVS 	= mul(input.position, mWorldView);
	output.positionCS 	= mul(input.position, mWorldViewProjection);	
	output.normal 		= normalize(mul(float4(input.normal, 0.0f), mWorld)).xyz;	
	output.tangent 		= normalize(mul(float4(input.tangent, 0.0f), mWorld)).xyz;
	output.bitangent 	= normalize(mul(float4(input.bitangent, 0.0f), mWorld)).xyz;
    output.uv 			= input.uv;
	
	return output;
}

PixelOutputType DirectusPixelShader(PixelInputType input)
{
	PixelOutputType output;

	float2 texel			= float2(1.0f / resolution.x, 1.0f / resolution.y);
	float depthCS 			= input.positionCS.z / input.positionCS.w;
	float depthVS 			= input.positionCS.z / input.positionWS.w;
	float2 texCoords 		= float2(input.uv.x * materialTiling.x + materialOffset.x, input.uv.y * materialTiling.y + materialOffset.y);
	float4 albedo			= materialAlbedoColor;
	float roughness 		= materialRoughness;
	float metallic 			= materialMetallic;
	float emission			= 0.0f;
	float occlusion			= 1.0f;
	float3 normal			= input.normal.xyz;
	float type				= 0.0f; // pbr mesh
	
	//= TYPE CODES ============================
	// 0.0 = Default mesh 	-> PBR
	// 0.1 = CubeMap 		-> texture mapping
	//=========================================

	//= HEIGHT ==================================================================================
#if HEIGHT_MAP
		// Parallax Mapping
		float height_scale = materialHeight * 0.01f;
		float3 viewDir = normalize(cameraPosWS - input.positionWS.xyz);
		float height = texHeight.Sample(samplerAniso, texCoords).r;
		float2 offset = viewDir * (height * height_scale);
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
	
	//= NORMAL ==================================================================================
#if NORMAL_MAP
		float3 normalSample = normalize(UnpackNormal(texNormal.Sample(samplerAniso, texCoords).rgb));
		normal = TangentToWorld(normalSample, input.normal.xyz, input.tangent.xyz, input.bitangent.xyz, materialNormalStrength);
#endif
	//============================================================================================
	
	//= OCCLUSION ================================================================================
#if OCCLUSION_MAP
		occlusion = texOcclusion.Sample(samplerAniso, texCoords).r;
#endif
	
	//= EMISSION ================================================================================
#if EMISSION_MAP
		emission = texEmission.Sample(samplerAniso, texCoords).r;
#endif
		
	//= CUBEMAP ==================================================================================
#if CUBE_MAP
		type = 0.1f;
#endif
	//============================================================================================

	//= SHADOW MAPPING ===========================================================================	
	float shadowing = 1.0f;
	int cascadeIndex = 0;
	if (receiveShadows == 1.0f && doShadowMapping != 0.0f)
	{
		float z = 1.0f - depthCS;

		cascadeIndex = 0; // assume 1st cascade as default
		cascadeIndex += step(shadowSplits.x, z); // test 2nd cascade
		cascadeIndex += step(shadowSplits.y, z); // test 3rd cascade
		
		float shadowTexel = 1.0f / shadowMapResolution;
		float bias = 0.03f * shadowTexel;
		float normalOffset = 150.0f;
		float NdotL = dot(normal, lightDir);
		float cosAngle = saturate(1.0f - NdotL);
		float3 scaledNormalOffset = normal * (normalOffset * cosAngle * shadowTexel);
		
		// Perform shadow mapping only if the polygons are back-faced
		// from the light, to avoid self-shadowing artifacts
		//if (NdotL < 0.0f)
		{
			if (cascadeIndex == 0)
			{
				float4 lightPos = mul(float4(input.positionWS.xyz + scaledNormalOffset, 1.0f), mLightViewProjection[0]);
				shadowing	= ShadowMapping(lightDepthTex[0], samplerLinear, shadowMapResolution, lightPos, normal, lightDir, bias);
			}
			else if (cascadeIndex == 1)
			{
				float4 lightPos = mul(float4(input.positionWS.xyz + scaledNormalOffset, 1.0f), mLightViewProjection[1]);
				shadowing	= ShadowMapping(lightDepthTex[1], samplerLinear, shadowMapResolution, lightPos, normal, lightDir, bias);
			}
			else if (cascadeIndex == 2)
			{
				float4 lightPos = mul(float4(input.positionWS.xyz + scaledNormalOffset, 1.0f), mLightViewProjection[2]);
				shadowing	= ShadowMapping(lightDepthTex[2], samplerLinear, shadowMapResolution, lightPos, normal, lightDir, bias);
			}
		}
	}
	//============================================================================================
	
	float totalShadowing = clamp(occlusion * shadowing, 0.0f, 1.0f);

	// Write to G-Buffer
	output.albedo		= albedo;
	output.normal 		= float4(PackNormal(normal), totalShadowing);
	output.depth 		= float4(depthCS, depthVS, emission, 0.0f);
	output.material		= float4(roughness, metallic, 0.0f, type);
		
	// Uncomment to vizualize cascade splits 
	/*if (cascadeIndex == 0)
		output.albedo		= float4(1,0,0,1);
	if (cascadeIndex == 1)
		output.albedo		= float4(0,1,0,1);
	if (cascadeIndex == 2)
		output.albedo		= float4(0,0,1,1);*/

    return output;
}