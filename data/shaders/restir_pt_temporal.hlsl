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

/*------------------------------------------------------------------------------
    RESOURCES
------------------------------------------------------------------------------*/
RWTexture2D<float4> tex_reservoir0 : register(u21);
RWTexture2D<float4> tex_reservoir1 : register(u22);
RWTexture2D<float4> tex_reservoir2 : register(u23);
RWTexture2D<float4> tex_reservoir3 : register(u24);
RWTexture2D<float4> tex_reservoir4 : register(u25);

Texture2D<float4> tex_reservoir_prev0 : register(t21);
Texture2D<float4> tex_reservoir_prev1 : register(t22);
Texture2D<float4> tex_reservoir_prev2 : register(t23);
Texture2D<float4> tex_reservoir_prev3 : register(t24);
Texture2D<float4> tex_reservoir_prev4 : register(t25);

/*------------------------------------------------------------------------------
    TEMPORAL REPROJECTION
------------------------------------------------------------------------------*/
float2 reproject_to_previous_frame(float2 current_uv)
{
    float2 velocity = tex_velocity.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv, 0).xy;
    return current_uv - velocity;
}

bool is_temporal_sample_valid(float2 current_uv, float2 prev_uv, float3 current_pos, float3 current_normal, float current_depth, out float confidence)
{
    confidence = 1.0f;
    
    if (!is_valid_uv(prev_uv))
        return false;
    
    // check depth at reprojected location
    float prev_uv_depth_raw = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), prev_uv, 0).r;
    if (prev_uv_depth_raw <= 0.0f)
        return false;
    
    // position consistency check
    float3 prev_uv_pos_ws = get_position(prev_uv);
    float3 pos_diff       = current_pos - prev_uv_pos_ws;
    float pos_distance    = length(pos_diff);
    
    // depth-scaled threshold
    float distance_threshold = max(current_depth * 0.02f, 0.05f);
    if (pos_distance > distance_threshold)
    {
        confidence = 0.0f;
        return false;
    }
    
    // normal consistency check
    float3 prev_uv_normal   = get_normal(prev_uv);
    float normal_similarity = dot(current_normal, prev_uv_normal);
    
    if (normal_similarity < 0.8f)
        return false;
    
    // disocclusion detection via motion and depth edges
    float2 motion       = (current_uv - prev_uv) * buffer_frame.resolution_render;
    float motion_length = length(motion);
    
    float2 texel_size    = 1.0f / buffer_frame.resolution_render;
    float depth_left     = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv + float2(-texel_size.x, 0), 0).r;
    float depth_right    = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), current_uv + float2(texel_size.x, 0), 0).r;
    float depth_gradient = abs(linearize_depth(depth_left) - linearize_depth(depth_right));
    bool is_depth_edge   = depth_gradient > current_depth * 0.1f;
    
    float edge_penalty = is_depth_edge ? saturate(1.0f - motion_length * 0.1f) : 1.0f;
    
    // aggregate confidence factors
    float pos_confidence    = saturate(1.0f - pos_distance / distance_threshold);
    float normal_confidence = saturate((normal_similarity - 0.8f) / 0.15f);
    float motion_confidence = saturate(1.0f - motion_length * 0.01f);
    
    confidence = pos_confidence * normal_confidence * motion_confidence * edge_penalty;
    return true;
}

/*------------------------------------------------------------------------------
    MAIN
------------------------------------------------------------------------------*/
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
    
    // load current frame reservoir
    Reservoir current = unpack_reservoir(
        tex_reservoir0[pixel],
        tex_reservoir1[pixel],
        tex_reservoir2[pixel],
        tex_reservoir3[pixel],
        tex_reservoir4[pixel]
    );
    
    uint seed = create_seed(pixel, buffer_frame.frame);
    
    // initialize combined reservoir with current sample
    Reservoir combined = create_empty_reservoir();
    
    float target_pdf_current = calculate_target_pdf(current.sample.radiance);
    float weight_current     = target_pdf_current * current.W * current.M;
    combined.weight_sum      = weight_current;
    combined.M               = current.M;
    combined.sample          = current.sample;
    combined.target_pdf      = target_pdf_current;
    
    // temporal reuse from previous frame
    float2 prev_uv = reproject_to_previous_frame(uv);
    float temporal_confidence;
    float linear_depth = linearize_depth(depth);
    
    if (is_temporal_sample_valid(uv, prev_uv, pos_ws, normal_ws, linear_depth, temporal_confidence))
    {
        int2 prev_pixel = clamp(int2(prev_uv * resolution), int2(0, 0), int2(resolution) - int2(1, 1));
        
        Reservoir temporal = unpack_reservoir(
            tex_reservoir_prev0[prev_pixel],
            tex_reservoir_prev1[prev_pixel],
            tex_reservoir_prev2[prev_pixel],
            tex_reservoir_prev3[prev_pixel],
            tex_reservoir_prev4[prev_pixel]
        );
        
        // apply temporal decay
        temporal.M          *= RESTIR_TEMPORAL_DECAY;
        temporal.weight_sum *= RESTIR_TEMPORAL_DECAY;
        
        float effective_M_cap = RESTIR_M_CAP * temporal_confidence;
        clamp_reservoir_M(temporal, max(effective_M_cap, 4.0f));
        
        float target_pdf_temporal = calculate_target_pdf(temporal.sample.radiance);
        
        if (merge_reservoir(combined, temporal, target_pdf_temporal, random_float(seed)))
            combined.target_pdf = target_pdf_temporal;
    }
    
    clamp_reservoir_M(combined, RESTIR_M_CAP);
    
    // compute final weight
    if (combined.target_pdf > 0 && combined.M > 0)
        combined.W = combined.weight_sum / (combined.target_pdf * combined.M);
    else
        combined.W = 0;
    
    combined.W = min(combined.W, 20.0f);
    
    // store reservoir
    float4 t0, t1, t2, t3, t4;
    pack_reservoir(combined, t0, t1, t2, t3, t4);
    tex_reservoir0[pixel] = t0;
    tex_reservoir1[pixel] = t1;
    tex_reservoir2[pixel] = t2;
    tex_reservoir3[pixel] = t3;
    tex_reservoir4[pixel] = t4;
    
    float3 gi = combined.sample.radiance * combined.W;
    
    // numerical stability
    if (any(isnan(gi)) || any(isinf(gi)))
        gi = float3(0.0f, 0.0f, 0.0f);
    
    // clamp extreme values
    float lum = luminance(gi);
    if (lum > 200.0f)
        gi *= 200.0f / lum;
    
    tex_uav[pixel] = float4(gi, 1.0f);
}
