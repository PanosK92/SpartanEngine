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

//= INCLUDES ===================
#include "common.hlsl"
#include "restir_reservoir.hlsl"
//==============================

// current frame reservoir uavs
RWTexture2D<float4> tex_reservoir0 : register(u21);
RWTexture2D<float4> tex_reservoir1 : register(u22);
RWTexture2D<float4> tex_reservoir2 : register(u23);
RWTexture2D<float4> tex_reservoir3 : register(u24);
RWTexture2D<float4> tex_reservoir4 : register(u25);

// previous frame reservoir srvs
Texture2D<float4> tex_reservoir_prev0 : register(t21);
Texture2D<float4> tex_reservoir_prev1 : register(t22);
Texture2D<float4> tex_reservoir_prev2 : register(t23);
Texture2D<float4> tex_reservoir_prev3 : register(t24);
Texture2D<float4> tex_reservoir_prev4 : register(t25);

float2 reproject_to_previous_frame(float2 current_uv, float depth)
{
    float3 world_pos = get_position(depth, current_uv);
    float4 prev_clip = mul(float4(world_pos, 1.0f), buffer_frame.view_projection_previous);
    float2 prev_ndc  = prev_clip.xy / prev_clip.w;
    float2 prev_uv   = prev_ndc * float2(0.5f, -0.5f) + 0.5f;
    return prev_uv;
}

bool is_temporal_sample_valid(float2 prev_uv, float3 current_pos, float3 current_normal)
{
    if (!is_valid_uv(prev_uv))
        return false;
    
    float prev_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).r;
    if (prev_depth <= 0.0f)
        return false;
    
    float3 prev_pos    = get_position(prev_depth, prev_uv);
    float3 prev_normal = get_normal(prev_uv);
    
    // position check
    float pos_dist = length(current_pos - prev_pos);
    float max_dist = length(current_pos - buffer_frame.camera_position) * 0.05f;
    if (pos_dist > max_dist)
        return false;
    
    // normal check
    if (dot(current_normal, prev_normal) < RESTIR_NORMAL_THRESHOLD)
        return false;
    
    return true;
}

float evaluate_target_pdf_at_point(PathSample sample, float3 shading_pos, float3 shading_normal)
{
    return calculate_target_pdf(sample.radiance);
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    
    float2 resolution = buffer_frame.resolution_render;
    if (pixel.x >= (uint)resolution.x || pixel.y >= (uint)resolution.y)
        return;
    
    float2 uv = (pixel + 0.5f) / resolution;
    
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
        return;
    
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    
    // load current reservoir
    Reservoir current = unpack_reservoir(
        tex_reservoir0[pixel],
        tex_reservoir1[pixel],
        tex_reservoir2[pixel],
        tex_reservoir3[pixel],
        tex_reservoir4[pixel]
    );
    
    uint seed = create_seed(pixel, buffer_frame.frame);
    
    // init combined reservoir
    Reservoir combined = create_empty_reservoir();
    
    float target_pdf_current = calculate_target_pdf(current.sample.radiance);
    float weight_current = target_pdf_current * current.W * current.M;
    combined.weight_sum = weight_current;
    combined.M = current.M;
    combined.sample = current.sample;
    combined.target_pdf = target_pdf_current;
    
    // temporal reuse
    float2 prev_uv = reproject_to_previous_frame(uv, depth);
    
    if (is_temporal_sample_valid(prev_uv, pos_ws, normal_ws))
    {
        int2 prev_pixel = int2(prev_uv * resolution);
        prev_pixel = clamp(prev_pixel, int2(0, 0), int2(resolution) - int2(1, 1));
        
        Reservoir temporal = unpack_reservoir(
            tex_reservoir_prev0[prev_pixel],
            tex_reservoir_prev1[prev_pixel],
            tex_reservoir_prev2[prev_pixel],
            tex_reservoir_prev3[prev_pixel],
            tex_reservoir_prev4[prev_pixel]
        );
        
        clamp_reservoir_M(temporal, RESTIR_M_CAP);
        
        float target_pdf_temporal = evaluate_target_pdf_at_point(temporal.sample, pos_ws, normal_ws);
        
        float rand = random_float(seed);
        if (merge_reservoir(combined, temporal, target_pdf_temporal, rand))
            combined.target_pdf = target_pdf_temporal;
    }
    
    clamp_reservoir_M(combined, RESTIR_M_CAP);
    
    // finalize weight
    if (combined.target_pdf > 0 && combined.M > 0)
        combined.W = combined.weight_sum / (combined.target_pdf * combined.M);
    else
        combined.W = 0;
    
    // write reservoir
    float4 t0, t1, t2, t3, t4;
    pack_reservoir(combined, t0, t1, t2, t3, t4);
    tex_reservoir0[pixel] = t0;
    tex_reservoir1[pixel] = t1;
    tex_reservoir2[pixel] = t2;
    tex_reservoir3[pixel] = t3;
    tex_reservoir4[pixel] = t4;
    
    // output
    float3 gi = combined.sample.radiance * combined.W;
    
    // firefly clamp
    float lum = luminance(gi);
    if (lum > 100.0f)
        gi *= 100.0f / lum;
    
    tex_uav[pixel] = float4(gi, 1.0f);
}
