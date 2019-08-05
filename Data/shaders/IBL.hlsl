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

//= INCLUDES =======
#include "BRDF.hlsl"
//==================

static const float tex_maxMip = 11.0f;
static const float2 environmentMipSize[12] =
{
    float2(4096, 2048),
	float2(2048, 1024),
	float2(1024, 512),
	float2(512, 256),
	float2(256, 128),
	float2(128, 64),
	float2(64, 32),
	float2(32, 16),
	float2(16, 8),
	float2(8, 4),
	float2(4, 2),
	float2(2, 1),
};

float3 GetSpecularDominantDir(float3 normal, float3 reflection, float roughness)
{
	const float smoothness = 1.0f - roughness;
	const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
	return lerp(normal, reflection, lerpFactor);
}

// https://www.unrealengine.com/blog/physically-based-shading-on-mobile
float3 EnvBRDFApprox(float3 specColor, float roughness, float NdV)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f );
    const float4 c1 = float4(1.0f, 0.0425f, 1.0f, -0.04f );
    float4 r 		= roughness * c0 + c1;
    float a004 		= min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB 		= float2(-1.04f, 1.04f) * a004 + r.zw;
    return specColor * AB.x + AB.y;
}

float3 ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, Texture2D tex_environment, Texture2D tex_lutIBL, SamplerState sampler_linear, SamplerState sampler_trilinear)
{
	float3 reflection 	= reflect(camera_to_pixel, normal);
	// From Sebastien Lagarde Moving Frostbite to PBR page 69
	reflection	= GetSpecularDominantDir(normal, reflection, material.roughness);

	float NdV 	= saturate(dot(-camera_to_pixel, normal));
	float3 F 	= Fresnel_Schlick_Roughness(NdV, material.F0, material.roughness);

	float3 kS 	= F; 			// The energy of light that gets reflected
	float3 kD 	= 1.0f - kS;	// Remaining energy, light that gets refracted
	kD 			*= 1.0f - material.metallic;	

	// Diffuse
	float3 irradiance	= abs(tex_environment.SampleLevel(sampler_linear,  directionToSphereUV(normal), 11).rgb);
	float3 cDiffuse		= irradiance * material.albedo;

	// Specular
	float alpha 			= max(0.001f, material.roughness * material.roughness);
	float mipLevel 			= alpha * tex_maxMip;
	float3 prefilteredColor	= tex_environment.SampleLevel(sampler_trilinear,  directionToSphereUV(reflection), mipLevel).rgb;
	float2 envBRDF  		= tex_lutIBL.Sample(sampler_linear, float2(NdV, material.roughness)).xy;
	float3 cSpecular 		= prefilteredColor * (F * envBRDF.x + envBRDF.y);

	return kD * cDiffuse + cSpecular; 
}