/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// Screen space subsurface scattering. The light pass leaves materials with a diffusion profile
// a plain diffuse lobe, demodulated from albedo, and marks them in alpha. Here every marked pixel
// gathers that light from the surface around it, weighted by how much of it would have travelled
// under the surface to reach this point. Albedo is applied afterwards by the composition, so
// texture detail stays sharp while the lighting softens and reddens at shadow edges.

//= INCLUDES ============================
#include "common.hlsl"
#include "subsurface_scattering.hlsl"
//=======================================

static const uint  sample_count = 32;
static const float golden_angle = 2.39996323f;

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 uv = (thread_id.xy + 0.5f) / resolution_out;

    float4 center = tex[thread_id.xy];
    if (center.a < 0.5f)
    {
        tex_uav[thread_id.xy] = center;
        return;
    }

    MaterialParameters material = material_parameters[get_material_index(uv)];
    float3 radius    = material.subsurface_radius;
    float radius_max = max(max(radius.x, radius.y), radius.z);
    float depth      = tex_depth.SampleLevel(samplers[sampler_point_clamp], uv, 0).r;
    float3 position  = get_position(depth, render_uv_to_screen_uv(uv));
    float3 normal    = get_geometric_normal(uv);

    // world size of a pixel at this depth, light that stays inside the pixel needs no gathering
    float view_z       = max(world_to_view(position).z, buffer_frame.camera_near);
    float pixel_size   = 2.0f * view_z / (abs(get_projection()[1][1]) * get_render_resolution_active().y);
    float pixel_radius = pixel_size * 0.5f;
    if (radius_max * 2.5f < pixel_radius)
    {
        tex_uav[thread_id.xy] = center;
        return;
    }

    // the disk lies on the surface so a grazing view samples the same area a frontal one does
    float3 up        = abs(normal.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 tangent   = normalize(cross(up, normal));
    float3 bitangent = cross(normal, tangent);

    // the share that never leaves the pixel, per channel, blue barely moves so it stays close to one
    float3 weight_center = subsurface_burley_cdf(pixel_radius, radius);
    float3 light_sum     = center.rgb * weight_center;
    float3 weight_sum    = weight_center;

    float rotation = noise_interleaved_gradient(float2(thread_id.xy)) * PI2;
    float jitter   = hash(float2(thread_id.xy) + float(buffer_frame.frame % 64u) * 1.6180339f);
    for (uint i = 0; i < sample_count; i++)
    {
        // radii follow the widest channel, the others are reweighted to their own profile below
        float r;
        float rcp_pdf;
        subsurface_burley_sample((float(i) + jitter) / float(sample_count), radius_max, r, rcp_pdf);
        if (r < pixel_radius)
        {
            continue;
        }

        float angle        = float(i) * golden_angle + rotation;
        float3 sample_ws   = position + (tangent * cos(angle) + bitangent * sin(angle)) * r;
        float2 sample_uv   = screen_uv_to_render_uv(world_to_uv(sample_ws));
        if (!is_valid_render_uv(sample_uv))
        {
            continue;
        }

        float4 sample_light = tex.SampleLevel(samplers[sampler_point_clamp], sample_uv, 0);
        if (sample_light.a < 0.5f)
        {
            continue;
        }

        // the surface under the sample is rarely on the disk, the true distance decides how much arrives
        float sample_depth    = tex_depth.SampleLevel(samplers[sampler_point_clamp], sample_uv, 0).r;
        float3 sample_surface = get_position(sample_depth, render_uv_to_screen_uv(sample_uv));
        float distance_true   = length(sample_surface - position);
        float3 weight         = subsurface_burley_profile(distance_true, radius) * rcp_pdf / float(sample_count);

        light_sum  += sample_light.rgb * weight;
        weight_sum += weight;
    }

    float3 scattered = light_sum / max(weight_sum, 1e-6f);
    float amount     = saturate(material.subsurface_scattering);
    tex_uav[thread_id.xy] = validate_output(float4(lerp(center.rgb, scattered, amount), center.a));
}
