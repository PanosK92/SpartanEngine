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

// adaptive radius parameters
static const float SPATIAL_RADIUS_MIN = 5.0f;
static const float SPATIAL_RADIUS_MAX = 30.0f;
static const float SPATIAL_DEPTH_SCALE = 0.5f; // how much depth affects radius

/*------------------------------------------------------------------------------
    RESOURCES
------------------------------------------------------------------------------*/
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
    float2(-0.9423, -0.3993), float2( 0.9457,  0.2908),
    float2(-0.0675, -0.9920), float2(-0.8116,  0.5841),
    float2( 0.3890, -0.9213), float2( 0.7854,  0.6189),
    float2(-0.3066,  0.9518), float2( 0.5576, -0.8302),
    float2(-0.8765,  0.0183), float2( 0.1105,  0.9939),
    float2( 0.9824, -0.1869), float2(-0.4680, -0.8837),
    float2(-0.6205,  0.7841), float2( 0.2673, -0.9636),
    float2( 0.8420,  0.5394), float2(-0.9987,  0.0502)
};

/*------------------------------------------------------------------------------
    VISIBILITY
------------------------------------------------------------------------------*/
bool check_spatial_visibility(float3 center_pos, float3 center_normal, float3 sample_hit_pos)
{
    float3 dir  = sample_hit_pos - center_pos;
    float dist  = length(dir);

    // skip very short distances
    if (dist < 0.03f)
        return true;

    dir /= dist;

    if (dot(dir, center_normal) <= 0.0f)
        return false;

    RayDesc ray;
    ray.Origin    = center_pos + center_normal * RESTIR_RAY_NORMAL_OFFSET;
    ray.Direction = dir;
    ray.TMin      = RESTIR_RAY_T_MIN;
    ray.TMax      = dist - 0.02f;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER> query;
    query.TraceRayInline(tlas, RAY_FLAG_NONE, 0xFF, ray);
    query.Proceed();

    return query.CommittedStatus() == COMMITTED_NOTHING;
}

/*------------------------------------------------------------------------------
    NEIGHBOR VALIDATION
------------------------------------------------------------------------------*/
bool is_neighbor_valid(int2 neighbor_pixel, float3 center_pos, float3 center_normal, float center_linear_depth, float2 resolution)
{
    if (neighbor_pixel.x < 0 || neighbor_pixel.x >= (int)resolution.x ||
        neighbor_pixel.y < 0 || neighbor_pixel.y >= (int)resolution.y)
        return false;

    float2 neighbor_uv   = (neighbor_pixel + 0.5f) / resolution;
    float neighbor_depth = tex_depth.SampleLevel(GET_SAMPLER(sampler_point_clamp), neighbor_uv, 0).r;

    if (neighbor_depth <= 0.0f)
        return false;

    // use linearized depth for perceptually consistent threshold
    float neighbor_linear_depth = linearize_depth(neighbor_depth);
    float depth_ratio = center_linear_depth / max(neighbor_linear_depth, 1e-6f);
    if (abs(depth_ratio - 1.0f) > RESTIR_DEPTH_THRESHOLD)
        return false;

    float3 neighbor_normal = get_normal(neighbor_uv);
    if (dot(center_normal, neighbor_normal) < RESTIR_NORMAL_THRESHOLD)
        return false;

    return true;
}

/*------------------------------------------------------------------------------
    UNBIASED SPATIAL RESAMPLING WITH 1/Z MIS

    This implements unbiased spatial reuse using 1/Z normalization.
    For each sample, the MIS weight is: p(x) / sum_i(p_i(x))
    where p_i(x) is the target PDF evaluated at pixel i.
    This ensures unbiased results without double-counting M values.
------------------------------------------------------------------------------*/
struct SpatialCandidate
{
    Reservoir reservoir;
    float3    shading_pos;
    float     target_pdf_at_center;
    float     jacobian;
    bool      visible;
    bool      valid;
};

float evaluate_target_pdf_spatial(PathSample sample, float3 center_pos, float3 center_normal, float3 neighbor_pos, out float jacobian)
{
    jacobian = 1.0f;

    // reject invalid samples
    if (all(sample.radiance <= 0.0f))
        return 0.0f;

    float3 dir_to_sample = sample.hit_position - center_pos;
    float dist_sq        = dot(dir_to_sample, dir_to_sample);
    if (dist_sq < 1e-6f)
        return 0.0f;

    dir_to_sample     = dir_to_sample * rsqrt(dist_sq);
    float cos_theta   = dot(center_normal, dir_to_sample);

    if (cos_theta <= 0.0f)
        return 0.0f;

    // compute solid angle jacobian for measure conversion
    jacobian = compute_jacobian(sample.hit_position, neighbor_pos, center_pos, sample.hit_normal);
    if (jacobian <= 0.0f)
        return 0.0f;

    // target PDF stays consistent - jacobian is applied to weight separately
    return calculate_target_pdf(sample.radiance);
}

