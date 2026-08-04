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

//= INCLUDES =============
#include "common.hlsl"
#include "sky/clouds.hlsl"
//========================

// returns 1 if the world space position is lit by the light, 0 if occluded
// directional picks a single cascade per sample instead of paying for both
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

        float4 clip_pos = mul(float4(position, 1.0f), light.transform[face_index]);
        if (clip_pos.w <= 0.0f)
            return 1.0f;

        float3 ndc          = clip_pos.xyz / clip_pos.w;
        float2 projected_uv = ndc_to_uv(ndc.xy);
        return light.compare_depth(float3(projected_uv, (float)face_index), ndc.z);
    }

    if (light.is_directional())
    {
        // try the near cascade first, fall back to the far cascade if outside its frustum
        const uint near_cascade = 0;
        const uint far_cascade  = 1;

        float3 projected_pos_near = world_to_ndc(position, light.transform[near_cascade]);
        float2 projected_uv_near  = ndc_to_uv(projected_pos_near);
        if (is_valid_uv(projected_uv_near))
        {
            return light.compare_depth(float3(projected_uv_near, (float)near_cascade), projected_pos_near.z);
        }

        float3 projected_pos_far = world_to_ndc(position, light.transform[far_cascade]);
        float2 projected_uv_far  = ndc_to_uv(projected_pos_far);
        if (is_valid_uv(projected_uv_far))
        {
            return light.compare_depth(float3(projected_uv_far, (float)far_cascade), projected_pos_far.z);
        }

        return 1.0f;
    }

    // spot or area light, both render a single perspective slice into the atlas
    float4 clip_pos = mul(float4(position, 1.0f), light.transform[0]);
    if (clip_pos.w <= 0.0f)
        return 1.0f;

    float3 projected_pos = clip_pos.xyz / clip_pos.w;
    float2 projected_uv  = ndc_to_uv(projected_pos.xy);
    if (!is_valid_uv(projected_uv))
        return 1.0f;

    return light.compare_depth(float3(projected_uv, 0.0f), projected_pos.z);
}

// henyey greenstein phase, g 0 isotropic, positive forward scatter, negative back scatter
float henyey_greenstein_phase(float cos_theta, float g)
{
    cos_theta     = clamp(cos_theta, -1.0f, 1.0f);
    float g2      = g * g;
    float denom   = max(1.0f + g2 - 2.0f * g * cos_theta, 1e-4f);
    // d * sqrt(d) instead of pow(d, 1.5)
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

    // soft minimum distance so the inverse square does not blow up at the surface
    const float soft_radius = 0.05f;

    if (light.is_area())
    {
        float3 closest    = light.compute_closest_point_on_area(sample_pos);
        float3 to_light   = closest - sample_pos;
        float  dist       = length(to_light);
        light_dir         = (dist > 1e-4f) ? to_light / dist : light.forward;

        float dist_eff    = max(dist, soft_radius);
        float range_atten = light.compute_attenuation_range(dist);
        float emitter_area = 0.5f * max(light.area_width * light.area_height, 0.0001f);
        float emission_cos = saturate(dot(light.forward, -light_dir));
        local_atten        = min((range_atten / (dist_eff * dist_eff)) * emission_cos * emitter_area, PI);
        return;
    }

    // point or spot
    float3 to_light = light.position - sample_pos;
    float  dist     = length(to_light);
    light_dir       = (dist > 1e-4f) ? to_light / dist : float3(0.0f, 1.0f, 0.0f);

    float dist_eff    = max(dist, soft_radius);
    float range_atten = light.compute_attenuation_range(dist);
    local_atten       = range_atten / (dist_eff * dist_eff);

    if (light.is_spot())
    {
        // cos_outer and angle_scale are precomputed in Light::Build to keep this path trig free
        float cd          = dot(-light_dir, light.forward);
        float angle_atten = saturate((cd - light.cos_outer) * light.angle_scale);
        local_atten      *= angle_atten * angle_atten;
    }
}
