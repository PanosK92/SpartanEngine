/*
Copyright(c) 2015-2026 Panos Karabelas

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
    FRESNEL
------------------------------------------------------------------------------*/

// schlick 1994 - attempt to provide energy conserving f90
float3 compute_f90(float3 f0) { return saturate(50.0 * dot(f0, 0.33333)); }
float3 get_f90()              { return 1.0; } // for legacy/external use

float3 F_Schlick(float3 f0, float3 f90, float v_dot_h)
{
    return f0 + (f90 - f0) * pow(1.0 - v_dot_h, 5.0);
}

float F_Schlick(float f0, float f90, float v_dot_h)
{
    return f0 + (f90 - f0) * pow(1.0 - v_dot_h, 5.0);
}

/*------------------------------------------------------------------------------
    DIFFUSE
------------------------------------------------------------------------------*/

// burley 2012 - attempt to provide energy retro-reflection
float3 Diffuse_Burley(float3 diffuse_color, float roughness, float n_dot_v, float n_dot_l, float v_dot_h)
{
    float f90           = 0.5 + 2.0 * v_dot_h * v_dot_h * roughness;
    float light_scatter = F_Schlick(1.0, f90, n_dot_l);
    float view_scatter  = F_Schlick(1.0, f90, n_dot_v);
    return diffuse_color * (light_scatter * view_scatter * INV_PI);
}

float3 BRDF_Diffuse(Surface surface, AngularInfo angular_info)
{
    return Diffuse_Burley(surface.albedo, surface.roughness, angular_info.n_dot_v, angular_info.n_dot_l, angular_info.v_dot_h);
}

/*------------------------------------------------------------------------------
    DISTRIBUTION
------------------------------------------------------------------------------*/

// attempt to remap roughness to alpha - call of duty: wwii, attempt to provide wider range
float D_GGX_Alpha(float roughness)
{
    float gloss = 1.0 - roughness;
    return sqrt(2.0 / (1.0 + pow(2.0, 18.0 * gloss)));
}

// walter et al. 2007 - attempt to provide ggx/trowbridge-reitz ndf, takes alpha^2
float D_GGX(float n_dot_h, float a2)
{
    float d = n_dot_h * n_dot_h * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d + FLT_MIN);
}

// burley 2012 - attempt to provide anisotropic ggx ndf
float D_GGX_Anisotropic(float n_dot_h, float t_dot_h, float b_dot_h, float at, float ab)
{
    float d = (t_dot_h * t_dot_h) / (at * at) + (b_dot_h * b_dot_h) / (ab * ab) + n_dot_h * n_dot_h;
    return INV_PI / (at * ab * d * d + FLT_MIN);
}

// estevez and kulla 2017 - attempt to provide cloth/sheen ndf
float D_Charlie(float roughness, float n_dot_h)
{
    float inv_r = 1.0 / max(roughness, 0.001);
    float sin2h = max(1.0 - n_dot_h * n_dot_h, 0.0078125);
    return (2.0 + inv_r) * pow(sin2h, inv_r * 0.5) / PI2;
}

/*------------------------------------------------------------------------------
    VISIBILITY
------------------------------------------------------------------------------*/

// heitz 2014 - attempt to provide height-correlated smith ggx, takes alpha^2
float V_SmithGGX(float n_dot_v, float n_dot_l, float a2)
{
    float ggxv = n_dot_l * sqrt(n_dot_v * n_dot_v * (1.0 - a2) + a2);
    float ggxl = n_dot_v * sqrt(n_dot_l * n_dot_l * (1.0 - a2) + a2);
    return 0.5 / max(ggxv + ggxl, FLT_MIN);
}

// kelemen 2001 - attempt to provide efficient visibility for clearcoat
float V_Kelemen(float v_dot_h)
{
    return 0.25 / (v_dot_h * v_dot_h + FLT_MIN);
}

// neubelt and pettineo 2013 - attempt to provide cloth visibility
float V_Neubelt(float n_dot_v, float n_dot_l)
{
    return saturate_16(1.0 / (4.0 * (n_dot_l + n_dot_v - n_dot_l * n_dot_v)));
}

/*------------------------------------------------------------------------------
    ENERGY
------------------------------------------------------------------------------*/

// attempt to provide diffuse energy conservation
float3 compute_diffuse_energy(float3 F, float metallic)
{
    return (1.0 - F) * (1.0 - metallic);
}

// fdez-aguera 2019 - attempt to provide multi-scattering energy compensation
float3 compute_multiscatter_energy(float3 f0, float n_dot_v, float roughness)
{
    // approximate directional-hemispherical reflectance
    float dhr         = lerp(1.0 - roughness * 0.7, 1.0, pow(1.0 - n_dot_v, 5.0));
    float3 energy_loss = (1.0 - dhr) * (1.0 - f0);
    return 1.0 + f0 * energy_loss / (1.0 - energy_loss + FLT_MIN);
}

/*------------------------------------------------------------------------------
    SPECULAR - ISOTROPIC
------------------------------------------------------------------------------*/

