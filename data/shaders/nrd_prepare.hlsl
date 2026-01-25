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

// compute shader to prepare nrd input textures from g-buffer and path tracer output

#include "common.hlsl"

// input textures
Texture2D<float4> tex_noisy_radiance : register(t0);

// output textures for nrd
RWTexture2D<float>  tex_nrd_viewz            : register(u0);
RWTexture2D<float4> tex_nrd_normal_roughness : register(u1);
RWTexture2D<float4> tex_nrd_diff_radiance    : register(u2);
RWTexture2D<float4> tex_nrd_spec_radiance    : register(u3);

// octahedron encoding for normals
float2 oct_wrap(float2 v)
{
    return (1.0f - abs(v.yx)) * select(v.xy >= 0.0f, 1.0f, -1.0f);
}

float2 encode_oct(float3 n)
{
    n /= (abs(n.x) + abs(n.y) + abs(n.z));
    n.xy = n.z >= 0.0f ? n.xy : oct_wrap(n.xy);
    return n.xy;
}

// pack normal and roughness for nrd (r10g10b10a2 unorm format)
float4 pack_normal_roughness(float3 normal, float roughness)
{
    float2 oct = encode_oct(normal);
    oct = oct * 0.5f + 0.5f;
    return float4(oct.x, oct.y, roughness, 0.0f);
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 pos = thread_id.xy;
    float2 resolution = buffer_frame.resolution_render;
    
    if (pos.x >= (uint)resolution.x || pos.y >= (uint)resolution.y)
        return;
    
    float2 uv = (pos + 0.5f) / resolution;
    
    // get depth and check for sky
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    
    if (depth <= 0.0f)
    {
        // sky pixel
        tex_nrd_viewz[pos]            = 100000.0f;
        tex_nrd_normal_roughness[pos] = float4(0.5f, 0.5f, 0.0f, 0.0f);
        tex_nrd_diff_radiance[pos]    = float4(0.0f, 0.0f, 0.0f, 0.0f);
        tex_nrd_spec_radiance[pos]    = float4(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }
    
    // compute linear view z from world position
    float3 pos_world = get_position(uv);
    float3 pos_view  = mul(buffer_frame.view, float4(pos_world, 1.0f)).xyz;
    float view_z     = pos_view.z;
    
    // get normal and material properties
    float3 normal_world = get_normal(uv);
    float4 material     = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float roughness     = max(material.r, 0.04f);
    float metallic      = material.g;
    
    // get noisy radiance from path tracer
    float3 radiance = tex_noisy_radiance.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb;
    
    // estimate hit distance from depth (approximation)
    float hit_distance = length(pos_world - buffer_frame.camera_position);
    
    // split radiance into diffuse/specular based on metallic
    // for dielectrics: mostly diffuse, for metals: mostly specular
    float3 diffuse_radiance  = radiance * (1.0f - metallic);
    float3 specular_radiance = radiance * metallic;
    
    // write nrd input textures
    tex_nrd_viewz[pos]            = view_z;
    tex_nrd_normal_roughness[pos] = pack_normal_roughness(normal_world, roughness);
    tex_nrd_diff_radiance[pos]    = float4(diffuse_radiance, hit_distance);
    tex_nrd_spec_radiance[pos]    = float4(specular_radiance, hit_distance);
}
