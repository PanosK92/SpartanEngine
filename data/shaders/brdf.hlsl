/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#ifndef SPARTAN_BRDF
#define SPARTAN_BRDF

//= INCLUDES =========
#include "common.hlsl"
//====================

/*------------------------------------------------------------------------------
    FRESNEL
------------------------------------------------------------------------------*/

// schlick 1994 - attempt to provide energy conserving f90
float3 compute_f90(float3 f0) { return saturate(50.0 * dot(f0, 0.33333)); }
float3 get_f90()              { return 1.0; } // for legacy/external use

// (1 - x)^5 expressed as four multiplies, much cheaper than pow which lowers to exp2/log2
float pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

float3 F_Schlick(float3 f0, float3 f90, float v_dot_h)
{
    return f0 + (f90 - f0) * pow5(1.0 - v_dot_h);
}

float F_Schlick(float f0, float f90, float v_dot_h)
{
    return f0 + (f90 - f0) * pow5(1.0 - v_dot_h);
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
    float dhr         = lerp(1.0 - roughness * 0.7, 1.0, pow5(1.0 - n_dot_v));
    float3 energy_loss = (1.0 - dhr) * (1.0 - f0);
    return 1.0 + f0 * energy_loss / (1.0 - energy_loss + FLT_MIN);
}

// karis 2014 analytic split sum, same as reflections_shade, f90 is baked in as 1 so the bias
// term carries grazing fresnel, the gpu lut was built with compute_f90(0)=0 which wiped that term
float2 reflection_env_brdf(float roughness, float n_dot_v)
{
    const float4 c0 = float4(-1.0f, -0.0275f, -0.572f, 0.022f);
    const float4 c1 = float4(1.0f, 0.0425f, 1.04f, -0.04f);
    float4 r        = roughness * c0 + c1;
    float  a004     = min(r.x * r.x, exp2(-9.28f * n_dot_v)) * r.x + r.y;
    return float2(-1.04f, 1.04f) * a004 + r.zw;
}

