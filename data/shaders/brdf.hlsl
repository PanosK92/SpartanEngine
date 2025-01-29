/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "common.hlsl"
//====================

/*------------------------------------------------------------------------------
    Diffuse
------------------------------------------------------------------------------*/

float3 Diffuse_Disney(float3 diffuse_color, float roughness, float NoV, float NoL, float VoH)
{
    float FD90 = 0.5 + 2 * VoH * VoH * roughness;
    float FdV  = 1 + (FD90 - 1) * pow(1 - NoV, 5);
    float FdL  = 1 + (FD90 - 1) * pow(1 - NoL, 5);
    return diffuse_color * ((1 / PI) * FdV * FdL);
}

float3 BRDF_Diffuse(Surface surface, AngularInfo angular_info)
{
    return Diffuse_Disney(
        surface.albedo,       // diffuse_color
        surface.roughness,    // roughness
        angular_info.n_dot_v, // NoV
        angular_info.n_dot_l, // NoL
        angular_info.v_dot_h  // VoH
    );
}

/*------------------------------------------------------------------------------
    Specular
------------------------------------------------------------------------------*/

float3 F_Schlick(const float3 f0, float3 f90, float v_dot_h)
{
    return f0 + (f90 - f0) * pow(1.0 - v_dot_h, 5.0);
}

float3 get_f90(Surface surface)
{
    return lerp(1.0f, surface.F0, surface.metallic);
}

float V_SmithGGX(float n_dot_v, float n_dot_l, float alpha2)
{
    float lambdaV = n_dot_l * sqrt(n_dot_v * (n_dot_v - n_dot_v * alpha2) + alpha2);
    float lambdaL = n_dot_v * sqrt(n_dot_l * (n_dot_l - n_dot_l * alpha2) + alpha2);

    return 0.5 / max(lambdaV + lambdaL, 1e-5);
}

float V_GGX_anisotropic_2cos(float cos_theta_m, float alpha_x, float alpha_y, float cos_phi, float sin_phi)
    {
    float cos2  = cos_theta_m * cos_theta_m;
    float sin2  = (1.0 - cos2);
    float s_x   = alpha_x * cos_phi;
    float s_y   = alpha_y * sin_phi;
    return 1.0 / max(cos_theta_m + sqrt(cos2 + (s_x * s_x + s_y * s_y) * sin2), 0.001);
}

// [Kelemen 2001, "A microfacet based coupled specular-matte brdf model with importance sampling"]
float V_Kelemen(float v_dot_h)
{
    // constant to prevent NaN
    return rcp(4 * v_dot_h * v_dot_h + 1e-5);
}

// Neubelt and Pettineo 2013, "Crafting a Next-gen Material Pipeline for The Order: 1886"
float V_Neubelt(float n_dot_v, float n_dot_l)
{
    return saturate_16(1.0 / (4.0 * (n_dot_l + n_dot_v - n_dot_l * n_dot_v)));
}

float D_GGX_Alpha(float roughness)
{
    // call of duty: WWII GGX gloss parameterization method
    // https://www.activision.com/cdn/research/siggraph_2018_opt.pdf
    // provides a wider roughness range
    
    float gloss       = 1.0 - roughness;
    float denominator = (1.0 + pow(2, 18 * gloss));
    return sqrt(2.0 / denominator);
}

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
float D_GGX(float n_dot_h, float roughness_alpha_squared)
{
    float f = (n_dot_h * roughness_alpha_squared - n_dot_h) * n_dot_h + 1.0f;
    return roughness_alpha_squared / (PI * f * f + FLT_MIN);
}

float D_GGX_Anisotropic(float cos_theta_m, float alpha_x, float alpha_y, float cos_phi, float sin_phi)
{
    float cos2  = cos_theta_m * cos_theta_m;
    float sin2  = (1.0 - cos2);
    float r_x   = cos_phi / alpha_x;
    float r_y   = sin_phi / alpha_y;
    float d     = cos2 + sin2 * (r_x * r_x + r_y * r_y);
    return saturate_16(1.0 / (PI * alpha_x * alpha_y * d * d));
}

float D_Charlie(float roughness, float NoH)
 {
    // Estevez and Kulla 2017, "Production Friendly Microfacet Sheen BRDF"
    float invAlpha  = 1.0 / roughness;
    float cos2h     = NoH * NoH;
    float sin2h     = max(1.0 - cos2h, 0.0078125); // 2^(-14/2), so sin2h^2 > 0 in fp16
    return (2.0 + invAlpha) * pow(sin2h, invAlpha * 0.5) / (2.0 * PI);
}