float compute_adaptive_radius(float linear_depth, float center_roughness)
{
    // larger radius for distant surfaces (less screen-space detail)
    // smaller radius for nearby surfaces (preserve detail)
    float depth_factor = saturate(linear_depth * SPATIAL_DEPTH_SCALE / 100.0f);

    // interpolate between min and max radius based on depth
    float base_radius = lerp(SPATIAL_RADIUS_MIN, SPATIAL_RADIUS_MAX, depth_factor);

    // rougher surfaces can tolerate larger radius
    float roughness_factor = lerp(0.7f, 1.0f, center_roughness);

    return base_radius * roughness_factor;
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

    float linear_depth = linearize_depth(depth);
    float3 pos_ws      = get_position(uv);
    float3 normal_ws   = get_normal(uv);

    // get roughness for adaptive radius
    float4 material = tex_material.SampleLevel(GET_SAMPLER(sampler_point_clamp), uv, 0);
    float roughness = max(material.r, 0.04f);

    // load center pixel reservoir
    Reservoir center = unpack_reservoir(
        tex_reservoir_in0[pixel],
        tex_reservoir_in1[pixel],
        tex_reservoir_in2[pixel],
        tex_reservoir_in3[pixel],
        tex_reservoir_in4[pixel]
    );

    // validate center reservoir
    if (!is_reservoir_valid(center))
        center = create_empty_reservoir();

    uint seed = create_seed_for_pass(pixel, buffer_frame.frame, 2); // pass 2: spatial reuse

    // compute adaptive radius based on depth and roughness
    float adaptive_radius = compute_adaptive_radius(linear_depth, roughness);

    // collect valid candidates first for proper MIS weighting
    SpatialCandidate candidates[RESTIR_SPATIAL_SAMPLES];
    uint valid_count = 0;

    // stratified sampling rotation
    float rotation_angle = random_float(seed) * 2.0f * PI;
    float cos_rot = cos(rotation_angle);
    float sin_rot = sin(rotation_angle);

    // gather candidates
    for (uint i = 0; i < RESTIR_SPATIAL_SAMPLES; i++)
    {
        candidates[i].valid = false;

        float2 offset = SPATIAL_OFFSETS[i % 16];
        float2 rotated_offset = float2(
            offset.x * cos_rot - offset.y * sin_rot,
            offset.x * sin_rot + offset.y * cos_rot
        );

        float radius_scale  = adaptive_radius * (0.5f + 0.5f * random_float(seed));
        int2 neighbor_pixel = int2(pixel) + int2(rotated_offset * radius_scale);

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

        // validate neighbor reservoir
        if (!is_reservoir_valid(neighbor))
            continue;

        if (neighbor.M <= 0 || neighbor.W <= 0)
            continue;

        if (neighbor.sample.path_length == 0 || all(neighbor.sample.radiance <= 0.0f))
            continue;

        // check visibility
        bool visible = check_spatial_visibility(pos_ws, normal_ws, neighbor.sample.hit_position);
        if (!visible)
            continue;

        float neighbor_jacobian;
        float target_pdf_neighbor = evaluate_target_pdf_spatial(
            neighbor.sample, pos_ws, normal_ws, neighbor_pos_ws, neighbor_jacobian);

        if (target_pdf_neighbor <= 0.0f || neighbor_jacobian <= 0.0f)
            continue;

        candidates[i].reservoir           = neighbor;
        candidates[i].shading_pos         = neighbor_pos_ws;
        candidates[i].target_pdf_at_center = target_pdf_neighbor;
        candidates[i].jacobian            = neighbor_jacobian;
        candidates[i].visible             = true;
        candidates[i].valid               = true;
        valid_count++;
    }

    // initialize combined reservoir with center sample
    // start with M=1 to avoid double-counting when merging
    Reservoir combined = create_empty_reservoir();

    float target_pdf_center = calculate_target_pdf(center.sample.radiance);

    // compute MIS weight for center sample
    // center's MIS weight considers all valid neighbors using 1/Z normalization
    float center_mis_weight = 1.0f;
    if (valid_count > 0 && target_pdf_center > 0.0f)
    {
        // 1/Z MIS: denominator is sum of target PDFs at all pixels where sample is valid
        float denominator = target_pdf_center; // center contribution
        for (uint i = 0; i < RESTIR_SPATIAL_SAMPLES; i++)
        {
            if (candidates[i].valid)
            {
                // add neighbor's target PDF contribution
                denominator += candidates[i].target_pdf_at_center;
            }
        }
        center_mis_weight = target_pdf_center / max(denominator, 1e-6f);
    }

    // add center sample with MIS weight (don't multiply by M)
    float weight_center = target_pdf_center * center.W * center_mis_weight;
    combined.weight_sum = weight_center;
    combined.M          = 1.0f; // start with 1, not center.M
    combined.sample     = center.sample;
    combined.target_pdf = target_pdf_center;

    // add neighbor samples with 1/Z MIS
    for (uint i = 0; i < RESTIR_SPATIAL_SAMPLES; i++)
    {
        if (!candidates[i].valid)
            continue;

        Reservoir neighbor = candidates[i].reservoir;

        // compute 1/Z MIS weight for this neighbor
        // denominator is sum of target PDFs at center and all valid neighbors
        float mis_denominator = target_pdf_center; // center can also generate this sample
        for (uint j = 0; j < RESTIR_SPATIAL_SAMPLES; j++)
        {
            if (candidates[j].valid)
                mis_denominator += candidates[j].target_pdf_at_center;
        }
        float mis_weight = candidates[i].target_pdf_at_center / max(mis_denominator, 1e-6f);

        // apply jacobian and MIS weight to the contribution (don't multiply by M)
        float weight = candidates[i].target_pdf_at_center * neighbor.W *
                      candidates[i].jacobian * mis_weight;

        combined.weight_sum += weight;
        combined.M += 1.0f; // increment by 1, not by neighbor.M

        if (random_float(seed) * combined.weight_sum < weight)
        {
            combined.sample     = neighbor.sample;
            combined.target_pdf = candidates[i].target_pdf_at_center;
        }
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

    // output weighted radiance
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
