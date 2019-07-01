/*
Copyright(c) 2016-2019 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

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

//= SAMPLERS ======================================
SamplerState sampler_linear_clamp	: register(s0);
SamplerState sampler_trlinear_clamp	: register(s1);
SamplerState sampler_point_clamp	: register(s2);
//=================================================

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
#include "BRDF.hlsl"
#include "IBL.hlsl"
#include "SSR.hlsl"
//====================

Pixel_PosUv mainVS(Vertex_PosUv input)
{
    Pixel_PosUv output;
    
    input.position.w    = 1.0f;
    output.position     = mul(input.position, mWorldViewProjection);
    output.uv           = input.uv;
	
    return output;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    float2 texCoord	= input.uv;
    float3 color	= float3(0, 0, 0);
	
	// Sample from textures
    float4 albedo       		= degamma(texAlbedo.Sample(sampler_point_clamp, texCoord));
    float4 normalSample 		= texNormal.Sample(sampler_point_clamp, texCoord);
	float3 normal				= normal_decode(normalSample.xyz);
	float4 materialSample   	= texMaterial.Sample(sampler_point_clamp, texCoord);
    float occlusion_texture 	= normalSample.w;
	float occlusion_ssao		= texSSAO.Sample(sampler_point_clamp, texCoord).r; 
	float shadow_directional	= texShadows.Sample(sampler_point_clamp, texCoord).r;

	// Create material
    Material material;
    material.albedo     		= albedo.rgb;
    material.roughness  		= materialSample.r;
    material.metallic   		= materialSample.g;
    material.emissive   		= materialSample.b;
	material.F0 				= lerp(0.04f, material.albedo, material.metallic);
	material.roughness_alpha 	= max(0.001f, material.roughness * material.roughness);

	// Compute common values
    float depth  			= texDepth.Sample(sampler_point_clamp, texCoord).r;
    float3 worldPos 		= get_world_position_from_depth(depth, mViewProjectionInverse, texCoord);
    float3 camera_to_pixel  = normalize(worldPos.xyz - g_camera_position.xyz);

	// Sky
    if (materialSample.a == 0.0f)
    {
        color = texEnvironment.Sample(sampler_linear_clamp, directionToSphereUV(camera_to_pixel)).rgb;
        color *= clamp(dirLightIntensity.r, 0.01f, 1.0f);
        return float4(color, 1.0f);
    }

	//= Ambient light ==========================================================================================
	float factor_occlusion 		= occlusion_texture == 1.0f ? occlusion_ssao : occlusion_texture;
	float factor_self_shadowing = shadow_directional * saturate(dot(normal, normalize(-dirLightDirection).xyz));
	float factor_sky_light		= clamp(dirLightIntensity.r * factor_self_shadowing, 0.025f, 1.0f);
	float ambient_light 		= factor_sky_light * factor_occlusion;
	//==========================================================================================================

	//= IBL - Image-based lighting =========================================================================================================================
    color += ImageBasedLighting(material, normal, camera_to_pixel, texEnvironment, texLutIBL, sampler_linear_clamp, sampler_trlinear_clamp) * ambient_light;
	//======================================================================================================================================================

	//= SSR - Screen space reflections ==============================================
	if (padding2.x != 0.0f)
	{
		float3 ssr = SSR(worldPos, normal, texCoord, material.roughness, texFrame, texDepth, sampler_point_clamp);
		color += ssr * ambient_light;
	}
	//===============================================================================

	//= Emissive ============================================
    float3 emissive = material.emissive * albedo.rgb * 100.0f;
    color += emissive;
	//=======================================================

	//= Directional Light ===============================================================================================
	Light directionalLight;
    directionalLight.color      = dirLightColor.rgb; 
    directionalLight.direction  = normalize(-dirLightDirection).xyz;
	float directional_shadow	= micro_shadow(factor_occlusion, normal, directionalLight.direction, shadow_directional);
	directionalLight.intensity  = dirLightIntensity.r * directional_shadow;	
	
	// Compute illumination
	if (directionalLight.intensity > 0.0f)
	{
		color += BRDF(material, directionalLight, normal, camera_to_pixel);
	}
	//===================================================================================================================
	
	//= Point lights ====================================================
    Light pointLight;
    for (int i = 0; i < pointlightCount; i++)
    {
		// Get light data
        pointLight.color        = pointLightColor[i].rgb;
        float3 position         = pointLightPosition[i].xyz;
        pointLight.intensity    = pointLightIntenRange[i].x * factor_occlusion;
        float range             = pointLightIntenRange[i].y;
        
		// Compute light
        pointLight.direction    = normalize(position - worldPos);
        float dist              = length(worldPos - position);
        float attenuation       = saturate(1.0f - dist / range);
        attenuation             *= attenuation;
        pointLight.intensity    *= attenuation;

		// Compute illumination
        if (dist < range)
        {
            color += BRDF(material, pointLight, normal, camera_to_pixel);
        }
    }
	//===================================================================

	//= Spot Lights =========================================================================================================
    Light spotLight;
    for (int j = 0; j < spotlightCount; j++)
    {
		// Get light data
        spotLight.color     = spotLightColor[j].rgb;
        float3 position     = spotLightPosition[j].xyz;
        spotLight.intensity = spotLightIntenRangeAngle[j].x * factor_occlusion;
        spotLight.direction = normalize(-spotLightDirection[j].xyz);
        float range         = spotLightIntenRangeAngle[j].y;
        float cutoffAngle   = 1.0f - spotLightIntenRangeAngle[j].z;
        
		// Compute light
        float3 direction    = normalize(position - worldPos);
        float dist          = length(worldPos - position);
        float theta         = dot(direction, spotLight.direction);
        float epsilon       = cutoffAngle - cutoffAngle * 0.9f;
        float attunation    = saturate((theta - cutoffAngle) / epsilon); // attunate when approaching the outer cone
        attunation          *= saturate(1.0f - dist / range);
        attunation          *= attunation; // attunate with distance as well
        spotLight.intensity *= attunation;

		// Compute illumination
        if (theta > cutoffAngle)
        {
            color += BRDF(material, spotLight, normal, camera_to_pixel);
        }
    }
	//=======================================================================================================================

    return float4(color, 1.0f);
}