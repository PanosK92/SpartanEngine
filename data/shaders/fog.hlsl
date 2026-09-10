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

#ifndef SPARTAN_FOG_LIGHTING
#define SPARTAN_FOG_LIGHTING
#include "fog_volume.hlsl"
#include "rt_visibility.hlsl"

// returns 1 if the world space position is lit by the light, 0 if occluded
// directional picks a single cascade per sample instead of paying for both
// raster atlas path only, ray traced shadows trace the tlas instead
float visible(float3 position, Light light, uint2 pixel_pos)
{
    if (light.is_point())
    {
        // pick the cube face whose axis dominates the light to point vector
        float3 light_to_pixel = position - light.position;
        float3 abs_dir        = abs(light_to_pixel);
        uint face_index = (abs_dir.x >= abs_dir.y && abs_dir.x >= abs_dir.z) ? (light_to_pixel.x > 0.0f ? 0u : 1u) :
                          (abs_dir.y >= abs_dir.z)                           ? (light_to_pixel.y > 0.0f ? 2u : 3u) :
                                                                               (light_to_pixel.z > 0.0f ? 4u : 5u);

        float4 clip_pos = mul(
            float4(position, 1.0f),
            light_get_transform(light, face_index)
        );
        if (clip_pos.w <= 0.0f)
        {
            return 1.0f;
        }

        float3 ndc          = clip_pos.xyz / clip_pos.w;
        float2 projected_uv = ndc_to_uv(ndc.xy);
        return light_compare_depth(
            light,
            float3(projected_uv, (float)face_index),
            ndc.z
        );
    }

    if (light.is_directional())
    {
        const uint near_cascade = 0;
        const uint far_cascade  = 1;

        float3 projected_pos_near = world_to_ndc(
            position,
            light_get_transform(light, near_cascade)
        );
        float2 projected_uv_near  = ndc_to_uv(projected_pos_near);
        // The blend is exactly zero here; the far cascade cannot contribute.
        // Most detailed fog cells are in this interior, so avoid a second atlas lookup.
        if (cascade_contains(projected_pos_near) &&
            max(abs(projected_pos_near.x), abs(projected_pos_near.y)) <= 0.8f)
        {
            return light_compare_depth(light,
                float3(projected_uv_near, (float)near_cascade), projected_pos_near.z);
        }
        float3 projected_pos_far = world_to_ndc(
            position,
            light_get_transform(light, far_cascade)
        );
        float2 projected_uv_far  = ndc_to_uv(projected_pos_far);
        float far_visibility = 1.0f;
        if (cascade_contains(projected_pos_far))
        {
            float shadowed = light_compare_depth(
                light,
                float3(projected_uv_far, (float)far_cascade),
                projected_pos_far.z
            );
            far_visibility = lerp(1.0f, shadowed, cascade_edge_fade(projected_pos_far));
        }
        if (cascade_contains(projected_pos_near))
        {
            float near_visibility = light_compare_depth(light,
                float3(projected_uv_near, (float)near_cascade), projected_pos_near.z);
            // Transition to the actual far-cascade shadow, never an unshadowed
            // ring which moves with the camera through the trees.
            float blend = smoothstep(0.8f, 0.95f, max(abs(projected_pos_near.x), abs(projected_pos_near.y)));
            return lerp(near_visibility, far_visibility, blend);
        }
        return far_visibility;
    }

    // spot or area light, both render a single perspective slice into the atlas
    float4 clip_pos = mul(
        float4(position, 1.0f),
        light_get_transform(light, 0)
    );
    if (clip_pos.w <= 0.0f)
    {
        return 1.0f;
    }

    float3 projected_pos = clip_pos.xyz / clip_pos.w;
    float2 projected_uv  = ndc_to_uv(projected_pos.xy);
    if (!is_valid_uv(projected_uv))
    {
        return 1.0f;
    }

    return light_compare_depth(
        light,
        float3(projected_uv, 0.0f),
        projected_pos.z
    );
}

// henyey greenstein phase, g 0 isotropic, positive forward scatter, negative back scatter
float henyey_greenstein_phase(float cos_theta, float g)
{
    cos_theta     = clamp(cos_theta, -1.0f, 1.0f);
    float g2      = g * g;
    float denom   = max(1.0f + g2 - 2.0f * g * cos_theta, 1e-4f);
    float denom32 = denom * sqrt(denom);
    return (1.0f - g2) / (4.0f * PI * denom32);
}

