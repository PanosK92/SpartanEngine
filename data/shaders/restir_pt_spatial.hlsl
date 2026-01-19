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

// input reservoir srvs (from temporal pass)
Texture2D<float4> tex_reservoir_in0 : register(t21);
Texture2D<float4> tex_reservoir_in1 : register(t22);
Texture2D<float4> tex_reservoir_in2 : register(t23);
Texture2D<float4> tex_reservoir_in3 : register(t24);
Texture2D<float4> tex_reservoir_in4 : register(t25);

// output reservoir uavs
RWTexture2D<float4> tex_reservoir0 : register(u21);
RWTexture2D<float4> tex_reservoir1 : register(u22);
RWTexture2D<float4> tex_reservoir2 : register(u23);
RWTexture2D<float4> tex_reservoir3 : register(u24);
RWTexture2D<float4> tex_reservoir4 : register(u25);

// blue noise offsets for spatial sampling (stratified spiral pattern)
static const float2 SPATIAL_OFFSETS[16] = {
    float2(-0.9423, -0.3993), float2( 0.9457,  0.2908),
    float2(-0.0675, -0.9920), float2(-0.8116,  0.5841),
    float2( 0.3890, -0.9213), float2( 0.7854,  0.6189),
    float2(-0.3066,  0.9518), float2( 0.5576, -0.8302),
    float2(-0.8765,  0.0183), float2( 0.1105,  0.9939),
    float2( 0.9824, -0.1869), float2(-0.4680, -0.8837),
    float2(-0.6205,  0.7841), float2( 0.2673, -0.9636),
    float2( 0.8420,  0.5394), float2(-0.9987,  0.0502)
};

// check if neighbor is suitable for spatial resampling
bool is_neighbor_valid(int2 neighbor_pixel, float3 center_pos, float3 center_normal, float center_depth, float2 resolution)
{
    // bounds check
    if (neighbor_pixel.x < 0 || neighbor_pixel.x >= (int)resolution.x ||
        neighbor_pixel.y < 0 || neighbor_pixel.y >= (int)resolution.y)
        return false;
    
    float2 neighbor_uv = (neighbor_pixel + 0.5f) / resolution;
    
    // depth check
    float neighbor_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), neighbor_uv, 0).r;
    if (neighbor_depth <= 0.0f)
        return false;
    
    // relative depth check
    float depth_ratio = center_depth / max(neighbor_depth, 1e-6f);
    if (abs(depth_ratio - 1.0f) > RESTIR_DEPTH_THRESHOLD)
        return false;
    
    // normal similarity check
    float3 neighbor_normal = get_normal(neighbor_uv);
    if (dot(center_normal, neighbor_normal) < RESTIR_NORMAL_THRESHOLD)
        return false;
    
    return true;
}

