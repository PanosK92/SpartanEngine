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

// distant light coronas, only when the camera is outside the light's lighting range
// near lights stay cluster-lit with no corona, far lights keep a small soft disc forever

#include "common.hlsl"

#define THREAD_DIM 8

float light_luminance(float3 color)
{
    return dot(color, float3(0.2126f, 0.7152f, 0.0722f));
}

float flare_emitter_factor(LightParameters light)
{
    if (light.flags & (1u << 6))
    {
        return saturate(max(light.area_width, light.area_height) * 0.5f);
    }
    if (light.flags & (1u << 2))
    {
        return saturate(0.35f + tan(light.angle) * 0.4f);
    }
    return 0.5f;
}

float flare_facing(LightParameters light, float3 to_camera)
{
    if (light.flags & (1u << 1))
    {
        return 1.0f;
    }

    float cos_theta = dot(normalize(light.direction.xyz), to_camera);
    float facing    = saturate(0.45f + 0.55f * cos_theta);
    if (light.flags & (1u << 2))
    {
        float cos_outer = cos(light.angle);
        float cos_inner = cos(light.angle * 0.9f);
        float cone      = saturate((cos_theta - cos_outer) / max(cos_inner - cos_outer, 1e-4f));
        facing          = max(facing, cone);
    }
    return max(facing, 0.45f);
}

[numthreads(THREAD_DIM, THREAD_DIM, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // f3: near_distance, size_scale, intensity_scale
    // f3_2: max_size_px, occlusion, light_index
    // f2: disc_size_px, fade_length
    uint light_index = (uint)pass_get_f3_value2().z;
    if (light_index == 0u || light_index >= buffer_frame.cluster_light_count)
    {
        return;
    }

    LightParameters light = light_parameters[light_index];
    if ((light.flags & (1u << 0)) || light.intensity <= 0.0f)
    {
        return;
    }

    float near_distance     = max(pass_get_f3_value().x, 0.0f);
    float size_scale        = max(pass_get_f3_value().y, 0.01f);
    float intensity_scale   = max(pass_get_f3_value().z, 0.01f);
    float max_size_px       = max(pass_get_f3_value2().x, 1.0f);
    bool  occlusion_enabled = pass_get_f3_value2().y > 0.5f;
    float disc_size_px      = max(pass_get_f2_value().x, 1.0f);
    float fade_length       = max(pass_get_f2_value().y, 0.1f);

    if (thread_id.x >= (uint)disc_size_px || thread_id.y >= (uint)disc_size_px)
    {
        return;
    }

    float3 camera_pos = get_camera_position();
    float3 to_light   = light.position.xyz - camera_pos;
    float  distance   = length(to_light);
    if (distance < 0.05f)
    {
        return;
    }

    // gone at near_distance, fully visible at near_distance + fade_length, calibrate via cvars
    float fade = smoothstep(near_distance, near_distance + fade_length, distance);
    if (fade <= 0.0f)
    {
        return;
    }

    float3 to_camera = -to_light / distance;
    float  facing    = flare_facing(light, to_camera);

    float4 clip_pos = mul(float4(light.position.xyz, 1.0f), get_view_projection());
    if (clip_pos.w <= 0.0f)
    {
        return;
    }

    float2 ndc       = clip_pos.xy / clip_pos.w;
    float2 screen_uv = float2(ndc.x * 0.5f + 0.5f, 1.0f - (ndc.y * 0.5f + 0.5f));
    if (screen_uv.x < -0.05f || screen_uv.x > 1.05f || screen_uv.y < -0.05f || screen_uv.y > 1.05f)
    {
        return;
    }

    float2 active_res = get_render_resolution_active();
    float lum         = max(light_luminance(light.color.rgb * light.intensity), 1e-4f);

    // small screen-space dots, not perspective-projected discs
    float radius_px = (1.5f + 1.25f * flare_emitter_factor(light) + 0.75f * saturate(0.12f * sqrt(lum))) * size_scale;
    radius_px       = clamp(radius_px, 1.0f, max_size_px);

    float2 offset  = float2(thread_id.xy) + 0.5f - disc_size_px * 0.5f;
    float  dist_px = length(offset);
    if (dist_px > radius_px)
    {
        return;
    }

    float2 pixel_coord = screen_uv * active_res + offset;
    if (pixel_coord.x < 0.0f || pixel_coord.y < 0.0f || pixel_coord.x >= active_res.x || pixel_coord.y >= active_res.y)
    {
        return;
    }

    uint2 pixel_coord_int = uint2(pixel_coord);
    float2 render_uv      = (float2(pixel_coord_int) + 0.5f) / max(buffer_frame.resolution_render, float2(1.0f, 1.0f));
    float2 screen_uv_px   = render_uv_to_screen_uv(render_uv);

    float occlusion = 1.0f;
    if (occlusion_enabled)
    {
        float scene_depth_raw = get_depth(render_uv);
        if (scene_depth_raw > 1e-5f)
        {
            float3 scene_pos = get_position(scene_depth_raw, screen_uv_px);
            float scene_dist = length(scene_pos - camera_pos);
            occlusion        = saturate((scene_dist - distance + 0.5f) / 1.0f);
            if (occlusion <= 0.0f)
            {
                return;
            }
        }
    }

    float t       = dist_px / max(radius_px, 1e-3f);
    float falloff = exp(-t * t * 6.0f) * saturate(1.0f - t);

    float brightness = intensity_scale * (0.06f + 0.25f * saturate(0.18f * sqrt(lum))) * fade * facing * occlusion;
    float3 color     = light.color.rgb * (brightness * falloff);

    float4 base              = tex_uav[pixel_coord_int];
    tex_uav[pixel_coord_int] = float4(base.rgb + color, base.a);
}