// local light direction and volumetric attenuation at a sample inside the medium
// area lights use the closest point on the rectangle, point and spot use the centroid
void compute_volumetric_light_sample(Light light, float3 sample_pos, out float3 light_dir, out float local_atten)
{
    if (light.is_directional())
    {
        light_dir   = normalize(-light.forward);
        local_atten = 1.0f;
        return;
    }

    const float soft_radius = 0.05f;

    if (light.is_area())
    {
        float3 closest    = light.compute_closest_point_on_area(sample_pos);
        float3 to_light   = closest - sample_pos;
        float  dist       = length(to_light);
        light_dir         = (dist > 1e-4f) ? to_light / dist : light.forward;

        float dist_eff    = max(dist, soft_radius);
        float range_atten = light.compute_attenuation_range(dist);
        float emitter_area = max(light.area_width * light.area_height, 0.0001f);
        float emission_cos = saturate(dot(light.forward, -light_dir));
        local_atten        = min((range_atten / (dist_eff * dist_eff)) * emission_cos * emitter_area, PI);
        return;
    }

    float3 to_light = light.position - sample_pos;
    float  dist     = length(to_light);
    light_dir       = (dist > 1e-4f) ? to_light / dist : float3(0.0f, 1.0f, 0.0f);

    float dist_eff    = max(dist, soft_radius);
    float range_atten = light.compute_attenuation_range(dist);
    local_atten       = range_atten / (dist_eff * dist_eff);

    if (light.is_spot())
    {
        float cd = dot(-light_dir, light.forward);
        float t  = saturate((cd - light.cos_outer) * light.angle_scale);
        local_atten *= t * t;
    }
}

#ifdef RAY_TRACING_ENABLED
float fog_trace_visibility(float3 origin, float3 direction, float t_max)
{
    return rt_trace_visibility(origin, direction, t_max);
}

float fog_trace_shadow(Light light, float3 sample_pos)
{
    float3 light_dir;
    float local_atten;
    compute_volumetric_light_sample(light, sample_pos, light_dir, local_atten);
    if (local_atten <= 0.0f)
    {
        return 0.0f;
    }

    float bias = 0.02f;
    float3 origin = sample_pos + light_dir * bias;
    float3 direction = light_dir;
    float t_max = 10000.0f;

    if (!light.is_directional())
    {
        float3 target = light.is_area()
            ? light.compute_closest_point_on_area(sample_pos)
            : light.position;
        float3 to_light = target - origin;
        float dist = length(to_light);
        if (dist <= bias)
        {
            return 1.0f;
        }

        direction = to_light / dist;
        float emitter_safety = 0.02f;
        if (light.is_area())
        {
            emitter_safety = min(min(light.area_width, light.area_height) * 0.5f, 0.08f) + 0.01f;
        }
        t_max = max(dist - emitter_safety, 0.001f);
    }

    return fog_trace_visibility(origin, direction, t_max);
}
#endif

Surface fog_build_surface(float3 position, float3 ray_direction, uint2 pixel, float2 uv)
{
    Surface surface;
    surface.flags                  = 0u;
    surface.albedo                 = 1.0f;
    surface.alpha                  = 1.0f;
    surface.roughness              = 1.0f;
    surface.roughness_alpha        = 1.0f;
    surface.metallic               = 0.0f;
    surface.clearcoat              = 0.0f;
    surface.clearcoat_roughness    = 0.0f;
    surface.anisotropic            = 0.0f;
    surface.anisotropic_rotation   = 0.0f;
    surface.sheen                  = 0.0f;
    surface.subsurface_scattering  = 0.0f;
    surface.flake_strength         = 0.0f;
    surface.flake_scale            = 0.0f;
    surface.pearl_strength         = 0.0f;
    surface.pearl_color            = 0.0f;
    surface.coat_tint              = 1.0f;
    surface.coat_tint_strength     = 0.0f;
    surface.ior                    = 1.5f;
    surface.absorption             = 0.0f;
    surface.thickness              = 0.0f;
    surface.occlusion              = 1.0f;
    surface.emissive               = 0.0f;
    surface.F0                     = 0.04f;
    surface.pos                    = pixel;
    surface.uv                     = uv;
    surface.depth                  = 0.0f;
    surface.position               = position;
    surface.normal                 = -ray_direction;
    surface.bent_normal            = surface.normal;
    surface.camera_to_pixel        = ray_direction;
    surface.camera_to_pixel_length = distance(position, get_camera_position());
    surface.diffuse_energy         = 1.0f;
    return surface;
}

#endif
