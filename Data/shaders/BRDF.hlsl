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

// F - Fresnel
float3 Fresnel_Schlick(float HdV, float3 f0)
{
	return f0 + (1.0f - f0) * pow(1.0f - HdV, 5.0f);
}

float3 Fresnel_Schlick_Roughness(float cosTheta, float3 F0, float roughness)
{
	float val = 1.0 - roughness;
    return F0 + (max(float3(val, val, val), F0) - F0) * pow(1.0f - cosTheta, 5.0f);
}   

// G - Geometric shadowing
float Geometry_Schlick_GGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0);
    float k = (r*r) / 8.0;

    float num   = NdotV;
    float denom = NdotV * (1.0 - k) + k;
	
    return num / denom;
}

float Geometry_Smith(float NdotV, float NdotL, float roughness)
{
    float ggx2  = Geometry_Schlick_GGX(NdotV, roughness);
    float ggx1  = Geometry_Schlick_GGX(NdotL, roughness);
	
    return ggx1 * ggx2;
}

// D - Normal distribution
float Distribution_GGX(float NdotH, float a)
{
    float a2     = a * a;
    float NdotH2 = NdotH * NdotH;
	
    float num   = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom 		= PI * denom * denom;
	
    return num / denom;
}

//= INCLUDES ===================
#include "BRDF_IBL.hlsl"
#include "BRDF_Environment.hlsl"
//==============================

float3 Diffuse_Lambert( float3 diffuse_color )
{
	return diffuse_color * (1 / PI);
}

// [Burley 2012, "Physically-Based Shading at Disney"]
float3 Diffuse_Burley(float3 diffuse_color, float Roughness, float NoV, float NoL, float VoH)
{
	float FD90 = 0.5 + 2 * VoH * VoH * Roughness;
	float FdV = 1 + (FD90 - 1) * pow(1 - NoV, 5);
	float FdL = 1 + (FD90 - 1) * pow(1 - NoL, 5);
	return diffuse_color * ( (1 / PI) * FdV * FdL );
}

// Diffuse - [Gotanda 2012, "Beyond a Simple Physically Based Blinn-Phong Model in Real-Time"]
float3 Diffuse_OrenNayar(float3 diffuse_color, float Roughness, float NoV, float NoL, float VoH)
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

float3 BRDF_Specular(Material material, float n_dot_v, float n_dot_l, float n_dot_h, float v_dot_h, out float3 F)
{
	float alpha 		= max(0.001f, material.roughness * material.roughness);
	F 					= Fresnel_Schlick(v_dot_h, material.F0);
    float G 			= Geometry_Smith(n_dot_v, n_dot_l, material.roughness);
    float D 			= Distribution_GGX(n_dot_h, alpha);
	float3 nominator 	= F * G * D;
	float denominator 	= 4.0f * n_dot_l * n_dot_v;
	return nominator / max(EPSILON, denominator);
}

float3 BRDF_Diffuse(Material material, float n_dot_v, float n_dot_l, float v_dot_h)
{
	return Diffuse_Burley(material.albedo, material.roughness, n_dot_v, n_dot_l, v_dot_h);
}