//= TEXTURES ==============================
Texture2D texAlbedo 		: register(t0);
Texture2D texNormal 		: register(t1);
Texture2D texDepth 			: register(t2);
Texture2D texMaterial 		: register(t3);
Texture2D texShadows 		: register(t4);
Texture2D texSSDO 			: register(t5);
Texture2D texFrame 			: register(t6);
Texture2D environmentTex 	: register(t7);
//=========================================

//= SAMPLERS ===================================
SamplerState samplerLinear : register(s0);
SamplerState sampler_point_clamp : register(s1);
//==============================================

//= CONSTANT BUFFERS ==========================
#define MaxLights 64
cbuffer MiscBuffer : register(b0)
{
    matrix mWorldViewProjection;
	matrix mView;
	matrix mProjection;
    matrix mViewProjectionInverse;
    float4 cameraPosWS;
	
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
    float nearPlane;
    float farPlane;
	
    float2 resolution;
    float2 padding;
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
    float4 albedo       	= ToLinear(texAlbedo.Sample(samplerLinear, texCoord));
    float4 normalSample 	= texNormal.Sample(samplerLinear, texCoord);
    float occlusionTex  	= normalSample.w;
    float3 normal			= normalize(UnpackNormal(normalSample.xyz));
    float4 materialSample   = texMaterial.Sample(samplerLinear, texCoord);

	// Create material
    Material material;
    material.albedo     	= albedo.rgb;
    material.roughness  	= materialSample.r;
    material.metallic   	= materialSample.g;
    material.emission   	= materialSample.b;
	material.color_diffuse 	= (1.0f - material.metallic) * material.albedo;	
	material.color_specular = lerp(0.03f, material.albedo, material.metallic); // Aka F0
	material.alpha 			= max(0.001f, material.roughness * material.roughness);	
	
	// Compute common values
    float2 depth  			= texDepth.Sample(samplerLinear, texCoord).rg;
    float3 worldPos 		= ReconstructPositionWorld(depth.g, mViewProjectionInverse, texCoord);
    float3 camera_to_pixel  = normalize(worldPos.xyz - cameraPosWS.xyz);

    if (materialSample.a == 0.0f) // Render technique
    {
        finalColor = environmentTex.Sample(samplerLinear, DirectionToSphereUV(camera_to_pixel)).rgb;
        finalColor *= clamp(dirLightIntensity.r, 0.01f, 1.0f); // some totally fake day/night effect	
        return float4(finalColor, 1.0f);
    }

	//= DIRECTIONAL LIGHT ==================================================
	// Directional shadow
    float dirShadow	= texShadows.Sample(samplerLinear, texCoord).r;
    
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

	// Dirty and ugly, just the way I like it
	float ambientTerm 	= 0.1f;
	ambientTerm 		= clamp(saturate(dirLightIntensity.r), ambientTerm, 1.0f);
	
	// SSR - screen space reflections
	if (padding.x != 0.0f)
	{
		float4 ssr	= SSR(worldPos, normal, farPlane, mView, mProjection, texFrame, texDepth, sampler_point_clamp);
		finalColor += ssr.xyz * (1.0f - material.roughness)  * ambientTerm;
	}
	
	// IBL - Image based lighting
    finalColor 	+= ImageBasedLighting(material, normal, camera_to_pixel, environmentTex, samplerLinear, ambientTerm);

	// SDDO - Screen space directional occlusion
	float4 ssdo			= texSSDO.Sample(samplerLinear, texCoord);
    float3 occlusion	= ssdo.a * occlusionTex;
	finalColor			+= ssdo.rgb;
	finalColor			*= occlusion;

	// Emission
    float3 emission = material.emission * albedo.rgb * 20.0f;
    finalColor 		+= emission;

    return float4(finalColor, 1.0f);
}