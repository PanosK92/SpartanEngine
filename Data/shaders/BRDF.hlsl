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

/*------------------------------------------------------------------------------
    Fresnel, visibility and normal distribution functions
------------------------------------------------------------------------------*/

// [Schlick 1994, "An Inexpensive BRDF Model for Physically-Based Rendering"]
inline float3 F_Schlick(float3 f0, float v_dot_h)
{
    float Fc = pow(1 - v_dot_h, 5.0f);
    // Anything less than 2% is physically impossible and is instead considered to be shadowing
    return saturate(50.0 * f0.g) * Fc + (1 - Fc) * f0;
}

inline float3 F_Schlick(float3 f0, float f90, float v_dot_h)
{
    return f0 + (f90 - f0) * pow(1.0 - v_dot_h, 5.0f);
}

inline float3 fresnel(float3 f0, float l_dot_h)
{
    float f90 = saturate(dot(f0, 50.0 * 0.33));
    return F_Schlick(f0, f90, l_dot_h);
}

// Smith term for GGX
// [Smith 1967, "Geometrical shadowing of a random rough surface"]
inline float V_Smith(float a2, float n_dot_v, float n_dot_l)
{
    float Vis_SmithV = n_dot_v + fast_sqrt(n_dot_v * (n_dot_v - n_dot_v * a2) + a2);
    float Vis_SmithL = n_dot_l + fast_sqrt(n_dot_l * (n_dot_l - n_dot_l * a2) + a2);
    return rcp(Vis_SmithV * Vis_SmithL);
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
inline float V_SmithJointApprox(float a2, float n_dot_v, float n_dot_l)
{
    float a             = fast_sqrt(a2);
    float Vis_SmithV    = n_dot_l * (n_dot_v * (1 - a) + a);
    float Vis_SmithL    = n_dot_v * (n_dot_l * (1 - a) + a);
    return saturate_16(0.5 * rcp(Vis_SmithV + Vis_SmithL));
}

float V_GGX_anisotropic_2cos(float cos_theta_m, float alpha_x, float alpha_y, float cos_phi, float sin_phi)
    {
    float cos2  = cos_theta_m * cos_theta_m;
    float sin2  = (1.0 - cos2);
    float s_x   = alpha_x * cos_phi;
    float s_y   = alpha_y * sin_phi;
    return 1.0 / max(cos_theta_m + fast_sqrt(cos2 + (s_x * s_x + s_y * s_y) * sin2), 0.001);
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

// GGX / Trowbridge-Reitz
// [Walter et al. 2007, "Microfacet models for refraction through rough surfaces"]
inline float D_GGX(float a2, float n_dot_h)
{
    float d = (n_dot_h * a2 - n_dot_h) * n_dot_h + 1; // 2 mad
    return a2 / (PI * d * d); // 4 mul, 1 rcp
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

/*------------------------------------------------------------------------------
    Diffuse
------------------------------------------------------------------------------*/

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
    float a     = Roughness * Roughness;
    float s     = a;                    // ( 1.29 + 0.5 * a );
    float s2    = s * s;
    float VoL   = 2 * VoH * VoH - 1;    // double angle identity
    float Cosri = VoL - NoV * NoL;
    float C1    = 1 - 0.5 * s2 / (s2 + 0.33);
    float C2    = 0.45 * s2 / (s2 + 0.09) * Cosri * ( Cosri >= 0 ? rcp( max( NoL, NoV + 0.0001f ) ) : 1 );
    return diffuse_color / PI * ( C1 + C2 ) * ( 1 + Roughness * 0.5 );
}

inline float3 BRDF_Diffuse(Material material, float n_dot_v, float n_dot_l, float v_dot_h)
{
    return Diffuse_Burley(material.albedo.rgb, material.roughness, n_dot_v, n_dot_l, v_dot_h);
}

/*------------------------------------------------------------------------------
    Specular
------------------------------------------------------------------------------*/

inline float3 BRDF_Specular_Isotropic(Material material, float n_dot_v, float n_dot_l, float n_dot_h, float v_dot_h, inout float3 diffuse_energy, inout float3 reflectivity)
{
    // remapping and linearization
    float roughness = clamp(material.roughness, 0.089f, 1.0f);
    float a         = roughness * roughness;
    float a2        = pow(roughness, 4.0f);

    float V     = V_SmithJointApprox(a2, n_dot_v, n_dot_l);
    float D     = D_GGX(a2, n_dot_h);
    float3 F    = F_Schlick(material.F0, v_dot_h);

    diffuse_energy  *= compute_diffuse_energy(F, material.metallic);
    reflectivity    *= F;
    
    return (D * V) * F;
}

inline float3 BRDF_Specular_Anisotropic(Material material, Surface surface, float3 v, float3 l, float3 h, float n_dot_v, float n_dot_l, float n_dot_h, float l_dot_h, inout float3 diffuse_energy, inout float3 reflectivity)
{
    float3 n            = surface.normal;
    float3 t            = normalize(cross(n, -n));                              // any perpendicular vector to the normal
    float3 b            = cross(n, t);
    float3x3 TBN        = float3x3(t, b, n);
    float rotation      = max(material.anisotropic_rotation * PI2, FLT_MIN);    // convert material property to a full rotation
    float2 direction    = float2(cos(rotation), sin(rotation));                 // convert rotation to direction
    t                   = normalize(mul(float3(direction, 0.0f), TBN).xyz);     // compute direction derived tangent
    b                   = normalize(cross(n, t));                               // update bitangent

    float alpha_ggx = material.roughness;
    float aspect    = fast_sqrt(1.0 - material.anisotropic * 0.9);
    float ax        = alpha_ggx / aspect;
    float ay        = alpha_ggx * aspect;
    float XdotH     = dot(t, h);
    float YdotH     = dot(b, h);
    
    // specular anisotropic BRDF
    float D     = D_GGX_Anisotropic(n_dot_h, ax, ay, XdotH, YdotH);
    float V     = V_GGX_anisotropic_2cos(n_dot_v, ax, ay, XdotH, YdotH) * V_GGX_anisotropic_2cos(n_dot_v, ax, ay, XdotH, YdotH);
    float3 F    = fresnel(material.F0, l_dot_h);

    diffuse_energy  *= compute_diffuse_energy(F, material.metallic);
    reflectivity    *= F;
    
    return (D * V) * F;
}

inline float3 BRDF_Specular_Clearcoat(Material material, float n_dot_h, float v_dot_h, inout float3 diffuse_energy, inout float3 reflectivity)
{
    // remapping and linearization
    float roughness = clamp(material.clearcoat_roughness, 0.089f, 1.0f);
    float a         = roughness * roughness;
    float a2        = pow(roughness, 4.0f);
    
    float D     = D_GGX(a2, n_dot_h);
    float V     = V_Kelemen(v_dot_h);
    float3 F    = F_Schlick(0.04, 1.0, v_dot_h) * material.clearcoat;

    diffuse_energy  *= compute_diffuse_energy(F, material.metallic);
    reflectivity    *= F;

    return (D * V) * F;
}

inline float3 BRDF_Specular_Sheen(Material material, float n_dot_v, float n_dot_l, float n_dot_h, inout float3 diffuse_energy, inout float3 reflectivity)
{
    // remapping and linearization
    float roughness = clamp(material.roughness, 0.089f, 1.0f);

    // Mix between white and using base color for sheen reflection
    float tint  = material.sheen_tint * material.sheen_tint;
    float3 f0   = lerp(1.0f, material.F0, tint);
    
    float D     = D_Charlie(roughness, n_dot_h);
    float V     = V_Neubelt(n_dot_v, n_dot_l);
    float3 F    = f0 * material.sheen;

    diffuse_energy  *= compute_diffuse_energy(F, material.metallic);
    reflectivity    *= F;
    
    return (D * V) * F;
}
/*------------------------------------------------------------------------------
    Image based lighting
------------------------------------------------------------------------------*/

static const float mip_max = 11.0f;

inline float3 SampleEnvironment(Texture2D tex_environment, float2 uv, float mip_level)
{
    // We are currently using a spherical environment map which has a 2:1 ratio, so at the smallest 
    // mipmap we have to do a bit of blending otherwise we'll get a visible seem in the middle.
    if (mip_level == mip_max)
    {
        float2 mip_size = float2(2, 1);
        float dx        = mip_size.x;
    
        float3 tl = (tex_environment.SampleLevel(sampler_bilinear_clamp, uv + float2(-dx, 0.0f), mip_level).rgb);
        float3 tr = (tex_environment.SampleLevel(sampler_bilinear_clamp, uv + float2(dx, 0.0f), mip_level).rgb);
        return (tl + tr) / 2.0f;
    }
    
    return (tex_environment.SampleLevel(sampler_trilinear_clamp, uv, mip_level).rgb);
}

inline float3 GetSpecularDominantDir(float3 normal, float3 reflection, float roughness)
{
    const float smoothness = 1.0f - roughness;
    const float lerpFactor = smoothness * (fast_sqrt(smoothness) + roughness);
    
    return lerp(normal, reflection, lerpFactor);
}

// https://www.unrealengine.com/blog/physically-based-shading-on-mobile
inline float3 EnvBRDFApprox(float3 specColor, float roughness, float NdV)
{
    // [ Lazarov 2013, "Getting More Physical in Call of Duty: Black Ops II" ]
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f );
    const float4 c1 = float4(1.0f, 0.0425f, 1.0f, -0.04f );
    float4 r        = roughness * c0 + c1;
    float a004      = min(r.x * r.x, exp2(-9.28f * NdV)) * r.x + r.y;
    float2 AB       = float2(-1.04f, 1.04f) * a004 + r.zw;
   
    return specColor * AB.x + AB.y;
}

inline float3 Brdf_Diffuse_Ibl(Material material, float3 normal, Texture2D tex_environment)
{
    return SampleEnvironment(tex_environment, direction_sphere_uv(normal), mip_max) * material.albedo.rgb;
}

inline float3 Brdf_Specular_Ibl(Material material, float3 normal, float3 camera_to_pixel, Texture2D tex_environment, Texture2D tex_lutIBL, inout float3 diffuse_energy, inout float3 reflectivity)
{
    // remapping and linearization
    float roughness = clamp(material.roughness, 0.089f, 1.0f);
    float a         = roughness * roughness;
    
    float3 reflection       = reflect(camera_to_pixel, normal); 
    reflection              = GetSpecularDominantDir(normal, reflect(camera_to_pixel, normal), material.roughness); // From Sebastien Lagarde Moving Frostbite to PBR page 69
    float n_dot_v           = dot(-camera_to_pixel, normal);
    float f90               = 0.5 + 2 * n_dot_v * n_dot_v * material.roughness;
    float3 F                = F_Schlick(material.F0, f90, material.roughness);
    float mip_level         = lerp(0, mip_max, a);
    float3 prefilteredColor = SampleEnvironment(tex_environment, direction_sphere_uv(reflection), mip_level);
    float2 envBRDF          = tex_lutIBL.Sample(sampler_bilinear_clamp, float2(saturate(n_dot_v), material.roughness)).xy;
    reflectivity            *= F * envBRDF.x + envBRDF.y;

    diffuse_energy *= compute_diffuse_energy(F, material.metallic);
    
    return prefilteredColor * reflectivity;
}