// fdez-aguera 2019 from the split sum terms, a + b is the single scatter albedo of a white lobe,
// the energy it loses to microfacet shadowing comes back tinted by f0, rough metals stop going dark
float3 compute_multiscatter_energy_split_sum(float3 f0, float2 env_brdf)
{
    float single_scatter = max(env_brdf.x + env_brdf.y, 0.05f);
    return 1.0 + f0 * (1.0 / single_scatter - 1.0);
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
    SPECULAR - DUAL-LOBE SKIN (Unreal Engine 5 DualSpecularGGX)
------------------------------------------------------------------------------*/

// Human skin exhibits two specular micro-lobes sharing one lipid Fresnel (F0 ~= 0.028):
// - Lobe 0 (92%): velvety epidermal roughness following the pore micro-geometry
// - Lobe 1 (8%):  soft dry sebum micro-lobe (~0.72x roughness, >=0.30) — natural living skin, NEVER sweaty/oily
float3 BRDF_Specular_DualLobe_Skin(inout Surface surface, AngularInfo angular_info)
{
    const float lobe0_rough = clamp(surface.roughness, 0.42f, 0.95f);
    const float lobe1_rough = max(lobe0_rough * 0.72f, 0.30f);
    const float lobe_mix    = 0.08f;

    const float a0  = D_GGX_Alpha(lobe0_rough);
    const float a1  = D_GGX_Alpha(lobe1_rough);
    const float a02 = a0 * a0;
    const float a12 = a1 * a1;
    const float avg = D_GGX_Alpha(lerp(lobe0_rough, lobe1_rough, lobe_mix));
    const float av2 = avg * avg;

    const float  D  = lerp(D_GGX(angular_info.n_dot_h, a02), D_GGX(angular_info.n_dot_h, a12), lobe_mix);
    const float  V  = V_SmithGGX(angular_info.n_dot_v, angular_info.n_dot_l, av2);
    const float3 F  = F_Schlick(surface.F0, compute_f90(surface.F0), angular_info.v_dot_h);
    float3       Fr = D * V * F;

    Fr                     *= compute_multiscatter_energy(surface.F0, angular_info.n_dot_v, lobe0_rough);
    surface.diffuse_energy *= compute_diffuse_energy(F, surface.metallic);

    return Fr;
}

/*------------------------------------------------------------------------------
    EYE - CONCAVE REFRACTIVE IRIS CAUSTIC (Unreal Engine 5 EyeBxDF)
------------------------------------------------------------------------------*/

// Models light refracted through the convex cornea onto the concave inner iris cup:
// creates a luminous opposite-side caustic crescent on the colored iris while leaving the sclera & pupil untouched.
float BRDF_Eye_Iris_Caustic(Surface surface, AngularInfo angular_info, float3 real_albedo)
{
    const float luma      = dot(real_albedo, float3(0.2126f, 0.7152f, 0.0722f));
    const float iris_mask = smoothstep(0.020f, 0.075f, luma) * (1.0f - smoothstep(0.30f, 0.56f, luma));
    if (iris_mask <= 0.001f)
    {
        return 1.0f;
    }

    // Concave iris cup bends the effective refraction normal opposite the outer corneal curvature
    const float3 caustic_normal = normalize(lerp(surface.normal, -angular_info.v, 0.48f));
    const float  iris_nol       = saturate(angular_info.n_dot_l);
    const float  power          = lerp(12.0f, 1.0f, iris_nol);
    const float  caustic        = 0.80f + 0.24f * (power + 1.0f) * pow(saturate(dot(caustic_normal, angular_info.l)), power);
    return lerp(1.0f, caustic, iris_mask);
}

/*------------------------------------------------------------------------------
    SPECULAR - ANISOTROPIC
------------------------------------------------------------------------------*/

float3 BRDF_Specular_Anisotropic(inout Surface surface, AngularInfo angular_info)
{
    // tangent frame, find_best_axis_vectors yields orthonormal axes and the cross of unit
    // vectors stays unit, so the trailing normalizes are redundant
    float3 t, b;
    find_best_axis_vectors(surface.normal, t, b);
    float3x3 TBN   = float3x3(t, b, surface.normal);
    float rotation = max(surface.anisotropic_rotation * PI2, FLT_MIN);
    float cos_r, sin_r;
    sincos(rotation, sin_r, cos_r);
    t = mul(float3(cos_r, sin_r, 0.0), TBN);
    b = cross(surface.normal, t);
    
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
    Fr *= lerp(float3(1.0f, 1.0f, 1.0f), surface.coat_tint, saturate(surface.coat_tint_strength));
    
    // energy - attenuate base layer
    surface.diffuse_energy *= 1.0 - F;
    
    return Fr;
}

/*------------------------------------------------------------------------------
    SPECULAR - METALLIC FLAKES
------------------------------------------------------------------------------*/

// share of the light that crosses the clear coat both ways
float coat_transmission(float clearcoat, float n_dot_v, float n_dot_l)
{
    return (1.0f - F_Schlick(0.04f, 1.0f, n_dot_v) * clearcoat) * (1.0f - F_Schlick(0.04f, 1.0f, n_dot_l) * clearcoat);
}

// aluminium platelets suspended in the base coat, each one a small tilted mirror, light and view
// refract into the coat first which pulls them towards the normal, so the glints spread wider
// around the coat highlight than the platelet tilt alone would, the way real metallic paint does
float3 BRDF_Specular_Flakes(inout Surface surface, AngularInfo angular_info, Light light)
{
    float3 n = surface.normal;
    if (dot(angular_info.v, n) <= 1e-4f || angular_info.n_dot_l <= 1e-4f)
        return 0.0f;

    const float eta = 1.0f / paint_coat_ior;
    float3 l_coat   = -refract(-angular_info.l, n, eta);
    float3 v_coat   = -refract(-angular_info.v, n, eta);
    float3 h_coat   = normalize(l_coat + v_coat);

    float3 flake_normal;
    float  flake_alpha;
    paint_flake_lobe(surface.flake_code, n, flake_normal, flake_alpha);

    // an area light mirrored in a platelet is as wide as the light
    if (light.is_area())
    {
        flake_alpha = light.compute_area_roughness_modification(flake_alpha, light.distance_to_pixel);
    }

    float  D  = D_GGX(saturate(dot(flake_normal, h_coat)), flake_alpha * flake_alpha);
    float  V  = V_Kelemen(saturate(dot(l_coat, h_coat)));
    float3 F  = F_Schlick(surface.flake_color, 1.0f, saturate(dot(v_coat, h_coat)));
    float3 Fr = D * V * F * surface.flake_weight * coat_transmission(surface.clearcoat, angular_info.n_dot_v, angular_info.n_dot_l);

    // the pigment is only seen between the platelets
    surface.diffuse_energy *= 1.0f - surface.flake_weight * 0.6f;

    return Fr;
}

/*------------------------------------------------------------------------------
    SPECULAR - SHEEN
------------------------------------------------------------------------------*/

float3 BRDF_Specular_Sheen(inout Surface surface, AngularInfo angular_info)
{
    // an explicit sheen color is the gltf sheen layer with its own roughness and no fresnel,
    // otherwise the sheen is derived from the base color and needs a minimum roughness to read as cloth
    bool   explicit_color  = any(surface.sheen_color > 0.0);
    float  sheen_roughness = explicit_color ? max(surface.sheen_roughness, 0.07) : max(surface.roughness, 0.3);
    float3 sheen_color     = explicit_color ? surface.sheen_color : surface.albedo * surface.sheen;
    
    // specular
    float  D    = D_Charlie(sheen_roughness, angular_info.n_dot_h);
    float  V    = V_Neubelt(angular_info.n_dot_v, angular_info.n_dot_l);
    float  edge = pow5(1.0 - angular_info.n_dot_v);
    float3 F    = explicit_color ? sheen_color : lerp(sheen_color * 0.2, sheen_color, edge);
    float3 Fr   = D * V * F;
    
    // energy - sheen absorbs some diffuse
    float sheen_weight      = explicit_color ? max(max(sheen_color.r, sheen_color.g), sheen_color.b) : surface.sheen;
    surface.diffuse_energy *= 1.0 - sheen_weight * 0.5;
    
    return Fr;
}

/*------------------------------------------------------------------------------
    HAIR - MARSCHNER / KAJIYA-KAY DUAL-LOBE SHADING
------------------------------------------------------------------------------*/

// Physically-based hair shading (Marschner 2003 / Karis 2016):
// Models cylindrical keratin fibers (IOR 1.55) with tilted cuticles:
// - R:   primary cuticle highlight shifted toward the root (dielectric keratin F0 = 0.046)
// - TRT: true melanin-tinted secondary internal reflection shifted toward the tip
// - TT:  warm forward-scattered backlit rim transmission tinted by true melanin albedo
// - MS:  demodulated Kajiya-Kay cylindrical multiple-scattering diffuse (multiplied by albedo in composition)
void BRDF_Hair(Surface surface, AngularInfo angular_info, float3 hair_albedo, out float3 out_diffuse, out float3 out_specular)
{
    float3 n = surface.normal;
    float3 v = angular_info.v;
    float3 l = angular_info.l;

    // Smooth continuous scalp-to-tip tangent flow with no crown discontinuity
    float3 ref_a    = normalize(float3(0.0f, -1.0f, -0.15f));
    float3 ref_b    = float3(0.0f, 0.0f, 1.0f);
    float  pole     = smoothstep(0.85f, 0.98f, abs(dot(n, ref_a)));
    float3 flow_ref = normalize(lerp(ref_a, ref_b, pole));
    float3 t        = normalize(flow_ref - n * dot(flow_ref, n));

    float sin_theta_l = clamp(dot(t, l), -0.999f, 0.999f);
    float sin_theta_v = clamp(dot(t, v), -0.999f, 0.999f);
    float cos_theta_l = sqrt(saturate(1.0f - sin_theta_l * sin_theta_l));
    float cos_theta_d = cos(0.5f * abs(asin(sin_theta_v) - asin(sin_theta_l)));

    // Azimuthal cosine between view and light projected onto the fiber cross-section
    float3 l_perp   = l - t * sin_theta_l;
    float3 v_perp   = v - t * sin_theta_v;
    float  perp_len = sqrt(max(dot(l_perp, l_perp) * dot(v_perp, v_perp), 1e-6f));
    float  cos_phi  = clamp(dot(l_perp, v_perp) / perp_len, -1.0f, 1.0f);

    float rough   = clamp(surface.roughness, 0.28f, 0.88f);
    float beta_r  = max(rough * rough * 0.85f, 0.075f);
    float beta_tr = min(beta_r * 2.0f, 0.70f);

    // Cuticle tilt shifts R toward root (-5.5 deg) and TRT toward tip (+9 deg)
    float theta_h = 0.5f * (asin(sin_theta_l) + asin(sin_theta_v));
    float m_r     = exp(-0.5f * ((theta_h + 0.096f) * (theta_h + 0.096f)) / (beta_r * beta_r)) / (sqrt(PI2) * beta_r);
    float m_trt   = exp(-0.5f * ((theta_h - 0.157f) * (theta_h - 0.157f)) / (beta_tr * beta_tr)) / (sqrt(PI2) * beta_tr);

    // Physical keratin Fresnel (IOR = 1.55 -> F0 = 0.046) normalized by INV_PI so dark strands keep silky sheen without frosting white
    float f_r = F_Schlick(0.046f, 0.55f, saturate(cos_theta_d * sqrt(saturate(0.5f + 0.5f * cos_phi))));
    float n_r = 0.25f * sqrt(saturate(0.5f + 0.5f * cos_phi));

    // Scale cuticle specular gently with fiber luminance so coarse dark brows/eyelashes stay rich and velvety
    float  albedo_luma  = max(dot(hair_albedo, float3(0.2126f, 0.7152f, 0.0722f)), 0.002f);
    float  cuticle_fade = lerp(0.45f, 1.0f, saturate(sqrt(albedo_luma) * 3.0f));
    float3 melanin_trt  = hair_albedo * pow(max(hair_albedo, float3(0.002f, 0.0015f, 0.001f)), 0.35f) * float3(1.35f, 1.05f, 0.85f);
    float  front_wrap   = saturate(dot(n, l) * 0.78f + 0.22f);
    float  front_spec   = saturate(dot(n, l) * 0.90f + 0.10f);

    // Primary silky cuticle highlight (R) + warm melanin-absorbed secondary highlight (TRT)
    float3 lobe_r   = (m_r * n_r * f_r * INV_PI * 0.42f * cuticle_fade) * front_spec;
    float3 lobe_trt = (m_trt * (1.0f - f_r) * INV_PI * 0.55f) * melanin_trt * front_wrap;

    // Subtle warm backlit rim transmission (TT) along backlit hair edges
    float  backlit = pow(saturate(dot(-v, l)), 3.5f) * pow(saturate(0.5f - 0.5f * cos_phi), 2.0f);
    float3 lobe_tt = hair_albedo * float3(1.45f, 0.95f, 0.62f) * (backlit * INV_PI * 0.40f);

    // Demodulated cylindrical Kajiya-Kay multiple-scattering diffuse (light_composition multiplies by albedo once)
    float kajiya_sin = lerp(0.24f, 1.0f, cos_theta_l);
    float wrap_ndotl = saturate((dot(n, l) + 0.32f) / 1.32f);
    float ms_scatter = lerp(wrap_ndotl, kajiya_sin, 0.45f);

    out_diffuse  = float3(ms_scatter, ms_scatter, ms_scatter) * INV_PI;
    out_specular = lobe_r + lobe_trt + lobe_tt;
}

/*------------------------------------------------------------------------------
    SPECULAR OCCLUSION
------------------------------------------------------------------------------*/

float3 get_dominant_specular_direction(float3 normal, float3 view_dir, float roughness)
{
    // frostbite/epic dominant direction - biases toward normal as roughness increases
    // this better represents where most specular energy comes from for rough surfaces
    float3 reflection = reflect(-view_dir, normal);
    float smoothness  = 1.0f - roughness;
    float factor      = smoothness * (sqrt(smoothness) + roughness);
    return normalize(lerp(normal, reflection, factor));
}

// solid angle shared by two spherical caps over 2pi, oat and sander 2007 with the jimenez 2016 smoothstep fit
float spherical_caps_intersection(float cos_cap1, float cos_cap2, float cos_distance)
{
    float r1 = fast_acos(cos_cap1);
    float r2 = fast_acos(cos_cap2);
    float d  = fast_acos(cos_distance);

    if (min(r1, r2) <= max(r1, r2) - d)
        return 1.0f - max(cos_cap1, cos_cap2);

    if (r1 + r2 <= d)
        return 0.0f;

    float delta = abs(r1 - r2);
    float x     = 1.0f - saturate((d - delta) / max(r1 + r2 - delta, 1e-4f));
    return x * x * (3.0f - 2.0f * x) * (1.0f - max(cos_cap1, cos_cap2));
}

// jimenez 2016 gtso, the fraction of the reflection lobe cone that falls inside the bent normal visibility cone
float specular_occlusion_cones(float3 bent_normal, float visibility, float3 reflection, float roughness_alpha)
{
    float cos_visibility = sqrt(saturate(1.0f - visibility));
    float cos_specular   = exp2(-3.321928f * roughness_alpha * roughness_alpha);
    float cos_distance   = dot(bent_normal, reflection);
    return saturate(spherical_caps_intersection(cos_visibility, cos_specular, cos_distance) / max(1.0f - cos_specular, 1e-4f));
}

// a near mirror lobe is thinner than the visibility cone is accurate, the cone test turns binary
// and aliases there, so it hands over to lagarde 2014 which only needs the ao
float specular_occlusion_bent(float3 bent_normal, float visibility, float3 reflection, float n_dot_v, float roughness, float roughness_alpha)
{
    float specular_occlusion_cone = specular_occlusion_cones(bent_normal, visibility, reflection, roughness_alpha);
    float specular_occlusion_ao   = saturate(pow(n_dot_v + visibility, exp2(-16.0f * roughness - 1.0f)) - 1.0f + visibility);
    return lerp(specular_occlusion_ao, specular_occlusion_cone, smoothstep(0.01f, 0.09f, roughness_alpha));
}

#endif