float3 BRDF_Specular_Isotropic(inout Surface surface, AngularInfo angular_info)
{
    // roughness
    float a  = D_GGX_Alpha(surface.roughness);
    float a2 = a * a;
    
    // specular
    float  D   = D_GGX(angular_info.n_dot_h, a2);
    float  V   = V_SmithGGX(angular_info.n_dot_v, angular_info.n_dot_l, a2);
    float3 F   = F_Schlick(surface.F0, compute_f90(surface.F0), angular_info.v_dot_h);
    float3 Fr  = D * V * F;
    
    // energy
    Fr                     *= compute_multiscatter_energy(surface.F0, angular_info.n_dot_v, surface.roughness);
    surface.diffuse_energy *= compute_diffuse_energy(F, surface.metallic);
    
    return Fr;
}

/*------------------------------------------------------------------------------
    SPECULAR - ANISOTROPIC
------------------------------------------------------------------------------*/

float3 BRDF_Specular_Anisotropic(inout Surface surface, AngularInfo angular_info)
{
    // tangent frame
    float3 t, b;
    find_best_axis_vectors(surface.normal, t, b);
    float3x3 TBN = float3x3(t, b, surface.normal);
    float rotation   = max(surface.anisotropic_rotation * PI2, FLT_MIN);
    t = normalize(mul(float3(cos(rotation), sin(rotation), 0.0), TBN));
    b = normalize(cross(surface.normal, t));
    
    // roughness
    float a      = D_GGX_Alpha(surface.roughness);
    float aspect = sqrt(1.0 - surface.anisotropic * 0.9);
    float at     = a / aspect;
    float ab     = a * aspect;
    float at2    = at * at;
    float ab2    = ab * ab;
    
    // dot products
    float t_dot_v = dot(t, angular_info.v);
    float b_dot_v = dot(b, angular_info.v);
    float t_dot_l = dot(t, angular_info.l);
    float b_dot_l = dot(b, angular_info.l);
    float t_dot_h = dot(t, angular_info.h);
    float b_dot_h = dot(b, angular_info.h);
    
    // specular
    float  D     = D_GGX_Anisotropic(angular_info.n_dot_h, t_dot_h, b_dot_h, at, ab);
    float  ggxv  = angular_info.n_dot_l * sqrt(at2 * t_dot_v * t_dot_v + ab2 * b_dot_v * b_dot_v + angular_info.n_dot_v * angular_info.n_dot_v);
    float  ggxl  = angular_info.n_dot_v * sqrt(at2 * t_dot_l * t_dot_l + ab2 * b_dot_l * b_dot_l + angular_info.n_dot_l * angular_info.n_dot_l);
    float  V     = 0.5 / max(ggxv + ggxl, FLT_MIN);
    float3 F     = F_Schlick(surface.F0, compute_f90(surface.F0), angular_info.v_dot_h);
    float3 Fr    = D * V * F;
    
    // energy
    Fr                     *= compute_multiscatter_energy(surface.F0, angular_info.n_dot_v, surface.roughness);
    surface.diffuse_energy *= compute_diffuse_energy(F, surface.metallic);
    
    return Fr;
}

/*------------------------------------------------------------------------------
    SPECULAR - CLEARCOAT
------------------------------------------------------------------------------*/

float3 BRDF_Specular_Clearcoat(inout Surface surface, AngularInfo angular_info)
{
    // roughness - clearcoat uses simple squaring, not the cod wwii remap
    float a2 = surface.clearcoat_roughness * surface.clearcoat_roughness;

    // specular - fixed ior 1.5 gives f0 = 0.04
    float  D  = D_GGX(angular_info.n_dot_h, a2);
    float  V  = V_Kelemen(angular_info.v_dot_h);
    float  F  = F_Schlick(0.04, 1.0, angular_info.v_dot_h) * surface.clearcoat;
    float3 Fr = D * V * F;
    
    // energy - attenuate base layer
    surface.diffuse_energy *= 1.0 - F;
    
    return Fr;
}

/*------------------------------------------------------------------------------
    SPECULAR - SHEEN
------------------------------------------------------------------------------*/

float3 BRDF_Specular_Sheen(inout Surface surface, AngularInfo angular_info)
{
    // roughness - sheen needs minimum roughness for cloth appearance
    float sheen_roughness = max(surface.roughness, 0.3);
    
    // specular
    float  D           = D_Charlie(sheen_roughness, angular_info.n_dot_h);
    float  V           = V_Neubelt(angular_info.n_dot_v, angular_info.n_dot_l);
    float3 sheen_color = surface.albedo * surface.sheen;
    float  edge        = pow(1.0 - angular_info.n_dot_v, 5.0);
    float3 F           = lerp(sheen_color * 0.2, sheen_color, edge);
    float3 Fr          = D * V * F;
    
    // energy - sheen absorbs some diffuse
    surface.diffuse_energy *= 1.0 - surface.sheen * 0.5;
    
    return Fr;
}
