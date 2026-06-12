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
static const float SPATIAL_RADIUS_MAX  = 24.0f;
static const float SPATIAL_DEPTH_SCALE = 0.5f;

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

// g-buffer similarity gate used before attempting a shift on a neighbor's reservoir
bool is_neighbor_gbuffer_compatible(
    int2 neighbor_pixel,
    float3 center_pos,
    float3 center_normal,
    float center_linear_depth,
    float2 resolution)
{
    if (neighbor_pixel.x < 0 || neighbor_pixel.x >= (int)resolution.x ||
        neighbor_pixel.y < 0 || neighbor_pixel.y >= (int)resolution.y)
        return false;

    float2 neighbor_uv   = (neighbor_pixel + 0.5f) / resolution;
    float neighbor_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), neighbor_uv, 0).r;

    if (neighbor_depth <= 0.0f)
        return false;

    float neighbor_linear_depth = linearize_depth(neighbor_depth);

    float adaptive_depth_threshold = lerp(RESTIR_DEPTH_THRESHOLD, RESTIR_DEPTH_THRESHOLD * 2.0f,
                                           saturate(center_linear_depth / 200.0f));

    float depth_ratio = center_linear_depth / max(neighbor_linear_depth, 1e-6f);
    if (abs(depth_ratio - 1.0f) > adaptive_depth_threshold)
        return false;

    float3 neighbor_normal = get_normal(neighbor_uv);
    float normal_similarity = dot(center_normal, neighbor_normal);

    float adaptive_normal_threshold = lerp(RESTIR_NORMAL_THRESHOLD, 0.98f,
                                            saturate(center_linear_depth / 150.0f));
    if (normal_similarity < adaptive_normal_threshold)
        return false;

    // world distance gate scaled with depth, a 5cm floor, rejects crevices regardless of camera distance
    float3 neighbor_pos  = get_position(neighbor_uv);
    float  world_dist    = length(neighbor_pos - center_pos);
    float  max_world_dist = max(center_linear_depth * 0.02f, 0.05f);
    if (world_dist > max_world_dist)
        return false;

    return true;
}

float compute_edge_factor(float2 uv, float linear_depth, float2 screen_resolution)
{
    float2 texel     = 1.0f / screen_resolution;
    float depth_left  = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(-texel.x, 0), 0).r);
    float depth_right = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(texel.x, 0), 0).r);
    float depth_up    = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(0, -texel.y), 0).r);
    float depth_down  = linearize_depth(tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv + float2(0, texel.y), 0).r);

    float gradient = abs(depth_left - depth_right) + abs(depth_up - depth_down);
    float relative_gradient = gradient / max(linear_depth, 0.01f);

    return saturate(1.0f - relative_gradient * 5.0f);
}

