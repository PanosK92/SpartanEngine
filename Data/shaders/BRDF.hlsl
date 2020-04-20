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

//= INCLUDES =========
#include "Common.hlsl"
//====================

//= BRDF FUNCTIONS ================================================================
// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
float3 F_Schlick(float3 f0, float v_dot_h)
{
    float Fc = pow(1 - v_dot_h, 5.0f);
	// Anything less than 2% is physically impossible and is instead considered to be shadowing
    return saturate(50.0 * f0.g) * Fc + (1 - Fc) * f0;
}

inline float3 F_Schlick(float3 f0, float f90, float v_dot_h)
{
    return f0 + (f90 - f0) * pow(1.0 - v_dot_h, 5.0f);
}

// Smith term for GGX
// [Smith 1967, "Geometrical shadowing of a random rough surface"]
inline float V_Smith(float a2, float n_dot_v, float n_dot_l)
{
    float Vis_SmithV = n_dot_v + sqrt(n_dot_v * (n_dot_v - n_dot_v * a2) + a2);
    float Vis_SmithL = n_dot_l + sqrt(n_dot_l * (n_dot_l - n_dot_l * a2) + a2);
    return rcp(Vis_SmithV * Vis_SmithL);
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
inline float V_SmithJointApprox(float a2, float n_dot_v, float n_dot_l)
{
    float a             = sqrt(a2);
    float Vis_SmithV    = n_dot_l * (n_dot_v * (1 - a) + a) + EPSILON;
    float Vis_SmithL    = n_dot_v * (n_dot_l * (1 - a) + a) + EPSILON;
    return 0.5 * rcp(Vis_SmithV + Vis_SmithL);
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
inline float D_GGX(float a2, float n_dot_h)
{
    float d = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1; // 2 mad
    return a2 / (PI * d * d); // 4 mul, 1 rcp
}
//=================================================================================

//= LIGHTING ====================================================================================================
inline float3 Diffuse_Lambert(float3 diffuse_color)
{
	return diffuse_color * (1 / PI);
}

// [Burley 2012, "Physically-Based Shading at Disney"]
inline float3 Diffuse_Burley(float3 diffuse_color, float Roughness, float NoV, float NoL, float VoH)
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * pow(1 - NoV, 5);
	float FdL = 1 + (FD90 - 1) * pow(1 - NoL, 5);
	return diffuse_color * ( (1 / PI) * FdV * FdL );
}

// Diffuse - [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
inline float3 Diffuse_OrenNayar(float3 diffuse_color, float Roughness, float NoV, float NoL, float VoH)
{
	float a 	= Roughness * Roughness;
	float s 	= a;					// / ( 1.29 + 0.5 * a );
	float s2 	= s * s;
	float VoL 	= 2 * VoH * VoH - 1; 	// double angle identity
	float Cosri = VoL - NoV * NoL;
	float C1 	= 1 - 0.5 * s2 / (s2 + 0.33);
	float C2 	= 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? rcp( max( NoL, NoV + 0.0001f ) ) : 1 );
	return diffuse_color / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5 );
}

inline float3 BRDF_Diffuse(Material material, float n_dot_v, float n_dot_l, float v_dot_h)
{
    return Diffuse_Burley(material.albedo, material.roughness, n_dot_v, n_dot_l, v_dot_h);
}

inline float3 BRDF_Specular(Material material, float n_dot_v, float n_dot_l, float n_dot_h, float v_dot_h, out float3 F)
{
     // remapping and linearization
    float roughness = clamp(material.roughness, 0.089f, 1.0f);
    float a         = roughness * roughness;
    float a2        = pow(roughness, 4.0f);
    
    F       = F_Schlick(material.F0, v_dot_h);
    float V = V_SmithJointApprox(a2, n_dot_v, n_dot_l);
    float D = D_GGX(a2, n_dot_h);

	return (D * V) * F;
}
//===============================================================================================================

//= IMAGE BASED LIGHTING ====================================================================================================================================================
static const float mip_max = 11.0f;

inline float3 SampleEnvironment(Texture2D tex_environment, float2 uv, float mip_level)
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

inline float3 GetSpecularDominantDir(float3 normal, float3 reflection, float roughness)
{
	const float smoothness = 1.0f - roughness;
	const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    
	return lerp(normal, reflection, lerpFactor);
}

// https://www.unrealengine.com/blog/physically-based-shading-on-mobile
inline float3 EnvBRDFApprox(float3 specColor, float roughness, float NdV)
{
    // [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f );
    const float4 c1 = float4(1.0f, 0.0425f, 1.0f, -0.04f );
    float4 r 		= roughness * c0 + c1;
    float a004 		= min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB 		= float2(-1.04f, 1.04f) * a004 + r.zw;
   
    return specColor * AB.x + AB.y;
}

inline float3 Brdf_Diffuse_Ibl(Material material, float3 normal, Texture2D tex_environment)
{
    return SampleEnvironment(tex_environment, direction_sphere_uv(normal), mip_max) * material.albedo;
}

inline float3 Brdf_Specular_Ibl(Material material, float3 normal, float3 camera_to_pixel, Texture2D tex_environment, Texture2D tex_lutIBL, out float3 F)
{
	float3 reflection 	    = reflect(camera_to_pixel, normal);	
    reflection              = GetSpecularDominantDir(normal, reflect(camera_to_pixel, normal), material.roughness); // From Sebastien Lagarde Moving Frostbite to PBR page 69
    float n_dot_v           = dot(-camera_to_pixel, normal);
    float f90               = 0.5 + 2 * n_dot_v * n_dot_v * material.roughness;
    F                       = F_Schlick(material.F0, f90, material.roughness);
	float alpha 			= max(EPSILON, material.roughness * material.roughness);
    float mip_level         = lerp(0, mip_max, alpha);
	float3 prefilteredColor	= SampleEnvironment(tex_environment, direction_sphere_uv(reflection), mip_level);
    float2 envBRDF          = tex_lutIBL.Sample(sampler_bilinear_clamp, float2(saturate(n_dot_v), material.roughness)).xy;
	float3 reflectivity		= F * envBRDF.x + envBRDF.y;
    return prefilteredColor * reflectivity;
}
//===========================================================================================================================================================================