float3 compute_diffuse_energy(float3 F, float metallic)
{
    // used to tone down diffuse such that only non-metals have it
    
    float3 kS  = F;               // the energy of light that gets reflected - equal to fresnel
    float3 kD  = 1.0f - kS;       // remaining energy, light that gets refracted
    kD        *= 1.0f - metallic; // multiply kD by the inverse metalness such that only non-metals have diffuse lighting

    return kD;
}

float3 BRDF_Specular_Isotropic(inout Surface surface, AngularInfo angular_info)
{
    float alpha_ggx     = D_GGX_Alpha(surface.roughness);
    float  visibility   = V_SmithGGX(angular_info.n_dot_v, angular_info.n_dot_l, surface.alpha * surface.alpha);
    float  distribution = D_GGX(angular_info.n_dot_h, alpha_ggx * alpha_ggx);
    float3 fresnel      = F_Schlick(surface.F0, get_f90(surface), angular_info.v_dot_h);

    // energy conservation
    surface.diffuse_energy  *= compute_diffuse_energy(fresnel, surface.metallic);
    surface.specular_energy *= fresnel;

    return visibility * distribution * fresnel;
}

float3 BRDF_Specular_Anisotropic(inout Surface surface, AngularInfo angular_info)
{
    // construct TBN from the normal
    float3 t, b;
    find_best_axis_vectors(surface.normal, t, b);
    float3x3 TBN = float3x3(t, b, surface.normal);

    // rotate tangent and bitagent
    float rotation   = max(surface.anisotropic_rotation * PI2, FLT_MIN); // convert material property to a full rotation
    float2 direction = float2(cos(rotation), sin(rotation));             // convert rotation to direction
    t                = normalize(mul(float3(direction, 0.0f), TBN).xyz); // compute direction derived tangent
    b                = normalize(cross(surface.normal, t));              // re-compute bitangent

    float alpha_ggx = surface.roughness;
    float aspect    = sqrt(1.0 - surface.anisotropic * 0.9);
    float ax        = alpha_ggx / aspect;
    float ay        = alpha_ggx * aspect;
    float XdotH     = dot(t, angular_info.h);
    float YdotH     = dot(b, angular_info.h);
    
    // specular anisotropic BRDF
    float D  = D_GGX_Anisotropic(angular_info.n_dot_h, ax, ay, XdotH, YdotH);
    float V  = V_GGX_anisotropic_2cos(angular_info.n_dot_v, ax, ay, XdotH, YdotH) * V_GGX_anisotropic_2cos(angular_info.n_dot_v, ax, ay, XdotH, YdotH);
    float3 F = F_Schlick(surface.F0, get_f90(surface), angular_info.l_dot_h);

    surface.diffuse_energy  *= compute_diffuse_energy(F, surface.metallic);
    surface.specular_energy *= F;
    
    return D * V * F;
}

float3 BRDF_Specular_Clearcoat(inout Surface surface, AngularInfo angular_info)
{
    float roughness_alpha         = surface.clearcoat_roughness * surface.clearcoat_roughness;
    float roughness_alpha_squared = roughness_alpha * roughness_alpha;
    
    float D  = D_GGX(angular_info.n_dot_h, roughness_alpha_squared);
    float V  = V_Kelemen(angular_info.v_dot_h);
    float3 F = F_Schlick(0.04, get_f90(surface), angular_info.v_dot_h) * surface.clearcoat;

    surface.diffuse_energy  *= compute_diffuse_energy(F, surface.metallic);
    surface.specular_energy *= F;

    return D * V * F;
}

float3 BRDF_Specular_Sheen(inout Surface surface, AngularInfo angular_info)
{
    // charlie distribution for sheen
    float D = D_Charlie(surface.roughness, angular_info.n_dot_h);

    // sheen visibility term (simplified, can be adjusted based on needs)
    float V = V_Neubelt(angular_info.n_dot_v, angular_info.n_dot_l);

    // sheen fresnel term (simple Schlick approximation)
    float3 sheen_color = saturate(surface.albedo * 1.2f);
    float3 F           = F_Schlick(sheen_color, 1.0, angular_info.v_dot_h);

    // sheen energy conservation
    surface.diffuse_energy  *= compute_diffuse_energy(F, surface.metallic);
    surface.specular_energy *= F;

    // combine terms to get the sheen BRDF
    return D * V * F;
}
