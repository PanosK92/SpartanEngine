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

static const float mip_max = 11.0f;

float3 SampleEnvironment(SamplerState sampler_linear, Texture2D tex_environment, float2 uv, float mip_level)
{
	// We are currently using a spherical environment map which has a 2:1 ratio, so at the smallest 
	// mipmap we have to do a bit of blending otherwise we'll get a visible seem in the middle.
	if (mip_level == mip_max)
	{
		float2 mip_size	= float2(2, 1);
		float dx 		= mip_size.x;
	
		float3 tl = tex_environment.SampleLevel(sampler_linear, uv + float2(-dx, 0.0f), mip_level).rgb;
		float3 tr = tex_environment.SampleLevel(sampler_linear, uv + float2(dx, 0.0f), mip_level).rgb;
		return (tl + tr) / 2.0f;
	}
	
	return tex_environment.SampleLevel(sampler_linear, uv, mip_level).rgb;
}

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

float3 ImageBasedLighting(Material material, float3 normal, float3 camera_to_pixel, Texture2D tex_environment, Texture2D tex_lutIBL, SamplerState sampler_linear, SamplerState sampler_trilinear, out float3 reflectivity)
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
	float3 irradiance	= SampleEnvironment(sampler_linear, tex_environment, directionToSphereUV(normal), mip_max);
	float3 cDiffuse		= irradiance * material.albedo;

	// Specular
	float alpha 			= max(EPSILON, material.roughness * material.roughness);
	float mip_level 		= lerp(0, mip_max, material.roughness);
	float3 prefilteredColor	= SampleEnvironment(sampler_trilinear, tex_environment, directionToSphereUV(reflection), mip_level);
	float2 envBRDF  		= tex_lutIBL.Sample(sampler_linear, float2(NdV, material.roughness)).xy;
	reflectivity			= F * envBRDF.x + envBRDF.y;
	float3 cSpecular 		= prefilteredColor * reflectivity;

	return kD * cDiffuse + cSpecular; 
}