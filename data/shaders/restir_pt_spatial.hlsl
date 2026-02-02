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

static const float SPATIAL_RADIUS_MIN  = 4.0f;
static const float SPATIAL_RADIUS_MAX  = 16.0f;
static const float SPATIAL_DEPTH_SCALE = 0.5f;

Texture2D<float4> tex_reservoir_in0 : register(t21);
Texture2D<float4> tex_reservoir_in1 : register(t22);
Texture2D<float4> tex_reservoir_in2 : register(t23);
Texture2D<float4> tex_reservoir_in3 : register(t24);
Texture2D<float4> tex_reservoir_in4 : register(t25);

RWTexture2D<float4> tex_reservoir0 : register(u21);
RWTexture2D<float4> tex_reservoir1 : register(u22);
RWTexture2D<float4> tex_reservoir2 : register(u23);
RWTexture2D<float4> tex_reservoir3 : register(u24);
RWTexture2D<float4> tex_reservoir4 : register(u25);

static const float2 SPATIAL_OFFSETS[16] = {
    float2(-0.7071, -0.7071), float2( 0.9239,  0.3827),
    float2(-0.3827,  0.9239), float2( 0.7071, -0.7071),
    float2(-0.9239,  0.3827), float2( 0.3827, -0.9239),
    float2( 0.0000,  1.0000), float2(-0.3827, -0.9239),
    float2( 0.9239, -0.3827), float2(-0.7071,  0.7071),
    float2( 0.3827,  0.9239), float2(-0.9239, -0.3827),
    float2( 0.7071,  0.7071), float2(-1.0000,  0.0000),
    float2( 0.0000, -1.0000), float2( 1.0000,  0.0000)
};

bool check_spatial_visibility(float3 center_pos, float3 center_normal, float3 sample_hit_pos, float3 sample_hit_normal)
{
    float3 dir  = sample_hit_pos - center_pos;
    float dist  = length(dir);

    if (dist < RESTIR_VIS_PLANE_MIN)
        return true;

    dir /= dist;

    // reject directions that are too grazing relative to receiver
    float cos_theta = dot(dir, center_normal);
    if (cos_theta <= 0.25f)
        return false;

    float cos_back = dot(sample_hit_normal, -dir);
    if (cos_back <= RESTIR_VIS_COS_BACK)
        return false;

    float plane_dist = dot(sample_hit_pos - center_pos, center_normal);
    if (plane_dist < RESTIR_VIS_PLANE_MIN)
        return false;

    if (dist < RESTIR_VIS_MIN_DIST)
        return true;

    RayDesc ray;
    ray.Origin    = center_pos + center_normal * RESTIR_RAY_NORMAL_OFFSET;
    ray.Direction = dir;
    ray.TMin      = RESTIR_RAY_T_MIN;
    ray.TMax      = dist - RESTIR_RAY_NORMAL_OFFSET;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING;
}

bool is_neighbor_valid(int2 neighbor_pixel, float3 center_pos, float3 center_normal, float center_linear_depth, float2 resolution)
{
    if (neighbor_pixel.x < 0 || neighbor_pixel.x >= (int)resolution.x ||
        neighbor_pixel.y < 0 || neighbor_pixel.y >= (int)resolution.y)
        return false;

    float2 neighbor_uv   = (neighbor_pixel + 0.5f) / resolution;
    float neighbor_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), neighbor_uv, 0).r;

    if (neighbor_depth <= 0.0f)
        return false;

    float neighbor_linear_depth = linearize_depth(neighbor_depth);
    float depth_ratio = center_linear_depth / max(neighbor_linear_depth, 1e-6f);
    if (abs(depth_ratio - 1.0f) > RESTIR_DEPTH_THRESHOLD)
        return false;

    float3 neighbor_normal = get_normal(neighbor_uv);
    if (dot(center_normal, neighbor_normal) < RESTIR_NORMAL_THRESHOLD)
        return false;

    return true;
}

