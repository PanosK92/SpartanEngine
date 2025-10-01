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

static const float2 hexRatio = float2(1.0f, sqrt(3.0f));

float4 GetHexGridInfo(float2 uv)
{
    float4 hexIndex = round(float4(uv, uv - float2(0.5f, 1.0f)) / hexRatio.xyxy);
    float4 hexCenter = float4(hexIndex.xy * hexRatio, (hexIndex.zw + 0.5f) * hexRatio);
    float4 offset = uv.xyxy - hexCenter;
    return dot(offset.xy, offset.xy) < dot(offset.zw, offset.zw) ?
    float4(hexCenter.xy, hexIndex.xy) :
    float4(hexCenter.zw, hexIndex.zw);
}

float GetHexSDF(in float2 p)
{
    p = abs(p);
    return 0.5f - max(dot(p, hexRatio * 0.5f), p.x);
}

//xy: node pos, z: weight
float3 GetTriangleInterpNode(in float2 pos, in float freq, in int nodeIndex)
{
    float2 nodeOffsets[3] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, -1.0f)
    };

    float2 uv = pos * freq + nodeOffsets[nodeIndex] / hexRatio.xy * 0.5f;
    float4 hexInfo = GetHexGridInfo(uv);
    float dist = GetHexSDF(uv - hexInfo.xy) * 2.0f;
    return float3(hexInfo.xy / freq, dist);
}

float3 hash33(float3 p)
{
    p = float3(dot(p, float3(127.1f, 311.7f, 74.7f)),
			  dot(p, float3(269.5f, 183.3f, 246.1f)),
			  dot(p, float3(113.5f, 271.9f, 124.6f)));

    return frac(sin(p) * 43758.5453123f);
}

float4 GetTextureSample(Texture2D texture, float2 pos, float freq, float2 nodePoint, float z)
{
    const float3 hash = hash33(float3(nodePoint.xy, z));

    float2 uv = pos * freq + hash.yz;
    uv = pos * freq + hash.yz;
    return texture.SampleLevel(samplers[sampler_point_wrap], uv, 0);
}

float3 PreserveVariance(float3 linearColor, float3 meanColor, float moment2)
{
    return (linearColor - meanColor) / sqrt(moment2) + meanColor;
}

float4 PreserveVariance(float4 linearColor, float4 meanColor, float moment2)
{
    return (linearColor - meanColor) / sqrt(moment2) + meanColor;
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

    uv = tile_origin_uv_space + uv * tile_size;
    
    const float tex_freq = 2.0f;
    const float tile_freq = 1.0f;

    float3 displacement = float3(0.0f, 0.0f, 0.0f);
    float4 slope = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float moment2 = 0.0f;
    for (int i = 0; i < 3; i++)
    {
        float3 interp_node = GetTriangleInterpNode(uv, tile_freq, i);
        // tex2 = displacement_map as SRV
        const float ocean_tile_index = pass_get_f2_value().x;
        displacement.xyz += GetTextureSample(tex2, uv, tex_freq, interp_node.xy, ocean_tile_index).rgb * interp_node.z;
        slope += GetTextureSample(tex3, uv, tex_freq, interp_node.xy, ocean_tile_index) * interp_node.z;

        moment2 += interp_node.z * interp_node.z;
    }
    const float3 mean_displacement = tex2.SampleLevel(samplers[sampler_point_clamp], uv, 9).rgb;
    const float4 mean_slope = tex3.SampleLevel(samplers[sampler_point_clamp], uv, 9);
    
    synthesised_displacement[thread_id.xy] = float4(PreserveVariance(displacement, mean_displacement, moment2), 1.0f);
    synthesised_slope[thread_id.xy] = PreserveVariance(slope, mean_slope, moment2);

	//synthesised_displacement[thread_id.xy] = tex2[thread_id.xy];
    synthesised_slope[thread_id.xy] = tex3[thread_id.xy];
}