float compute_adaptive_radius(float linear_depth, float center_roughness, float edge_factor, float3 normal_ws, float3 view_dir)
{
    float depth_factor     = saturate(sqrt(linear_depth * SPATIAL_DEPTH_SCALE / 100.0f));
    float base_radius      = lerp(SPATIAL_RADIUS_MIN, SPATIAL_RADIUS_MAX, depth_factor);
    float roughness_factor = lerp(0.7f, 1.0f, center_roughness);

    float n_dot_v = abs(dot(normal_ws, view_dir));
    float grazing_factor = lerp(0.3f, 1.0f, saturate(n_dot_v * 2.0f));

    float distance_reduction = lerp(1.0f, 0.5f, saturate((linear_depth - 50.0f) / 100.0f));

    return base_radius * roughness_factor * edge_factor * grazing_factor * distance_reduction;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 dispatch_id : SV_DispatchThreadID)
{
    uint2 pixel = dispatch_id.xy;
    uint resolution_x, resolution_y;
    tex_uav.GetDimensions(resolution_x, resolution_y);
    float2 resolution = float2(resolution_x, resolution_y);

    if (pixel.x >= resolution_x || pixel.y >= resolution_y)
        return;

    float2 uv = (pixel + 0.5f) / resolution;

    float depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).r;
    if (depth <= 0.0f)
        return;

    float linear_depth = linearize_depth(depth);
    float3 pos_ws      = get_position(uv);
    float3 normal_ws   = get_normal(uv);
    float3 view_dir    = normalize(get_camera_position() - pos_ws);
    float4 material    = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float3 albedo      = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0).rgb);
    float  roughness   = max(material.r, 0.04f);
    float  metallic    = material.g;

    Reservoir center = unpack_reservoir(
        tex_reservoir_prev0[pixel],
        tex_reservoir_prev1[pixel],
        tex_reservoir_prev2[pixel],
        tex_reservoir_prev3[pixel],
        tex_reservoir_prev4[pixel],
        tex_reservoir_prev5[pixel]
    );

    if (!is_reservoir_valid(center))
        center = create_empty_reservoir();

    uint spatial_pass_index = (uint)pass_get_f3_value().x;
    uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 2 + spatial_pass_index);
    float center_confidence = saturate(center.confidence);

    float edge_factor     = compute_edge_factor(uv, linear_depth, buffer_frame.resolution_render);
    float adaptive_radius = compute_adaptive_radius(linear_depth, roughness, edge_factor, normal_ws, view_dir);

    // a-trous expanding spatial reuse, lin 2022 6.2, the radius grows by 1.6^pass so later
    // passes feed neighbors at larger distances into the canonical reservoir
    uint spatial_sample_count = RESTIR_SPATIAL_SAMPLES;
    if (spatial_pass_index > 0)
    {
        adaptive_radius     *= pow(1.6f, float(spatial_pass_index));
        spatial_sample_count = max(RESTIR_SPATIAL_SAMPLES - 2u, 4u);
    }

    // own domain target from the previous pass finalize at this same pixel, re-evaluating
    // would mismatch center.W for replay carried samples since W = weight_sum / stored target
    float target_cur = max(center.target_pdf, 0.0f);

    // defensive pairwise mis, bitterli 2020, lin 2022 algorithm 3
    // each pair uses both shift directions, a defensive canonical share keeps it stable when neighbors are bad
    Reservoir combined  = create_empty_reservoir();
    combined.sample     = center.sample;
    combined.target_pdf = target_cur;

    float center_M = max(center.M, 0.0f);

    float canonical_pair_acc = 0.0f;
    uint  valid_neighbors    = 0;

    // collected per neighbor so the canonical defensive share is known before the streaming insert
    PathSample stream_samples [RESTIR_SPATIAL_SAMPLES];
    float      stream_target  [RESTIR_SPATIAL_SAMPLES];
    float      stream_jacobian[RESTIR_SPATIAL_SAMPLES];
    float      stream_W       [RESTIR_SPATIAL_SAMPLES];
    float      stream_share   [RESTIR_SPATIAL_SAMPLES];

    float confidence_acc = center_M * center_confidence;
    float confidence_w   = center_M;
    float M_total        = center_M;

    float base_angle = random_float(seed) * 2.0f * PI;

    for (uint i = 0; i < spatial_sample_count; i++)
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

        if (!is_neighbor_gbuffer_compatible(neighbor_pixel, pos_ws, normal_ws, linear_depth, resolution))
            continue;

        Reservoir neighbor = unpack_reservoir(
            tex_reservoir_prev0[neighbor_pixel],
            tex_reservoir_prev1[neighbor_pixel],
            tex_reservoir_prev2[neighbor_pixel],
            tex_reservoir_prev3[neighbor_pixel],
            tex_reservoir_prev4[neighbor_pixel],
            tex_reservoir_prev5[neighbor_pixel]
        );

        if (!is_reservoir_valid(neighbor) || neighbor.M <= 0.0f || neighbor.W <= 0.0f)
            continue;

        // confidence feeds the m-weighted merge but does not gate participation
        float neighbor_confidence = saturate(neighbor.confidence);

        float2 neighbor_uv     = (neighbor_pixel + 0.5f) / resolution;
        float3 neighbor_pos_ws = get_position(neighbor_uv);

        // both the forward and backward shift need the neighbor brdf parameters
        float4 neighbor_material  = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), neighbor_uv, 0);
        float3 neighbor_albedo    = saturate(tex_albedo.SampleLevel(GET_SAMPLER(sampler_point_clamp), neighbor_uv, 0).rgb);
        float  neighbor_roughness = max(neighbor_material.r, 0.04f);
        float  neighbor_metallic  = neighbor_material.g;
        float3 neighbor_normal_ws = get_normal(neighbor_uv);
        float3 neighbor_view_dir  = normalize(get_camera_position() - neighbor_pos_ws);

        // forward shift, neighbor path evaluated at this pixel
        ShiftResult shift_j_to_c = try_hybrid_shift(
            neighbor.sample,
            neighbor_pos_ws,
            neighbor_normal_ws,
            neighbor_view_dir,
            neighbor_albedo,
            neighbor_roughness,
            neighbor_metallic,
            pos_ws,
            normal_ws,
            view_dir,
            albedo,
            roughness,
            metallic
        );

        if (!shift_j_to_c.ok)
            continue;

        if (!trace_shift_visibility(neighbor.sample, pos_ws, normal_ws))
            continue;

        float target_j_at_c = target_scalar(shift_j_to_c.f_dst);
        if (target_j_at_c <= 0.0f)
            continue;

        // backward shift, canonical sample evaluated at the neighbor pixel, for pairwise mis
        ShiftResult shift_c_to_j = try_hybrid_shift(
            center.sample,
            pos_ws,
            normal_ws,
            view_dir,
            albedo,
            roughness,
            metallic,
            neighbor_pos_ws,
            neighbor_normal_ws,
            neighbor_view_dir,
            neighbor_albedo,
            neighbor_roughness,
            neighbor_metallic
        );

        // if the canonical does not shift to the neighbor, the neighbor technique cannot
        // produce the canonical sample and that pair share collapses to full canonical weight
        float target_c_at_j   = shift_c_to_j.ok ? target_scalar(shift_c_to_j.f_dst) : 0.0f;
        float jacobian_c_to_j = shift_c_to_j.ok ? shift_c_to_j.jacobian             : 0.0f;
        float jacobian_j_to_c = shift_j_to_c.jacobian;

        // pairwise mis shares, lin 2022 5.2, each share evaluates one sample under both
        // techniques, own domain target in the numerator, shifted target times the shift
        // jacobian in the other denominator term

        // canonical sample x_c under canonical and neighbor densities
        float canon_denom = center_M * target_cur + neighbor.M * target_c_at_j * jacobian_c_to_j;
        canonical_pair_acc += (canon_denom > 0.0f) ? (center_M * target_cur) / canon_denom : 1.0f;

        // neighbor sample x_j under neighbor and canonical densities, own domain target is
        // stored in the neighbor reservoir from its own finalize
        float target_j_own = max(neighbor.target_pdf, 0.0f);
        float neigh_denom  = neighbor.M * target_j_own + center_M * target_j_at_c * jacobian_j_to_c;
        float neigh_share  = (neigh_denom > 0.0f) ? (neighbor.M * target_j_own) / neigh_denom : 0.0f;

        stream_samples [valid_neighbors] = neighbor.sample;
        stream_target  [valid_neighbors] = target_j_at_c;
        stream_jacobian[valid_neighbors] = jacobian_j_to_c;
        stream_W       [valid_neighbors] = neighbor.W;
        stream_share   [valid_neighbors] = neigh_share;
        valid_neighbors++;

        M_total        += neighbor.M;
        confidence_acc += neighbor.M * neighbor_confidence;
        confidence_w   += neighbor.M;

        if (valid_neighbors >= RESTIR_SPATIAL_SAMPLES)
            break;
    }

    // defensive pairwise mis weights, k == 0 gives the canonical the full weight
    float k_inv = 1.0f / float(valid_neighbors + 1);

    float weight_c = k_inv * (1.0f + canonical_pair_acc) * target_cur * center.W;
    combined.weight_sum = max(weight_c, 0.0f);

    // gris streaming weights, w_j = m_j * p_hat * W_j * jacobian, jacobian appears once here
    for (uint j = 0; j < valid_neighbors; j++)
    {
        float target_at_c = stream_target[j];
        float m_j         = k_inv * stream_share[j];
        float weight_j    = max(m_j * target_at_c * stream_W[j] * stream_jacobian[j], 0.0f);

        combined.weight_sum += weight_j;

        if (combined.weight_sum > 0.0f && random_float(seed) * combined.weight_sum < weight_j)
        {
            combined.sample     = stream_samples[j];
            combined.target_pdf = target_at_c;
        }
    }

    combined.M = M_total;
    clamp_reservoir_M(combined, get_restir_m_cap());

    // lin 2022 6.4 sample validation, kills stale paths that survive purely through spatial reuse
    uint validation_period = get_restir_validation_period();
    if (validation_period > 0u && combined.M > 0.0f && combined.W > 0.0f)
    {
        uint hash = (pixel.x * 73856093u) ^ (pixel.y * 19349663u);
        uint slot = (buffer_frame.frame + hash + spatial_pass_index * 11u) % validation_period;
        if (slot == 0u)
        {
            bool reachable = trace_shift_visibility(combined.sample, pos_ws, normal_ws);
            if (!reachable)
            {
                combined            = create_empty_reservoir();
                combined.sample     = center.sample;
                combined.target_pdf = target_cur;
                combined.weight_sum = 0.0f;
                combined.M          = 0.0f;
                combined.W          = 0.0f;
            }
        }
    }

    // finalize, W = weight_sum / target, no /M since the m_i factors are already normalized
    // the selection time target is reused instead of a re-evaluation, for replay shifted
    // samples the two evaluation paths differ slightly and a mismatched divide skews W
    combined.W = (combined.target_pdf > 0.0f) ? (combined.weight_sum / combined.target_pdf) : 0.0f;

    // soft saturator, see soft_clamp_w in restir_reservoir.hlsl
    float w_clamp = get_w_clamp_for_sample(combined.sample);
    combined.W    = soft_clamp_w(combined.W, w_clamp);

    // m-weighted confidence across merged streams, blended with the center
    float merged_confidence = (confidence_w > 0.0f) ? (confidence_acc / confidence_w) : center_confidence;
    combined.confidence = saturate(max(center_confidence, merged_confidence));
    combined.age        = center.age;

    // re-stamp the source primary g-buffer, the combine may have copied a neighbor src_*
    combined.sample.src_pos       = pos_ws;
    combined.sample.src_normal    = normal_ws;
    combined.sample.src_albedo    = albedo;
    combined.sample.src_roughness = roughness;
    combined.sample.src_metallic  = metallic;

    float4 t0, t1, t2, t3, t4, t5;
    pack_reservoir(combined, t0, t1, t2, t3, t4, t5);
    tex_reservoir0[pixel] = t0;
    tex_reservoir1[pixel] = t1;
    tex_reservoir2[pixel] = t2;
    tex_reservoir3[pixel] = t3;
    tex_reservoir4[pixel] = t4;
    tex_reservoir5[pixel] = t5;

    float3 gi = shade_reservoir_path(combined, pos_ws, normal_ws, view_dir, albedo, roughness, metallic);
    if (any(isnan(gi)) || any(isinf(gi)))
        gi = float3(0, 0, 0);

    // direct lighting lives inside the reservoir via the initial ris nee samples, adding it here would double count
    tex_uav[pixel] = float4(gi, saturate(combined.confidence));
}
