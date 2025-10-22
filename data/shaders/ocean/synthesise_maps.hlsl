/*
Copyright(c) 2025 George Bolba

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

#include "common_ocean.hlsl"

// Inspired by https://www.shadertoy.com/view/tsVGRd

static const float2 hex_ratio = float2(1.0f, sqrt(3.0f));

float4 get_hex_grid_info(float2 uv)
{
    const float4 hex_index = round(float4(uv, uv - float2(0.5f, 1.0f)) / hex_ratio.xyxy);
    const float4 hex_center = float4(hex_index.xy * hex_ratio, (hex_index.zw + 0.5f) * hex_ratio);
    const float4 offset = uv.xyxy - hex_center;
    return dot(offset.xy, offset.xy) < dot(offset.zw, offset.zw) ? float4(hex_center.xy, hex_index.xy) : float4(hex_center.zw, hex_index.zw);
}

float get_hex_sdf(float2 p)
{
    p = abs(p);
    return 0.5f - max(dot(p, hex_ratio * 0.5f), p.x);
}

//xy: node pos, z: weight
float3 get_triangle_interp_node(float2 pos, float freq, int node_index)
{
    const float2 node_offsets[3] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, -1.0f)
    };

    const float2 uv = pos * freq + node_offsets[node_index] / hex_ratio.xy * 0.5f;
    const float4 hex_info = get_hex_grid_info(uv);
    const float dist = get_hex_sdf(uv - hex_info.xy) * 2.0f;
    return float3(hex_info.xy / freq, dist);
}

float3 hash33(float3 p)
{
    p = float3(dot(p, float3(127.1f, 311.7f, 74.7f)),
			  dot(p, float3(269.5f, 183.3f, 246.1f)),
			  dot(p, float3(113.5f, 271.9f, 124.6f)));

    return frac(sin(p) * 43758.5453123f);
}

float4 get_texture_sample(Texture2D texture, float2 pos, float freq, float2 node_point)
{
    const float3 hash = hash33(float3(node_point.xy, 0.0f));

    const float2 uv = pos * freq + hash.yz;
    return texture.SampleLevel(samplers[sampler_anisotropic_wrap], uv, 0);
}

void preserve_variance(out float4 linear_color, float4 mean_color, float moment2)
{
    linear_color = (linear_color - mean_color) / sqrt(moment2) + mean_color;
}

void synthesize(Texture2D example, out float4 output, float2 uv)
{
    const float tex_freq = 1.0f;
    const float tile_freq = 2.0f;
    
    output = float4(0.0f, 0.0f, 0.0f, 0.0f); // init to 0.0f for safety reasons
    float moment2 = 0.0f;

    for (int i = 0; i < 3; i++)
    {
        const float3 interp_node = get_triangle_interp_node(uv, tile_freq, i);
        output += get_texture_sample(example, uv, tex_freq, interp_node.xy) * interp_node.z;
        
        moment2 += interp_node.z * interp_node.z;
    }
    // assumes example is a mip mapped 512x512 texture and samples lowest mip (1x1)
    const float4 mean_example = example.SampleLevel(samplers[sampler_point_clamp], uv, 9);
    
    preserve_variance(output, mean_example, moment2);
}

void synthesize_with_flow(Texture2D example, out float4 output, Texture2D flowmap, float2 tile_xz_pos, float wind_dir_deg, float2 tile_local_uv, bool debug_flow = false)
{
    const float tex_freq = 1.0f;
    const float tile_freq = 2.0f;
    
    // convert wind dir from degrees to a 2d vector
    const float wind_dir_rad = radians(wind_dir_deg);
    const float2 wind_dir = float2(cos(wind_dir_rad), sin(wind_dir_rad)) * float2(-1.0f, -1.0f);

    output = float4(0.0f, 0.0f, 0.0f, 0.0f); // init to 0.0f for safety reasons
    float moment2 = 0.0f;
    float2 output_flow = float2(0.0f, 0.0f);

    for (int i = 0; i < 3; i++)
    {
        const float3 interp_node = get_triangle_interp_node(tile_local_uv, tile_freq, i);

        float2 node_world_pos = tile_xz_pos;
        float2 node_world_uv = (node_world_pos - (-3069.0f)) / (3069.0f - (-3069.0f));

        float2 flow_dir = flowmap.SampleLevel(samplers[sampler_bilinear_wrap], node_world_uv, 0).rg;
        flow_dir = normalize(flow_dir * 2.0f - 1.0f);
        flow_dir = float2(flow_dir.x, flow_dir.y) * interp_node.z;
        output_flow += flow_dir;
        
        const float theta = atan2(flow_dir.y, flow_dir.x) - atan2(wind_dir.y, wind_dir.x);
        const float cosT = cos(theta);
        const float sinT = sin(theta);
        const float2x2 R2 = float2x2(cosT, -sinT, sinT, cosT);
        const float3x3 R3 = float3x3(cosT, 0, sinT,
                                     0, 1, 0,
                                    -sinT, 0, cosT);
        const float2 rotated_pos = interp_node.xy + mul(R2, tile_local_uv - interp_node.xy);
        
        float4 sample = get_texture_sample(example, rotated_pos, tex_freq, interp_node.xy);

        sample.xyz = mul(R3, sample.xyz);
        
        output += sample * interp_node.z;
        moment2 += interp_node.z * interp_node.z;
    }
    // assumes example is a mip mapped 512x512 texture and samples lowest mip (1x1)
    const float4 mean_example = example.SampleLevel(samplers[sampler_point_clamp], tile_local_uv, 9);
    
    preserve_variance(output, mean_example, moment2);

    if (debug_flow)
    {
        //output = float4(output_flow * 0.5f + 0.5f, 0.0f, 1.0f);
        output = float4(output_flow, 0.0f, 1.0f);
    }
}

float2 ocean_get_world_space_uvs(float2 uv, float2 tile_xz_pos, float tile_size)
{
    const float2 tile_origin_uv_space = float2(tile_xz_pos.x / tile_size, tile_xz_pos.y / tile_size);

    return tile_origin_uv_space + uv;
}
