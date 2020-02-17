/*
Copyright(c) 2016-2020 Panos Karabelas

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

float3 SampleEnvironment(Texture2D tex_environment, float2 uv, float mip_level)
{
	// We are currently using a spherical environment map which has a 2:1 ratio, so at the smallest 
	// mipmap we have to do a bit of blending otherwise we'll get a visible seem in the middle.
	if (mip_level == mip_max)
	{
		float2 mip_size	= float2(2, 1);
		float dx 		= mip_size.x;
	
		float3 tl = (tex_environment.SampleLevel(sampler_trilinear_clamp, uv + float2(-dx, 0.0f), mip_level).rgb);
		float3 tr = (tex_environment.SampleLevel(sampler_trilinear_clamp, uv + float2(dx, 0.0f), mip_level).rgb);
		return (tl + tr) / 2.0f;
	}
	
	return (tex_environment.SampleLevel(sampler_trilinear_clamp, uv, mip_level).rgb);
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
    // [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f );
    const float4 c1 = float4(1.0f, 0.0425f, 1.0f, -0.04f );
    float4 r 		= roughness * c0 + c1;
    float a004 		= min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB 		= float2(-1.04f, 1.04f) * a004 + r.zw;
   
    return specColor * AB.x + AB.y;
}

float3 Brdf_Diffuse_Ibl(Material material, float3 normal, Texture2D tex_environment)
{
    return SampleEnvironment(tex_environment, direction_sphere_uv(normal), mip_max) * material.albedo;
}

float3 Brdf_Specular_Ibl(Material material, float3 normal, float3 camera_to_pixel, Texture2D tex_environment, Texture2D tex_lutIBL, out float3 F)
{
	float3 reflection 	    = reflect(camera_to_pixel, normal);	
    reflection              = GetSpecularDominantDir(normal, reflect(camera_to_pixel, normal), material.roughness); // From Sebastien Lagarde Moving Frostbite to PBR page 69
    float NdV               = saturate(dot(-camera_to_pixel, normal));
    F                       = Fresnel_Schlick_Roughness(NdV, material.F0, material.roughness);
	float alpha 			= max(EPSILON, material.roughness * material.roughness);
    float mip_level         = lerp(0, mip_max, alpha);
	float3 prefilteredColor	= SampleEnvironment(tex_environment, direction_sphere_uv(reflection), mip_level);
	float2 envBRDF  		= tex_lutIBL.Sample(sampler_bilinear_clamp, float2(NdV, material.roughness)).xy;
	float3 reflectivity		= F * envBRDF.x + envBRDF.y;
    return prefilteredColor * reflectivity;
}
