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

float get_hex_sdf(in float2 p)
{
    p = abs(p);
    return 0.5f - max(dot(p, hex_ratio * 0.5f), p.x);
}

//xy: node pos, z: weight
float3 get_triangle_interp_node(in float2 pos, in float freq, in int node_index)
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
    return texture.SampleLevel(samplers[sampler_point_wrap], uv, 0);
}

void preserve_variance(out float4 linear_color, float4 mean_color, float moment2)
{
    linear_color = (linear_color - mean_color) / sqrt(moment2) + mean_color;
}

void synthesize(Texture2D example, out float4 output, float2 uv, float tex_freq, float tile_freq)
{
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

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    const uint2 pixel_coord = thread_id.xy;
    uint2 texture_size;
    synthesised_displacement.GetDimensions(texture_size.x, texture_size.y);
    
    float2 uv = (pixel_coord + 0.5f) / texture_size;

    const float3 pass_values = pass_get_f3_value();
    const float2 tile_xz_pos = pass_values.xy;
    const float tile_size = pass_values.z;

    const float2 tile_origin_uv_space = float2(tile_xz_pos.x / tile_size, tile_xz_pos.y / tile_size);

    uv = tile_origin_uv_space + uv;
    
    const float tex_freq = 1.0f;
    const float tile_freq = 2.0f;

    float4 displacement = float4(0.0f, 0.0f, 0.0f, 0.0f);
    synthesize(tex2, displacement, uv, tex_freq, tile_freq);
    
    float4 slope = float4(0.0f, 0.0f, 0.0f, 0.0f);
    synthesize(tex3, slope, uv, tex_freq, tile_freq);

    synthesised_displacement[thread_id.xy] = displacement;
    synthesised_slope[thread_id.xy] = slope;
    
	//synthesised_displacement[thread_id.xy] = tex2[thread_id.xy];
    //synthesised_slope[thread_id.xy] = tex3[thread_id.xy];
}