float evaluate_jacobian_for_reuse(PathSample sample, float3 center_pos, float3 center_normal, float3 neighbor_pos)
{
    float3 dir_to_sample = sample.hit_position - center_pos;
    float dist_sq = dot(dir_to_sample, dir_to_sample);
    if (dist_sq < 1e-6f)
        return 0.0f;

    dir_to_sample = dir_to_sample * rsqrt(dist_sq);
    float cos_theta = dot(center_normal, dir_to_sample);
    if (cos_theta <= 0.25f)
        return 0.0f;

    float jacobian = compute_jacobian(sample.hit_position, neighbor_pos, center_pos, sample.hit_normal, center_normal);
    return max(jacobian, 0.0f);
}

float compute_adaptive_radius(float linear_depth, float center_roughness, float edge_factor)
{
    float depth_factor     = saturate(sqrt(linear_depth * SPATIAL_DEPTH_SCALE / 100.0f));
    float base_radius      = lerp(SPATIAL_RADIUS_MIN, SPATIAL_RADIUS_MAX, depth_factor);
    float roughness_factor = lerp(0.7f, 1.0f, center_roughness);
    return base_radius * roughness_factor * edge_factor;
}

float compute_edge_factor(float2 uv, float linear_depth, float2 resolution)
{
    float2 texel     = 1.0f / resolution;
    float depth_left  = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(-texel.x, 0), 0).r);
    float depth_right = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(texel.x, 0), 0).r);
    float depth_up    = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(0, -texel.y), 0).r);
    float depth_down  = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(0, texel.y), 0).r);

    float gradient = abs(depth_left - depth_right) + abs(depth_up - depth_down);
    float relative_gradient = gradient / max(linear_depth, 0.01f);

    return saturate(1.0f - relative_gradient * 5.0f);
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

    float linear_depth = linearize_depth(depth);
    float3 pos_ws      = get_position(uv);
    float3 normal_ws   = get_normal(uv);
    float3 view_dir    = normalize(buffer_frame.camera_position - pos_ws);

    float4 material = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float3 albedo   = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float roughness = max(material.r, 0.04f);
    float metallic  = material.g;

    Reservoir center = unpack_reservoir(
        tex_reservoir_in0[pixel],
        tex_reservoir_in1[pixel],
        tex_reservoir_in2[pixel],
        tex_reservoir_in3[pixel],
        tex_reservoir_in4[pixel]
    );

    if (!is_reservoir_valid(center))
        center = create_empty_reservoir();

    uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 2);

    float edge_factor     = compute_edge_factor(uv, linear_depth, resolution);
    float adaptive_radius = compute_adaptive_radius(linear_depth, roughness, edge_factor);

    Reservoir combined = create_empty_reservoir();

    float target_pdf_center = 0.0f;
    if (any(center.sample.radiance > 0.0f))
    {
        if (is_sky_sample(center.sample))
        {
            target_pdf_center = calculate_target_pdf_sky(
                center.sample.radiance, center.sample.direction, normal_ws, view_dir,
                albedo, roughness, metallic);
        }
        else if (center.sample.path_length > 0)
        {
            target_pdf_center = calculate_target_pdf_with_geometry(
                center.sample.radiance, pos_ws, normal_ws, view_dir,
                center.sample.hit_position, center.sample.hit_normal,
                albedo, roughness, metallic);
        }
    }

    float weight_center = target_pdf_center * center.W * center.M;
    combined.weight_sum = weight_center;
    combined.M          = center.M;
    combined.sample     = center.sample;
    combined.target_pdf = target_pdf_center;

    float base_angle = random_float(seed) * 2.0f * PI;

    for (uint i = 0; i < RESTIR_SPATIAL_SAMPLES; i++)
    {
        float rotation_angle = base_angle + float(i) * 2.39996323f;
        float cos_rot = cos(rotation_angle);
        float sin_rot = sin(rotation_angle);

        float2 offset = SPATIAL_OFFSETS[i % 16];
        float2 rotated_offset = float2(
            offset.x * cos_rot - offset.y * sin_rot,
            offset.x * sin_rot + offset.y * cos_rot
        );

        float radius_jitter = 0.5f + random_float(seed);
        float sample_radius = adaptive_radius * radius_jitter;
        int2 neighbor_pixel = int2(pixel) + int2(rotated_offset * sample_radius);

        if (!is_neighbor_valid(neighbor_pixel, pos_ws, normal_ws, linear_depth, resolution))
            continue;

        float2 neighbor_uv     = (neighbor_pixel + 0.5f) / resolution;
        float3 neighbor_pos_ws = get_position(neighbor_uv);

        Reservoir neighbor = unpack_reservoir(
            tex_reservoir_in0[neighbor_pixel],
            tex_reservoir_in1[neighbor_pixel],
            tex_reservoir_in2[neighbor_pixel],
            tex_reservoir_in3[neighbor_pixel],
            tex_reservoir_in4[neighbor_pixel]
        );

        if (!is_reservoir_valid(neighbor))
            continue;

        if (neighbor.M <= 0 || neighbor.W <= 0)
            continue;

        if (all(neighbor.sample.radiance <= 0.0f))
            continue;

        float target_pdf_at_center = 0.0f;
        float jacobian = 1.0f;

        if (is_sky_sample(neighbor.sample))
        {
            float n_dot_sky = dot(normal_ws, neighbor.sample.direction);
            if (n_dot_sky <= 0.0f)
                continue;

            target_pdf_at_center = calculate_target_pdf_sky(
                neighbor.sample.radiance, neighbor.sample.direction, normal_ws, view_dir,
                albedo, roughness, metallic);

            jacobian = 1.0f;
        }
        else
        {
            if (neighbor.sample.path_length == 0)
                continue;

            bool visible = check_spatial_visibility(pos_ws, normal_ws, neighbor.sample.hit_position, neighbor.sample.hit_normal);
            if (!visible)
                continue;

            jacobian = evaluate_jacobian_for_reuse(neighbor.sample, pos_ws, normal_ws, neighbor_pos_ws);
            if (jacobian <= 0.0f)
                continue;

            target_pdf_at_center = calculate_target_pdf_with_geometry(
                neighbor.sample.radiance, pos_ws, normal_ws, view_dir,
                neighbor.sample.hit_position, neighbor.sample.hit_normal,
                albedo, roughness, metallic);
        }

        if (target_pdf_at_center <= 0.0f)
            continue;

        float effective_M = min(neighbor.M, max(center.M * 2.0f, 4.0f));
        float weight = target_pdf_at_center * neighbor.W * effective_M * jacobian;

        combined.weight_sum += weight;
        combined.M += effective_M;

        if (random_float(seed) * combined.weight_sum < weight)
        {
            combined.sample     = neighbor.sample;
            combined.target_pdf = target_pdf_at_center;
        }
    }

    clamp_reservoir_M(combined, RESTIR_M_CAP);

    if (combined.target_pdf > 0 && combined.M > 0)
        combined.W = combined.weight_sum / (combined.target_pdf * combined.M);
    else
        combined.W = 0;

    float w_clamp = get_w_clamp_for_sample(combined.sample);
    combined.W = min(combined.W, w_clamp);

    float neighbor_contribution = (combined.M > center.M) ? saturate((combined.M - center.M) / combined.M) : 0.0f;
    combined.confidence = lerp(center.confidence, 1.0f, neighbor_contribution * 0.5f);

    float4 t0, t1, t2, t3, t4;
    pack_reservoir(combined, t0, t1, t2, t3, t4);
    tex_reservoir0[pixel] = t0;
    tex_reservoir1[pixel] = t1;
    tex_reservoir2[pixel] = t2;
    tex_reservoir3[pixel] = t3;
    tex_reservoir4[pixel] = t4;

    float3 gi = combined.sample.radiance * combined.W;
    gi = soft_clamp_gi(gi, combined.sample);

    tex_uav[pixel] = float4(gi, 1.0f);
}
