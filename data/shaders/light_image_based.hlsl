/*
Copyright(c) 2016-2023 Panos Karabelas

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

// = INCLUDES ======
#include "brdf.hlsl"
#include "fog.hlsl"
//==================

// From Sebastien Lagarde Moving Frostbite to PBR page 69
float3 get_dominant_specular_direction(float3 normal, float3 reflection, float roughness)
{
    const float smoothness = 1.0f - roughness;
    const float alpha      = smoothness * (sqrt(smoothness) + roughness);
    
    return lerp(normal, reflection, alpha);
}

float3 sample_environment(float2 uv, float mip_level)
{
    // We are currently using a spherical environment map which has a smallest mip size of 2x1.
    // So we have to do a bit of blending otherwise we'll get a visible seem in the middle.
    if (mip_level >= ENVIRONMENT_MAX_MIP)
    {
        uv = float2(0.5f, 0.0);
    }

    return tex_environment.SampleLevel(samplers[sampler_trilinear_clamp], uv, mip_level).rgb;
}

float3 get_parallax_corrected_reflection(Surface surface, float3 position_probe, float3 box_min, float3 box_max)
{
    float3 camera_to_pixel = surface.position - buffer_frame.camera_position;
    float3 reflection      = reflect(camera_to_pixel, surface.normal);
    
    // Find the ray intersection with box plane
    float3 FirstPlaneIntersect  = (box_max - surface.position) / reflection;
    float3 SecondPlaneIntersect = (box_min - surface.position) / reflection;

    // Get the furthest of these intersections along the ray
    // (Ok because x/0 give +inf and -x/0 give –inf )
    float3 furthest_plane = max(FirstPlaneIntersect, SecondPlaneIntersect);

    // Find the closest far intersection
    float distance = min3(furthest_plane);
    
    // Get the intersection position
    float3 position_intersection = surface.position + reflection * distance;

    // Get corrected reflection
    reflection = position_intersection - position_probe;
    
    return reflection;
}

bool is_inside_box(in float3 p, in float3 min, in float3 max)
{
    return (p.x < min.x || p.x > max.x || p.y < min.y || p.y > max.y || p.z < min.z || p.z > max.z) ? false : true;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    const uint2 pos = input.uv * pass_get_resolution_out();
    
    // construct surface
    Surface surface;
    bool use_ssgi = pass_is_opaque(); // we don't do ssgi for transparents.
    surface.Build(pos, true, use_ssgi, false);

    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // If this is an opaque pass, ignore all transparent pixels.
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // If this is an transparent pass, ignore all opaque pixels.
    bool early_exit_3 = surface.is_sky();                             // We don't want to do IBL on the sky itself.
    if (early_exit_1 || early_exit_2 || early_exit_3)
        discard;

    // try to compute some sort of ambient light that makes sense
    float3 light_ambient = 1.0f;
    {
        light_ambient = buffer_light.intensity_range_angle_bias.x * 0.1f * surface.occlusion;
    }
    
    // compute specular energy
    const float n_dot_v          = saturate(dot(-surface.camera_to_pixel, surface.normal));
    const float3 F               = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
    const float2 envBRDF         = tex_lut_ibl.SampleLevel(samplers[sampler_bilinear_clamp], float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;

    // IBL - Diffuse
    float3 diffuse_energy = compute_diffuse_energy(specular_energy, surface.metallic); // Used to town down diffuse such as that only non metals have it
    float3 ibl_diffuse    = sample_environment(direction_sphere_uv(surface.normal), ENVIRONMENT_MAX_MIP) * surface.albedo.rgb * light_ambient * diffuse_energy;

    // IBL - Specular
    const float3 reflection            = reflect(surface.camera_to_pixel, surface.normal);
    float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, reflection, surface.roughness);
    float mip_level                    = lerp(0, ENVIRONMENT_MAX_MIP, surface.roughness);
    float3 ibl_specular_environment    = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level) * light_ambient;
    
    // get ssr color
    float ssr_mip_count     = pass_get_f4_value().y;
    mip_level               = lerp(0, ssr_mip_count, surface.roughness);
    const float4 ssr_sample = is_ssr_enabled() ? tex_ssr.SampleLevel(samplers[sampler_trilinear_clamp], surface.uv, mip_level) : 0.0f;
    const float3 color_ssr  = ssr_sample.rgb;
    float ssr_alpha         = ssr_sample.a;

    // remap alpha above a certain roughness threshold in order to hide blocky reflections (from very small mips)
    static const float ssr_roughness_threshold = 0.8f;
    if (surface.roughness > ssr_roughness_threshold)
    {
        ssr_alpha = lerp(ssr_alpha, 0.0f, (surface.roughness - ssr_roughness_threshold) / (1.0f - ssr_roughness_threshold));
    }

    // sample reflection probe
    float3 ibl_specular_probe = 0.0f;
    float probe_alpha         = 0.0f;
    if (pass_is_reflection_probe_available())
    {
        float3 probe_position = pass_get_f3_value();
        float3 extents        = pass_get_f3_value2();
        float3 box_min        = probe_position - extents;
        float3 box_max        = probe_position + extents;

        if (is_inside_box(surface.position, box_min, box_max))
        {
            float3 reflection   = get_parallax_corrected_reflection(surface, probe_position, box_min, box_max);
            float4 probe_sample = tex_reflection_probe.SampleLevel(samplers[sampler_bilinear_clamp], reflection, 0.0f);
            ibl_specular_probe  = probe_sample.rgb;
            probe_alpha         = probe_sample.a;
        }
    }

    // assume specular from SSR
    float3 ibl_specular = color_ssr;

    // if there are no SSR data, fallback to to the reflection probe.
    ibl_specular = lerp(ibl_specular_probe, ibl_specular, ssr_alpha);

    // if there are no reflection probe data, fallback to the environment texture
    ibl_specular = lerp(ibl_specular_environment, ibl_specular, max(ssr_alpha, probe_alpha));

    // modulate outcoming energy
    ibl_specular *= specular_energy;

    float3 ibl = ibl_diffuse + ibl_specular;
    
    // SSGI
    if (is_ssgi_enabled() && use_ssgi)
    {
        ibl *= surface.occlusion;
    }

    // fade out for transparents
    ibl *= surface.alpha;
    
    // fog
    float3 fog = get_fog_factor(surface.position, buffer_frame.camera_position.xyz);
    ibl += fog;
    
    // Perfection achieved
    return float4(ibl, 0.0f);
}