// evaluate target pdf for neighbor's sample at center shading point
float evaluate_target_pdf_spatial(PathSample sample, float3 center_pos, float3 center_normal)
{
    // for unbiased restir, we'd need to check visibility from center to sample's path
    // simplified version uses luminance-based target
    // todo: implement proper visibility retracing for full correctness
    
    // basic heuristic: reduce weight for samples whose hit normal faces away
    float alignment = saturate(dot(center_normal, sample.hit_normal) * 0.5f + 0.5f);
    return calculate_target_pdf(sample.radiance) * alignment;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    
    // get resolution
    float2 resolution = buffer_frame.resolution_render;
    if (pixel.x >= (uint)resolution.x || pixel.y >= (uint)resolution.y)
        return;
    
    float2 uv = (pixel + 0.5f) / resolution;
    
    // check if there's geometry
    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
        return;
    
    // get current surface data
    float3 pos_ws    = get_position(uv);
    float3 normal_ws = get_normal(uv);
    
    // load center reservoir
    Reservoir center = unpack_reservoir(
        tex_reservoir_in0[pixel],
        tex_reservoir_in1[pixel],
        tex_reservoir_in2[pixel],
        tex_reservoir_in3[pixel],
        tex_reservoir_in4[pixel]
    );
    
    // initialize rng
    uint seed = create_seed(pixel, buffer_frame.frame + 1000); // different seed than temporal
    
    // create combined reservoir starting with center
    Reservoir combined = create_empty_reservoir();
    
    // add center sample
    float target_pdf_center = calculate_target_pdf(center.sample.radiance);
    float weight_center = target_pdf_center * center.W * center.M;
    combined.weight_sum = weight_center;
    combined.M = center.M;
    combined.sample = center.sample;
    combined.target_pdf = target_pdf_center;
    
    // track validity for bias correction
    uint valid_neighbor_count = 0;
    float z_values[RESTIR_SPATIAL_SAMPLES + 1]; // for mis
    z_values[0] = center.M;
    
    // rotate spatial pattern by random angle for each pixel/frame
    float rotation_angle = random_float(seed) * 2.0f * PI;
    float cos_rot = cos(rotation_angle);
    float sin_rot = sin(rotation_angle);
    
    // sample spatial neighbors
    for (uint i = 0; i < RESTIR_SPATIAL_SAMPLES; i++)
    {
        // get offset from pattern and rotate
        float2 offset = SPATIAL_OFFSETS[i % 16];
        float2 rotated_offset = float2(
            offset.x * cos_rot - offset.y * sin_rot,
            offset.x * sin_rot + offset.y * cos_rot
        );
        
        // scale by radius with random jitter
        float radius_scale = RESTIR_SPATIAL_RADIUS * (0.5f + 0.5f * random_float(seed));
        int2 neighbor_pixel = int2(pixel) + int2(rotated_offset * radius_scale);
        
        // validate neighbor
        if (!is_neighbor_valid(neighbor_pixel, pos_ws, normal_ws, depth, resolution))
        {
            z_values[i + 1] = 0;
            continue;
        }
        
        // load neighbor reservoir
        Reservoir neighbor = unpack_reservoir(
            tex_reservoir_in0[neighbor_pixel],
            tex_reservoir_in1[neighbor_pixel],
            tex_reservoir_in2[neighbor_pixel],
            tex_reservoir_in3[neighbor_pixel],
            tex_reservoir_in4[neighbor_pixel]
        );
        
        // skip empty reservoirs
        if (neighbor.M <= 0 || neighbor.W <= 0)
        {
            z_values[i + 1] = 0;
            continue;
        }
        
        valid_neighbor_count++;
        z_values[i + 1] = neighbor.M;
        
        // evaluate target pdf at center position
        float target_pdf_neighbor = evaluate_target_pdf_spatial(neighbor.sample, pos_ws, normal_ws);
        
        // merge with ris
        float rand = random_float(seed);
        if (merge_reservoir(combined, neighbor, target_pdf_neighbor, rand))
        {
            combined.target_pdf = target_pdf_neighbor;
        }
    }
    
    // calculate unbiased weight using generalized balance heuristic
    // W = (1 / Z) * (1 / p_hat) * sum(w_i)
    // where Z is the effective number of samples that could have produced this sample
    
    float Z = 0;
    for (uint j = 0; j <= RESTIR_SPATIAL_SAMPLES; j++)
    {
        Z += z_values[j];
    }
    
    // clamp M based on actual valid samples
    combined.M = min(combined.M, Z);
    clamp_reservoir_M(combined, RESTIR_M_CAP * 2); // allow slightly higher M for spatial
    
    // finalize weight
    if (combined.target_pdf > 0 && combined.M > 0)
    {
        // bias correction: divide by number of valid neighbors + 1 (center)
        float bias_correction = 1.0f / max(float(valid_neighbor_count + 1), 1.0f);
        combined.W = (combined.weight_sum / (combined.target_pdf * combined.M)) * bias_correction;
    }
    else
    {
        combined.W = 0;
    }
    
    // write final reservoir
    float4 t0, t1, t2, t3, t4;
    pack_reservoir(combined, t0, t1, t2, t3, t4);
    tex_reservoir0[pixel] = t0;
    tex_reservoir1[pixel] = t1;
    tex_reservoir2[pixel] = t2;
    tex_reservoir3[pixel] = t3;
    tex_reservoir4[pixel] = t4;
    
    // output final spatially resampled gi
    float3 gi = combined.sample.radiance * combined.W;
    
    // firefly suppression
    float lum = luminance(gi);
    if (lum > 8.0f)
        gi *= 8.0f / lum;
    
    // boost for visibility (match original gi intensity)
    gi *= 2.0f;
    
    tex_uav[pixel] = float4(gi, 1.0f);
}
