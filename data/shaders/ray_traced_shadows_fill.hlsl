/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// nrd sigma has no checkerboard mode (unlike reblur and relax), so the pixels a checkerboard shadow trace skipped are
// filled here from their four traced neighbours, before sigma or the lighting read the masks
//
// tex_uav                   = ray traced shadows, sun
// tex_uav_rt_shadows_local  = ray traced shadows, local lights, one slice per sigma slot
// both use the shadow_mask_pack layout, the visibility channels (r, b, a) are linear and get averaged, the blocker
// distance (g) is not, it's taken from one neighbour

//= INCLUDES =========
#include "common.hlsl"
//====================

static const float fill_depth_edge      = 0.1f;  // relative depth spread of the four neighbours that counts as an edge
static const float fill_normal_power    = 8.0f;  // sharpness of the normal weight
static const float fill_weight_epsilon  = 1e-4f;

// Reconstruct both inputs from this trace. SIGMA reprojects its own history
// after packing and overwrites these textures with the filtered result.
float4 fill_signal(float4 samples[4], float4 weights)
{
    float3 visibility   = 0.0f;
    float best_weight   = -1.0f;
    float best_blocked  = -1.0f;
    float4 best         = samples[0];
    float4 best_blocker = samples[0];
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        visibility += samples[i].rba * weights[i];
        if (weights[i] > best_weight)
        {
            best_weight = weights[i];
            best        = samples[i];
        }

        if (samples[i].g > 0.0f && weights[i] > best_blocked)
        {
            best_blocked = weights[i];
            best_blocker = samples[i];
        }
    }

    float4 result = best_blocked >= 0.0f ? best_blocker : best;
    result.rba    = visibility;
    return result;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    uint2 active_resolution = max(uint2(floor(float2(resolution) * buffer_frame.resolution_scale)), uint2(1u, 1u));

    uint2 pixel = thread_id.xy;
    uint2 phase = min(uint2(buffer_frame.frame & 1u, (buffer_frame.frame >> 1u) & 1u), active_resolution - 1u);
    if (any(pixel >= active_resolution) || all((pixel & 1u) == phase))
    {
        return;
    }

    uint layers = 0;
    {
        uint width;
        uint height;
        tex_uav_rt_shadows_local.GetDimensions(width, height, layers);
    }
    uint slices = min(layers, (uint)nrd_local_shadow_max);

    float depth = get_linear_depth(pixel);
    if (tex_depth[pixel].r <= 0.0f)
    {
        tex_uav[pixel] = shadow_mask_pack(1.0f, 0.0f);
        for (uint slice = 0; slice < slices; slice++)
        {
            if (pass_uint(pass_ray_traced_shadows::local_shadow_lights + slice) != 0u)
                tex_uav_rt_shadows_local[uint3(pixel, slice)] = shadow_mask_pack(1.0f, 0.0f);
        }
        return;
    }

    float3 normal = get_normal(pixel);

    // Read only traced texels. Clamp on the sample lattice so odd dimensions
    // and screen edges never read values another fill thread is writing.
    int2 first = int2(phase);
    int2 last = first + 2 * ((int2(active_resolution) - 1 - first) / 2);
    int2 lower = int2(pixel) - int2((pixel ^ phase) & 1u);
    int2 upper = int2(pixel) + int2((pixel ^ phase) & 1u);
    uint2  neighbours[4];
    float4 depths;
    float4 normal_weights;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        int2 neighbour = int2((i & 1u) ? upper.x : lower.x, (i & 2u) ? upper.y : lower.y);
        neighbours[i]     = uint2(clamp(neighbour, first, last));
        depths[i]         = get_linear_depth(neighbours[i]);
        normal_weights[i] = pow(saturate(dot(normal, get_normal(neighbours[i]))), fill_normal_power);
    }

    // the same edge test as a bilateral upsample, a depth weight only where the neighbours disagree
    float depth_min     = min(min(depths.x, depths.y), min(depths.z, depths.w));
    float depth_max     = max(max(depths.x, depths.y), max(depths.z, depths.w));
    float depth_average = dot(depths, 0.25f);
    bool  depth_edge    = (depth_max - depth_min) / (depth_average + fill_weight_epsilon) > fill_depth_edge;

    float4 weights = 0.25f;
    if (depth_edge)
    {
        weights *= 1.0f / (abs(depths - depth) + fill_weight_epsilon);
    }
    weights *= normal_weights;

    // every neighbour is on another surface, the one closest in depth stands in
    float weight_sum = dot(weights, 1.0f);
    if (weight_sum <= fill_weight_epsilon)
    {
        float4 delta = abs(depths - depth);
        float  least = min(min(delta.x, delta.y), min(delta.z, delta.w));
        weights      = float4(delta == least);
        weight_sum   = dot(weights, 1.0f);
    }
    weights /= weight_sum;

    float4 samples[4];
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        samples[i] = tex_uav[neighbours[i]];
    }
    tex_uav[pixel] = fill_signal(samples, weights);

    for (uint slice = 0; slice < slices; slice++)
    {
        // Unassigned slots have no consumers.
        if (pass_uint(pass_ray_traced_shadows::local_shadow_lights + slice) == 0u)
        {
            continue;
        }

        [unroll]
        for (uint i = 0; i < 4; i++)
        {
            samples[i] = tex_uav_rt_shadows_local[uint3(neighbours[i], slice)];
        }
        tex_uav_rt_shadows_local[uint3(pixel, slice)] = fill_signal(samples, weights);
    }
}
