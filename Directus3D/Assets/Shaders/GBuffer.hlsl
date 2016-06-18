#include "Normal.hlsl"
#include "ShadowMapping.hlsl"

#if COMPILE_PS == 1
/*------------------------------------------------------------------------------
								[TEXTURES]
------------------------------------------------------------------------------*/
Texture2D albedoTexture 	: register(t0);
Texture2D roughnessTexture 	: register(t1);
Texture2D metallicTexture 	: register(t2);
Texture2D occlusionTexture 	: register(t3);
Texture2D normalTexture 	: register(t4);
Texture2D heightTexture 	: register(t5);
Texture2D maskTexture 		: register(t6);
Texture2D dirLightDepthTex 	: register(t7);
/*------------------------------------------------------------------------------
								[SAMPLERS]
------------------------------------------------------------------------------*/
SamplerState samplerAnisoWrap : register (s0);
SamplerState shadowSampler    : register (s1);
#endif

/*------------------------------------------------------------------------------
								[BUFFERS]
------------------------------------------------------------------------------*/
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

/*------------------------------------------------------------------------------
								[STRUCTS]
------------------------------------------------------------------------------*/
struct VertexInputType
{
    float4 position : POSITION;
    float2 uv : TEXCOORD0;
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

#if COMPILE_PS == 1
struct PixelOutputType
{
	float4 albedo		: SV_Target0;
	float4 normal		: SV_Target1;
	float4 depth		: SV_Target2;
	float4 material		: SV_Target3;
};
#endif

/*------------------------------------------------------------------------------
									[VS()]
------------------------------------------------------------------------------*/
#if COMPILE_VS == 1
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
#endif

/*------------------------------------------------------------------------------
									[PS()]
------------------------------------------------------------------------------*/
#if COMPILE_PS == 1
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
		float3 maskSample = maskTexture.Sample(samplerAnisoWrap, texCoord).rgb;
		float threshold = 0.6f;
		if (maskSample.r <= threshold && maskSample.g <= threshold && maskSample.b <= threshold)
			discard;
#endif
	
	//= SAMPLING ================================================================================
#if ALBEDO_MAP == 1
		albedo *= albedoTexture.Sample(samplerAnisoWrap, texCoord);
#endif
	
	//= SAMPLING ================================================================================
#if ROUGHNESS_MAP == 1
		roughness *= roughnessTexture.Sample(samplerAnisoWrap, texCoord).r;
#endif
	
	//= SAMPLING ================================================================================
#if METALLIC_MAP == 1
		metallic *= metallicTexture.Sample(samplerAnisoWrap, texCoord).r;
#endif
	
	//= SAMPLING ================================================================================
#if OCCLUSION_MAP == 1
		occlusion = clamp(occlusionTexture.Sample(samplerAnisoWrap, texCoord).r * (1 / materialOcclusion), 0.0f, 1.0f);
#endif
	
	//= SAMPLING ================================================================================
#if NORMAL_MAP == 1
		normal 	= normalTexture.Sample(samplerAnisoWrap, texCoord); // sample
		normal 	= float4(NormalSampleToWorldSpace(normal, input.normal, input.tangent, materialNormalStrength), 1.0f); // transform to world space
		normal 	= float4(PackNormal(normal), 1.0f);
#endif
	//===========================================================================================
	
	//= SHADOWING ===============================================================================
	bool PCF = true;
	float shadow = 1.0f;
	
    float slopeScaledBias = bias * tan(acos(dot(normal.rgb, -lightDirection.rgb)));
	float4 lightClipSpace = mul(input.positionWS, mViewProjectionDirLight);
	shadow = ShadowMapping(dirLightDepthTex, shadowSampler, lightClipSpace, slopeScaledBias, PCF);	
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
#endif