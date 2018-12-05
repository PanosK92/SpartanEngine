//= TEXTURES ==============================
Texture2D texAlbedo 		: register(t0);
Texture2D texNormal 		: register(t1);
Texture2D texDepth 			: register(t2);
Texture2D texMaterial 		: register(t3);
Texture2D texShadows 		: register(t4);
Texture2D texSSAO 			: register(t5);
Texture2D texFrame 			: register(t6);
Texture2D texEnvironment 	: register(t7);
Texture2D texLutIBL			: register(t8);
//=========================================

//= SAMPLERS ===================================
SamplerState samplerLinear : register(s0);
SamplerState sampler_point_clamp : register(s1);
//==============================================

//= CONSTANT BUFFERS ==========================
#define MaxLights 64
cbuffer MiscBuffer : register(b1)
{
    matrix mWorldViewProjection;
    matrix mViewProjectionInverse;

    float4 dirLightColor;
    float4 dirLightIntensity;
    float4 dirLightDirection;

    float4 pointLightPosition[MaxLights];
    float4 pointLightColor[MaxLights];
    float4 pointLightIntenRange[MaxLights];
	
    float4 spotLightColor[MaxLights];
    float4 spotLightPosition[MaxLights];
    float4 spotLightDirection[MaxLights];
    float4 spotLightIntenRangeAngle[MaxLights];
	
    float pointlightCount;
    float spotlightCount;
    float2 padding2;
};
//=============================================

// = INCLUDES ========
#include "Common.hlsl"
#include "Vertex.hlsl"
#include "Sky.hlsl"
#include "BRDF.hlsl"
#include "IBL.hlsl"
#include "SSR.hlsl"
//====================

struct PixelInputType
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD;
};

PixelInputType mainVS(Vertex_PosUv input)
{
    PixelInputType output;
    
    input.position.w    = 1.0f;
    output.position     = mul(input.position, mWorldViewProjection);
    output.uv           = input.uv;
	
    return output;
}

float4 mainPS(PixelInputType input) : SV_TARGET
{
    float2 texCoord     = input.uv;
    float3 finalColor   = float3(0, 0, 0);
	
	// Sample from G-Buffer
    float4 albedo       	= Degamma(texAlbedo.Sample(samplerLinear, texCoord));
    float4 normalSample 	= texNormal.Sample(samplerLinear, texCoord);
	float3 normal			= Unpack(normalSample.xyz);
    float occlusionTex  	= normalSample.w;
    float4 materialSample   = texMaterial.Sample(samplerLinear, texCoord);

	// Create material
    Material material;
    material.albedo     	= albedo.rgb;
    material.roughness  	= materialSample.r;
    material.metallic   	= materialSample.g;
    material.emission   	= materialSample.b;
	material.F0 			= lerp(0.04f, material.albedo, material.metallic); // Aka F0
	material.alpha 			= max(0.001f, material.roughness * material.roughness);	
	
	// Compute common values
    float2 depth  			= texDepth.Sample(samplerLinear, texCoord).rg;
    float3 worldPos 		= ReconstructPositionWorld(depth.g, mViewProjectionInverse, texCoord);
    float3 camera_to_pixel  = normalize(worldPos.xyz - g_camera_position.xyz);

    if (materialSample.a == 0.0f) // Render technique
    {
        finalColor = texEnvironment.Sample(samplerLinear, DirectionToSphereUV(camera_to_pixel)).rgb;
        finalColor *= clamp(dirLightIntensity.r, 0.01f, 1.0f); // some totally fake day/night effect	
        return float4(finalColor, 1.0f);
    }
	
	//= OCCLUSION =========================================================
	float dirShadow			= texShadows.Sample(samplerLinear, texCoord).r;
	
	// SSAO
	float4 ssao				= texSSAO.Sample(samplerLinear, texCoord);
    float ssao_occlusion	= ssao.a;
	float3 ssao_color		= ssao.rgb;
	
	// Accumulate everything
	float occlusion_total	= 1.0f;
	occlusion_total			-= (1.0f - dirShadow);
	occlusion_total			-= (1.0f - occlusionTex);
	occlusion_total			-= (1.0f - ssao_occlusion);
	occlusion_total 		= max(0.1f, saturate(occlusion_total) * dirLightIntensity.r);
	
	// Cheap, dirty and pretty light bleeding from ssao
	finalColor += ssao_color * occlusion_total;
	//=====================================================================
	
	//= DIRECTIONAL LIGHT ==================================================
	Light directionalLight;

	// Compute
    directionalLight.color      = dirLightColor.rgb;
    directionalLight.intensity  = dirLightIntensity.r * dirShadow;
    directionalLight.direction  = normalize(-dirLightDirection).xyz;

	// Compute illumination
    finalColor += BRDF(material, directionalLight, normal, camera_to_pixel);
	//======================================================================
	
	//= POINT LIGHTS =========================================================
    Light pointLight;
    for (int i = 0; i < pointlightCount; i++)
    {
		// Get light data
        pointLight.color        = pointLightColor[i].rgb;
        float3 position         = pointLightPosition[i].xyz;
        pointLight.intensity    = pointLightIntenRange[i].x;
        float range             = pointLightIntenRange[i].y;
        
		// Compute light
        pointLight.direction    = normalize(position - worldPos);
        float dist              = length(worldPos - position);
        float attunation        = clamp(1.0f - dist / range, 0.0f, 1.0f);
        attunation              *= attunation;
        pointLight.intensity    *= attunation;

		// Compute illumination
        if (dist < range)
        {
            finalColor += BRDF(material, pointLight, normal, camera_to_pixel);
        }
    }
	//========================================================================

	//= SPOT LIGHTS =========================================================================================================
    Light spotLight;
    for (int j = 0; j < spotlightCount; j++)
    {
		// Get light data
        spotLight.color     = spotLightColor[j].rgb;
        float3 position     = spotLightPosition[j].xyz;
        spotLight.intensity = spotLightIntenRangeAngle[j].x;
        spotLight.direction = normalize(-spotLightDirection[j].xyz);
        float range         = spotLightIntenRangeAngle[j].y;
        float cutoffAngle   = 1.0f - spotLightIntenRangeAngle[j].z;
        
		// Compute light
        float3 direction    = normalize(position - worldPos);
        float dist          = length(worldPos - position);
        float theta         = dot(direction, spotLight.direction);
        float epsilon       = cutoffAngle - cutoffAngle * 0.9f;
        float attunation    = clamp((theta - cutoffAngle) / epsilon, 0.0f, 1.0f); // attunate when approaching the outer cone
        attunation          *= clamp(1.0f - dist / range, 0.0f, 1.0f);
        attunation          *= attunation; // attunate with distance as well
        spotLight.intensity *= attunation;

		// Compute illumination
        if (theta > cutoffAngle)
        {
            finalColor += BRDF(material, spotLight, normal, camera_to_pixel);
        }
    }
	//=======================================================================================================================

	// SSR - screen space reflections
	if (padding2.x != 0.0f)
	{
		float4 ssr	= SSR(worldPos, normal, texFrame, texDepth, sampler_point_clamp);
		finalColor += ssr.xyz * (1.0f - material.roughness)  * occlusion_total;
	}
	
	// IBL - Image based lighting
    finalColor 	+= ImageBasedLighting(material, normal, camera_to_pixel, texEnvironment, texLutIBL, samplerLinear, occlusion_total);

	// Emission
    float3 emission = material.emission * albedo.rgb * 20.0f;
    finalColor 		+= emission;

    return float4(finalColor, 1.0f);
}