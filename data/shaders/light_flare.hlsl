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

struct PixelInput
{
    float4 position : SV_POSITION;
    nointerpolation float2 center_pixel : TEXCOORD0;
    nointerpolation uint light_index : TEXCOORD1;
    nointerpolation float visible : TEXCOORD2;
};

PixelInput main_vs(uint vertex_id : SV_VertexID)
{
    static const float2 positions[6] =
    {
        float2(-1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f, -1.0f),
        float2( 1.0f, -1.0f),
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f)
    };

    PixelInput output;
    const float2 corner     = positions[vertex_id];
    const float disc_size   = max(pass_get_f2_value().x, 1.0f);
    const float2 resolution = max(get_render_resolution_active(), float2(1.0f, 1.0f));
    output.light_index      = (uint)pass_get_f3_value2().z;
    output.visible          = output.light_index > 0u && output.light_index < buffer_frame.cluster_light_count ? 1.0f : 0.0f;

    LightParameters light = light_parameters[min(output.light_index, max(buffer_frame.cluster_light_count, 1u) - 1u)];
    float4 center_position = mul(float4(light.position.xyz, 1.0f), get_view_projection());
    output.visible        *= center_position.w > 0.0f ? 1.0f : 0.0f;
    output.position        = center_position;
    output.position.xy    += corner * disc_size / resolution * center_position.w;
    float2 ndc             = center_position.xy / max(center_position.w, 1e-4f);
    output.center_pixel    = float2(ndc.x * 0.5f + 0.5f, 1.0f - (ndc.y * 0.5f + 0.5f)) * resolution;
    return output;
}

float4 main_ps(PixelInput input) : SV_TARGET
{
    uint light_index = input.light_index;
    if (light_index == 0u || light_index >= buffer_frame.cluster_light_count)
    {
        discard;
    }

    LightParameters light = light_parameters[light_index];
    if (input.visible == 0.0f || (light.flags & (1u << 0)) || light.intensity <= 0.0f)
    {
        discard;
    }

    float near_distance     = max(pass_get_f3_value().x, 0.0f);
    float size_scale        = max(pass_get_f3_value().y, 0.01f);
    float intensity_scale   = max(pass_get_f3_value().z, 0.01f);
    float max_size_px       = max(pass_get_f3_value2().x, 1.0f);
    bool  occlusion_enabled = pass_get_f3_value2().y > 0.5f;
    float fade_length       = max(pass_get_f2_value().y, 0.1f);

    float3 camera_pos = get_camera_position();
    float3 to_light   = light.position.xyz - camera_pos;
    float  distance   = length(to_light);
    if (distance < 0.05f)
    {
        discard;
    }

    float fade = smoothstep(near_distance, near_distance + fade_length, distance);
    if (fade <= 0.0f)
    {
        discard;
    }

    float3 to_camera = -to_light / distance;
    float  facing    = flare_facing(light, to_camera);

    float lum = max(light_luminance(light.color.rgb * light.intensity), 1e-4f);

    float radius_px = (1.5f + 1.25f * flare_emitter_factor(light) + 0.75f * saturate(0.12f * sqrt(lum))) * size_scale;
    radius_px       = clamp(radius_px, 1.0f, max_size_px);

    float2 offset  = input.position.xy - input.center_pixel;
    float  dist_px = length(offset);
    if (dist_px > radius_px)
    {
        discard;
    }

    uint2 pixel_coord_int = uint2(input.position.xy);
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
                discard;
            }
        }
    }

    float t       = dist_px / max(radius_px, 1e-3f);
    float falloff = exp(-t * t * 6.0f) * saturate(1.0f - t);

    float brightness = intensity_scale * (0.06f + 0.25f * saturate(0.18f * sqrt(lum))) * fade * facing * occlusion;
    float3 color     = light.color.rgb * (brightness * falloff);

    return float4(color, 0.0f);
}
