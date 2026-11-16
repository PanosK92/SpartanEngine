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

//= INCLUDES =========
#include "common.hlsl"
//====================

static const float fill_depth_edge      = 0.1f;  // relative depth spread of the four neighbours that counts as an edge
static const float fill_normal_power    = 8.0f;  // sharpness of the normal weight
static const float fill_weight_epsilon  = 1e-4f;
static const float fill_static_pixels   = 0.25f; // screen motion below which the previous frame's value is reused

static const int2 fill_offsets[4] =
{
    int2(-1,  0),
    int2( 1,  0),
    int2( 0, -1),
    int2( 0,  1)
};

// the weighted mean of the visibility, and the blocker data of the strongest neighbour that has a blocker,
// a static pixel keeps its previous visibility, clamped to the range of the neighbours
float4 fill_signal(float4 samples[4], float4 weights, float4 previous, bool is_static)
{
    float visibility    = 0.0f;
    float best_weight   = -1.0f;
    float best_blocked  = -1.0f;
    float4 best         = samples[0];
    float4 best_blocker = samples[0];
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        visibility += samples[i].r * weights[i];
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

    if (is_static)
    {
        float visibility_min = min(min(samples[0].r, samples[1].r), min(samples[2].r, samples[3].r));
        float visibility_max = max(max(samples[0].r, samples[1].r), max(samples[2].r, samples[3].r));
        visibility           = clamp(previous.r, visibility_min, visibility_max);
    }

    float4 result = best_blocked >= 0.0f ? best_blocker : best;
    result.r      = visibility;
    return result;
}

[numthreads(8, 8, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);

    // one thread per skipped pixel, the other pixel of the horizontal pair the trace took
    uint2 pixel = reflections_checkerboard_pixel(thread_id.xy);
    pixel.x    ^= 1u; // flip lowest bit to get the other pixel of the pair, the one that was skipped by the trace
    if (any(pixel >= resolution))
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
        tex_uav[pixel] = float4(1.0f, 0.0f, 0.0f, 1.0f);
        for (uint slice = 0; slice < slices; slice++)
        {
            tex_uav_rt_shadows_local[uint3(pixel, slice)] = float4(1.0f, 0.0f, 1.0f, 1.0f);
        }
        return;
    }

    float3 normal = get_normal(pixel);

    // velocity is in ndc, half of it times the resolution is the motion in pixels
    float2 motion    = tex_velocity[pixel].xy * 0.5f * float2(resolution);
    bool   is_static = dot(motion, motion) < fill_static_pixels * fill_static_pixels;

    // neighbours outside the screen take the one across, they are traced pixels too
    uint2  neighbours[4];
    float4 depths;
    float4 normal_weights;
    [unroll]
    for (uint i = 0; i < 4; i++)
    {
        int2 neighbour = int2(pixel) + fill_offsets[i];
        if (neighbour.x < 0 || neighbour.x >= (int)resolution.x || neighbour.y < 0 || neighbour.y >= (int)resolution.y)
        {
            neighbour = int2(pixel) - fill_offsets[i];
        }

        neighbours[i]     = uint2(clamp(neighbour, int2(0, 0), int2(resolution) - 1));
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
    tex_uav[pixel] = fill_signal(samples, weights, tex_uav[pixel], is_static);

    for (uint slice = 0; slice < slices; slice++)
    {
        // a slot without a light was cleared by the trace on every pixel
        if (pass_uint(pass_ray_traced_shadows::local_shadow_lights + slice) == 0u)
        {
            continue;
        }

        [unroll]
        for (uint i = 0; i < 4; i++)
        {
            samples[i] = tex_uav_rt_shadows_local[uint3(neighbours[i], slice)];
        }
        float4 previous = tex_uav_rt_shadows_local[uint3(pixel, slice)];
        tex_uav_rt_shadows_local[uint3(pixel, slice)] = fill_signal(samples, weights, previous, is_static);
    }
}
