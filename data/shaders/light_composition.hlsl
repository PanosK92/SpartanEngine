/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// = INCLUDES ========
#include "common.hlsl"
#include "fog_volume.hlsl"
#include "spherical_harmonics.hlsl"
#include "ocean_whitewater.hlsl"
//====================

// catmull rom filtered sky panorama fetch, sky pixels magnify the 4k panorama ~2.5x at a
// typical fov so plain bilinear smears cloud edges, the 5 tap cubic keeps them crisp, the
// neighborhood clamp suppresses ringing from the negative lobes around hdr spikes like the sun
float3 sample_sky_catmull_rom(float2 uv)
{
    float2 resolution;
    tex2.GetDimensions(resolution.x, resolution.y);

    float2 sample_position = uv * resolution;
    float2 tex_pos_1       = floor(sample_position - 0.5f) + 0.5f;
    float2 f               = sample_position - tex_pos_1;

    float2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
    float2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
    float2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
    float2 w3 = f * f * (-0.5f + 0.5f * f);

    float2 w12       = w1 + w2;
    float2 offset_12 = w2 / w12;

    float2 inv_res    = 1.0f / resolution;
    float2 tex_pos_0  = (tex_pos_1 - 1.0f) * inv_res;
    float2 tex_pos_3  = (tex_pos_1 + 2.0f) * inv_res;
    float2 tex_pos_12 = (tex_pos_1 + offset_12) * inv_res;

    float3 s0 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_0.y),  0.0f).rgb;
    float3 s1 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_0.x,  tex_pos_12.y), 0.0f).rgb;
    float3 s2 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_12.y), 0.0f).rgb;
    float3 s3 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_3.x,  tex_pos_12.y), 0.0f).rgb;
    float3 s4 = tex2.SampleLevel(samplers[sampler_bilinear_clamp], float2(tex_pos_12.x, tex_pos_3.y),  0.0f).rgb;

    float3 result = 0.0f.xxx;
    result += s0 * w12.x * w0.y;
    result += s1 * w0.x  * w12.y;
    result += s2 * w12.x * w12.y;
    result += s3 * w3.x  * w12.y;
    result += s4 * w12.x * w3.y;

    float  weight_sum = w12.x + w12.y - w12.x * w12.y;
    float3 sky_min    = min(min(min(s0, s1), min(s2, s3)), s4);
    float3 sky_max    = max(max(max(s0, s1), max(s2, s3)), s4);
    return clamp(result * rcp(weight_sum), sky_min, sky_max);
}

// The path estimator returns surface radiance. Integrate light scattered along
// the submerged segment and let the resolved foam cover both surface lobes.
float3 compose_traced_ocean(Surface surface, float3 radiance)
{
    float2 uv = render_uv_to_screen_uv(surface.uv);
    float bed_depth = tex4.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    float end_distance = bed_depth > 0.0f ? length(get_position(bed_depth, uv) - get_camera_position()) : fog_far;
    float incidence = saturate(abs(dot(surface.normal, surface.camera_to_pixel)));
    float transmission = (1.0f - surface.F0.x) * (1.0f - pow(1.0f - incidence, 5.0f));
    if (get_camera_position().y >= buffer_frame.ocean_sea_level)
        radiance += transmission * fog_transmit_water_segment(0.0f, uv, surface.camera_to_pixel_length, end_distance);

    float foam, churn;
    ocean_whitewater(surface, foam, churn);
    if (foam > 0.0f || churn > 0.0f)
    {
        float3 sky_sh[9];
        sh_load_l2(tex5, sky_sh);
        float3 irradiance = sh_irradiance_l2(surface.normal, sky_sh, 1.0f)
            + get_sun_radiance() * saturate(dot(surface.normal, -light_parameters[0].direction));
        float3 whitewater = max(irradiance, 0.0f) * INV_PI;
        float bubbles = saturate(0.32f * churn * sqrt(churn)) * transmission;
        radiance = lerp(radiance, whitewater * 0.80f * exp(-get_ocean_extinction() * 0.35f), bubbles);
        foam *= saturate(ocean_shore_coverage(surface, uv) * 2.5f);
        radiance = lerp(radiance, whitewater * lerp(0.55f, 0.85f, smoothstep(0.15f, 0.9f, foam)), saturate(foam));
    }
    return radiance;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // skip non transparent pixels during the transparent pass, the opaque pass already wrote them
    if (pass_is_transparent() && !surface.is_transparent())
        return;

    float3 light_diffuse       = 0.0f;
    float3 light_specular      = 0.0f;
    float3 light_emissive      = 0.0f;
    float alpha                = 0.0f;

    // fill in the sky pixels
    if (surface.is_sky() && pass_is_opaque())
    {
        // clamp y to the upper hemisphere so the dimmed below horizon sky does not show a dark band
        float3 view_dir_sky  = surface.camera_to_pixel;
        view_dir_sky.y       = max(view_dir_sky.y, 0.0f);
        view_dir_sky         = normalize(view_dir_sky);
        light_emissive       = sample_sky_catmull_rom(direction_sphere_uv(view_dir_sky));
        alpha                = 0.0f;
    }
    else if (is_restir_pt_enabled())
    {
        float4 lighting = tex3.Load(int3(thread_id.xy, 0));
        light_specular = lighting.a >= 0.0f ? lighting.rgb : 0.0f;
        if (surface.has_subsurface_profile() && surface.is_opaque())
            light_diffuse = tex6.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        if (surface.is_water() && buffer_frame.ocean_enabled > 0.5f)
            light_specular = compose_traced_ocean(surface, light_specular);
        light_emissive = surface.emissive;
        alpha = surface.alpha;
    }
    // fill opaque and transparent pixels based on pass type
    else if ((pass_is_opaque() && surface.is_opaque()) || (pass_is_transparent() && surface.is_transparent()))
    {
        light_diffuse        = tex3.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        light_specular       = tex4.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        // The G-buffer already stores emitted scene radiance. Emission is not
        // reflectance: multiplying it by albedo changes both its color and energy.
        light_emissive       = surface.emissive;
        alpha                = surface.alpha;

        // water and glass are shaded from reflection and transmission inside the refraction
        // composite, a lambert albedo layer on top double counts and washes the fresnel structure
        // to a flat albedo sheet, the analytic specular in light_specular is real and stays
        if (surface.is_water() || surface.is_glass())
        {
            light_diffuse = 0.0f;
        }

    }

    // Camera transport is composed after all surface lighting.

    // each pixel reaches this point once per frame so a straight write is safe
    tex_uav[thread_id.xy] = validate_output(float4(light_diffuse * surface.albedo + light_specular + light_emissive, alpha));
}
