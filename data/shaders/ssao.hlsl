/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// cortao (circular occluder ray traced ambient occlusion), see cortao/LICENSE
// cortao view space is right handed (camera looks down -z), spartan view space looks down +z, so z flips at the callbacks
// main pass -> temporal accumulation (32 frame history) -> cross bilateral denoise
// raw and history targets hold (world-space bent normal offset * visibility, visibility)
#define CORTAO_HLSL                    1
#define CORTAO_PERSPECTIVE_PROJECTION  1
#define CORTAO_PACKED_NORMAL_BITS      10

// the shader preprocessor inlines each file once, so the shared base has to sit outside the variant branches
#include "cortao/CORTAOBase.h"

// constants
static const float g_ao_radius                  = 1.5f;  // world space, metres
static const float g_ao_screen_radius_ratio     = 0.7f;  // max sampling radius as a fraction of the screen height
static const float g_ao_mip_bias                = -4.0f;
static const float g_ao_max_slope_compensation  = 4.0f;
static const float g_ao_backface_threshold      = 0.5f;
static const float g_ao_backface_boost          = 0.5f;
static const float g_history_max_frames         = 32.0f;

// the packed buffer is r32 float, depth sits in the float bits with the normal in the low mantissa bits
float4 cortao_projection_terms()
{
    matrix projection = get_projection();
    return float4(projection[0][0], projection[1][1], projection[2][0], projection[2][1]);
}

uint cortao_frame_seed()
{
    uint state = buffer_frame.frame * 747796405u + 2891336453u;
    uint word  = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    return (word >> 22u) ^ word;
}

uint cortao_packed_resolution()
{
    uint2 resolution = uint2(get_render_resolution_active());
    return resolution.x | (resolution.y << 16u);
}

float3 cortao_load_view_normal(int2 pixel)
{
    float3 normal = get_normal_view_space(uint2(pixel));
    return float3(normal.x, normal.y, -normal.z);
}

// reverse z, d = p22 + p32 / z, sky is kept finite so the bilateral weights stay defined
float ssao_view_depth(float depth_raw)
{
    matrix projection = get_projection();
    return min(abs(projection[3][2] / (depth_raw - projection[2][2])), 65000.0f);
}

// cosine to the 16th, keeps the directional field on its own surface across creases
float ssao_normal_weight(float cosine)
{
    float x = saturate(cosine);
    x *= x;
    x *= x;
    x *= x;
    return x * x;
}

#if defined(SSAO_PREPASS)

#include "cortao/CORTAOPrepass.h"

cortao_float cortao_loadDepth(cortao_int2 pixelCoords)
{
    return get_depth(uint2(pixelCoords));
}

cortao_float3 cortao_loadNormal(cortao_int2 pixelCoords)
{
    return cortao_load_view_normal(pixelCoords);
}

void cortao_writePackedData(cortao_int2 pixelCoords, cortao_int mipLevel, cortao_uint data)
{
#if defined(API_WEBGPU)
    // webgpu splits the mip array into separate bindings, so it takes constant indices only and the early return in the prepass loop keeps it from unrolling
    switch (mipLevel)
    {
        case 0: tex_uav_mips[0][pixelCoords] = asfloat(data); break;
        case 1: tex_uav_mips[1][pixelCoords] = asfloat(data); break;
        case 2: tex_uav_mips[2][pixelCoords] = asfloat(data); break;
        case 3: tex_uav_mips[3][pixelCoords] = asfloat(data); break;
        case 4: tex_uav_mips[4][pixelCoords] = asfloat(data); break;
        case 5: tex_uav_mips[5][pixelCoords] = asfloat(data); break;
        case 6: tex_uav_mips[6][pixelCoords] = asfloat(data); break;
        case 7: tex_uav_mips[7][pixelCoords] = asfloat(data); break;
    }
#else
    tex_uav_mips[mipLevel][pixelCoords] = asfloat(data);
#endif
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // reverse z, d = p22 + p32 / z, so 1 / z = d / p32 - p22 / p32
    matrix projection = get_projection();

    CORTAOPrepassParameters params;
    params.packedResolution = cortao_packed_resolution();
    params.depthScale       = 1.0f / projection[3][2];
    params.depthOffset      = -projection[2][2] / projection[3][2];
    params.randomSeed       = cortao_frame_seed();
    params.mipCount         = pass_uint(pass_ssao::mip_count);
    params._padding0        = 0;
    params._padding1        = 0;
    params._padding2        = 0;

    cortao_prepass(thread_id.xy, params);
}

#elif defined(SSAO_TEMPORAL)

// previous frame uv and ndc depth of a world static surface, camera motion only, jittered like the history
float3 camera_previous(float2 uv, float depth_raw)
{
    float3 position_world = get_position(depth_raw, uv);
    float4 clip           = mul(float4(position_world, 1.0f), get_view_projection_previous());
    float3 ndc            = clip.xyz / clip.w;
    return float3(ndc.xy * float2(0.5f, -0.5f) + 0.5f, ndc.z);
}

// velocity buffer as a uv offset from this frame's jittered sample to the previous frame's jittered sample
float2 velocity_buffer_uv(uint2 pixel)
{
    float2 velocity_ndc = get_velocity_uv(pixel);
    return (buffer_frame.taa_jitter_previous - buffer_frame.taa_jitter_current - velocity_ndc) * float2(0.5f, -0.5f);
}

// one of the 32 taps a group spreads around its centre to find surfaces moving through the world (a car,
// its wheels) within reach of the ao kernel, the only case where the ao of a world static receiver changes
// from frame to frame. 4 rings of 8 out to twice the ao radius plus the group half extent, so every pixel
// of the group is still covered on the frames right after the occluder has left its kernel, when the
// darkening is still in the history. the radius cap is relative to the screen so the reach does not
// depend on the render resolution. returns (velocity * weight, weight, ring of the tap when it hit a mover
// else 4), the first two rings lie within the ao radius, where the occluder's own darkening lives
float4 moving_occluder_tap(int2 center, uint index, int2 out_size, float view_depth)
{
    const float radius_px = min(g_ao_radius * get_projection()[0][0] * float(out_size.x) / max(view_depth, 1e-4f), 0.25f * float(out_size.y));
    const uint  ring      = index >> 3u;
    const float angle     = float(index) * (PI / 4.0f) + float(ring) * (PI / 16.0f);
    const float dist      = radius_px * 0.5f * float(ring + 1u) + 4.0f;
    const int2  tap       = clamp(center + int2(round(float2(cos(angle), sin(angle)) * dist)), int2(0, 0), out_size - 1);
    const float tap_depth = get_depth(uint2(tap));
    if (tap_depth == 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 4.0f);
    }
    const float2 tap_uv      = (float2(tap) + 0.5f) / float2(out_size);
    const float2 tap_cam_vel = camera_previous(tap_uv, tap_depth).xy - tap_uv;
    const float2 tap_vel     = velocity_buffer_uv(uint2(tap));
    const float  mover       = smoothstep(0.4f, 0.8f, length((tap_vel - tap_cam_vel) * float2(out_size)));
    return float4(tap_vel * mover, mover, mover > 0.5f ? float(ring) : 4.0f);
}

struct history_tap
{
    float  vis;
    float  age;
    float  accept;
    int2   base;
    float4 weights;
};

// tex2 = history (bent offset * visibility, visibility), tex3 = history (view depth, age / max frames)
// bilateral history: weight each of 4 prev neighbors by depth match instead of trusting bilinear; kills sub-pixel ghosting where neighbors straddle a depth discontinuity
// weights are normalized so they sum to 1 over the accepted neighbors
history_tap fetch_history(float2 prev_uv, float2 prev_size, float2 texture_size, float expected_z, float gate_lo, float gate_hi)
{
    history_tap h = (history_tap)0;
    if (any(prev_uv < 0.0f) || any(prev_uv > 1.0f))
    {
        return h;
    }
    const float2 prev_px    = clamp(prev_uv * prev_size - 0.5f, 0.0f, prev_size - 1.0f);
    const int2   prev_base  = int2(floor(prev_px));
    const float2 prev_frac  = prev_px - float2(prev_base);
    const float4 spatial4   = float4(
        (1.0f - prev_frac.x) * (1.0f - prev_frac.y),
        prev_frac.x          * (1.0f - prev_frac.y),
        (1.0f - prev_frac.x) * prev_frac.y,
        prev_frac.x          * prev_frac.y);

    // gather the 2x2 footprint, reorder gathered (.w, .z, .x, .y) into tl, tr, bl, br
    const float2 gather_uv = (float2(prev_base) + 1.0f) / texture_size;
    const float4 pv_g      = tex3.GatherRed(samplers[sampler_point_clamp], gather_uv);
    const float4 pa_g      = tex3.GatherGreen(samplers[sampler_point_clamp], gather_uv);
    const float4 ph_g      = tex2.GatherAlpha(samplers[sampler_point_clamp], gather_uv);
    const float4 pv4       = float4(pv_g.w, pv_g.z, pv_g.x, pv_g.y);
    const float4 pa4       = float4(pa_g.w, pa_g.z, pa_g.x, pa_g.y);
    const float4 ph4       = float4(ph_g.w, ph_g.z, ph_g.x, ph_g.y);

    const float4 r4 = abs(pv4 - expected_z) / max(expected_z, 1e-3f);
    const float4 depth_w4    = 1.0f - smoothstep(gate_lo.xxxx, gate_hi.xxxx, r4);
    const float4 w4          = spatial4 * depth_w4;
    const float  weight_sum  = dot(w4, 1.0f.xxxx);

    // the accept is the fraction of the bilinear footprint with valid depth, the bilateral weights already discard bad neighbors
    h.accept = smoothstep(0.25f, 0.75f, weight_sum);
    if (h.accept > 0.0f)
    {
        h.weights = w4 / weight_sum;
        h.vis     = dot(ph4, h.weights);
        h.age     = dot(pa4, h.weights) * g_history_max_frames;
        h.base    = prev_base;
    }
    return h;
}

// an 8x8 output group plus the 5x5 filter halo
#define TILE_DIM 12
#define TILE_N   (TILE_DIM * TILE_DIM)
groupshared float  g_tile_visibility[TILE_N];
groupshared float  g_tile_depth[TILE_N];
groupshared float3 g_tile_normal[TILE_N];
groupshared float3 g_tile_bent[TILE_N];
groupshared float4 g_tile_mover[32];

// tex = this frame's raw cortao, tex2/tex3 = history read, tex_uav/tex_uav2 = history write
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID)
{
    const int2  out_size   = int2(get_render_resolution_active());
    const uint2 group_base = thread_id.xy - group_thread_id.xy;
    const int2  tile_origin = int2(group_base) - 2;
    const uint  tile_index  = group_thread_id.y * THREAD_GROUP_COUNT_X + group_thread_id.x;
    for (uint i = tile_index; i < TILE_N; i += THREAD_GROUP_COUNT_X * THREAD_GROUP_COUNT_Y)
    {
        const int2   tile_pixel = clamp(tile_origin + int2(i % TILE_DIM, i / TILE_DIM), int2(0, 0), out_size - 1);
        const float4 tile_ao    = tex.Load(int3(tile_pixel, 0));
        g_tile_visibility[i]    = tile_ao.w;
        g_tile_bent[i]          = tile_ao.xyz;
        g_tile_depth[i]         = ssao_view_depth(get_depth(uint2(tile_pixel)));
        g_tile_normal[i]        = get_normal(uint2(tile_pixel));
    }
    if (tile_index < 32u)
    {
        const int2  mover_center = min(int2(group_base) + 4, out_size - 1);
        const float center_depth = get_depth(uint2(mover_center));
        const float mover_depth  = center_depth > 0.0f ? center_depth : get_depth(uint2(min(int2(thread_id.xy), out_size - 1)));
        g_tile_mover[tile_index] = moving_occluder_tap(mover_center, tile_index, out_size, ssao_view_depth(mover_depth));
    }
    GroupMemoryBarrierWithGroupSync();
    [unroll]
    for (uint stride = 16u; stride > 0u; stride >>= 1u)
    {
        if (tile_index < stride)
        {
            const float4 other = g_tile_mover[tile_index + stride];
            const float4 self  = g_tile_mover[tile_index];
            g_tile_mover[tile_index] = float4(self.xyz + other.xyz, min(self.w, other.w));
        }
        GroupMemoryBarrierWithGroupSync();
    }
    const float4 mover = g_tile_mover[0];
    if (any(int2(thread_id.xy) >= out_size))
    {
        return;
    }

    const uint2  pixel     = thread_id.xy;
    const float2 uv        = (float2(pixel) + 0.5f) / float2(out_size);
    const float  depth_raw = get_depth(pixel);
    const float  view_z    = ssao_view_depth(depth_raw);
    const int2   center    = int2(pixel) - tile_origin;
    const float3 normal    = g_tile_normal[center.y * TILE_DIM + center.x];

    // sky does not receive ao or carry useful temporal history
    if (depth_raw == 0.0f)
    {
        tex_uav[pixel]  = float4(0, 0, 0, 1);
        tex_uav2[pixel] = float4(view_z, 1.0f / g_history_max_frames, 0.0f, 0.0f);
        return;
    }

    const float depth_scale = 1.0f / max(view_z, 1e-3f);

    // A separable [1 2 3 2 1] spatial kernel reduces fresh-frame noise before reprojection.
    // Only immediate neighbours on the same surface set the history bounds.
    float  cur_sum   = 0.0f;
    float  cur_sum2  = 0.0f;
    float  cur_wsum  = 0.0f;
    float  v_min     = g_tile_visibility[center.y * TILE_DIM + center.x];
    float  v_max     = v_min;
    float3 bent_sum  = 0.0f;
    float3 bent_min  = g_tile_bent[center.y * TILE_DIM + center.x];
    float3 bent_max  = bent_min;
    [unroll]
    for (int dy = -2; dy <= 2; ++dy)
    {
        [unroll]
        for (int dx = -2; dx <= 2; ++dx)
        {
            const uint  cache_index = (center.y + dy) * TILE_DIM + center.x + dx;
            const float ao_n        = g_tile_visibility[cache_index];
            const float r_n         = abs(g_tile_depth[cache_index] - view_z) * depth_scale;
            const float surface_w   = (1.0f - smoothstep(0.01f, 0.08f, r_n)) * ssao_normal_weight(dot(normal, g_tile_normal[cache_index]));
            const float w_n         = surface_w * (3 - abs(dx)) * (3 - abs(dy));
            const float3 bent       = g_tile_bent[cache_index];
            bent_sum += bent * w_n;
            if (max(abs(dx), abs(dy)) <= 1 && surface_w > 0.05f)
            {
                v_min = min(v_min, ao_n);
                v_max = max(v_max, ao_n);
                bent_min = min(bent_min, bent);
                bent_max = max(bent_max, bent);
            }
            cur_sum  += ao_n * w_n;
            cur_sum2 += ao_n * ao_n * w_n;
            cur_wsum += w_n;
        }
    }
    const float cur_vis   = cur_wsum > 1e-4f ? cur_sum / cur_wsum : g_tile_visibility[center.y * TILE_DIM + center.x];
    const float cur_sigma = cur_wsum > 1e-4f ? sqrt(max(cur_sum2 / cur_wsum - cur_vis * cur_vis, 0.0f)) : 0.0f;
    const float3 cur_bent = cur_wsum > 1e-4f ? bent_sum / cur_wsum : g_tile_bent[center.y * TILE_DIM + center.x];
    float3 hist_bent = cur_bent;

    const float3 prev_camera     = camera_previous(uv, depth_raw);
    // The velocity target stores the previous linear depth of this surface, including object motion.
    const float object_prev_z = abs(tex_velocity[pixel].w);
    const bool has_object_depth = object_prev_z > 0.0f && object_prev_z < FLT_MAX_16U;
    const float prev_expected_z = has_object_depth ? min(object_prev_z, 65000.0f) : ssao_view_depth(prev_camera.z);
    const float2 velocity_matrix = prev_camera.xy - uv;

    // the matrix reprojection is exact for world static pixels, the velocity buffer carries the true per-object motion where they disagree (the dashboard moving on its suspension, other cars)
    const float2 velocity_raw    = velocity_buffer_uv(pixel);
    const float  vel_disagree_px = length((velocity_raw - velocity_matrix) * float2(out_size));
    const float  dyn_blend       = smoothstep(0.4f, 0.8f, vel_disagree_px);
    const float2 surface_vel     = lerp(velocity_matrix, velocity_raw, dyn_blend);
    // near a moving occluder the ao it casts moves with it, so the history is fetched where the occluder's ao was last frame; the receiver's own reprojection would drag the darkening it cast behind it (the wheel trail)
    // only receivers that move apart from the occluder, a surface of the moving object itself (the dashboard of a vibrating car) carries its own ao along and is reprojected exactly by its own velocity
    const float2 mover_vel    = mover.z > 0.0f ? mover.xy / mover.z : 0.0f.xx;
    const float  mover_apart  = smoothstep(0.4f, 0.8f, length((mover_vel - velocity_raw) * float2(out_size)));
    const float  mover_near   = saturate(mover.z) * (1.0f - dyn_blend) * mover_apart;
    const float2 velocity     = lerp(surface_vel, mover_vel, mover_near);
    // within the ao radius of the occluder, where its own darkening lives
    const float  mover_close  = mover.w < 1.5f ? mover_near : 0.0f;
    const float2 prev_uv      = uv + velocity;

    // velocity in pixels per frame; beyond ~96px reprojection is unreliable, bail to the current frame
    const float vel_px       = length(velocity * float2(out_size));
    const bool  too_fast     = vel_px > 96.0f;
    const bool  skip_history = pass_bool(pass_ssao::reset_history);

    float2 texture_size;
    tex2.GetDimensions(texture_size.x, texture_size.y);
    const float2 prev_size = max(floor(buffer_frame.resolution_render * buffer_frame.resolution_scale_previous), float2(1.0f, 1.0f));

    float blend_current = 1.0f;
    float hist_vis      = cur_vis;
    float new_age       = 1.0f;

    if (!skip_history && !too_fast)
    {
        // Allow extra depth error when only camera reprojection is available.
        const float vel_slack = has_object_depth ? 0.0f : saturate(vel_px / 32.0f) * 0.012f;
        const float gate_lo   = 0.002f + vel_slack;
        const float gate_hi   = 0.01f  + vel_slack * 4.0f;

        // near a moving occluder the ao of a receiver is the sum of what the occluder casts, which moves with the occluder, and the static ao of the world, which moves with the receiver. the history is fetched along both and the one closer to the current frame wins, so the static ao (a kerb next to a turning car) is not dragged along with the occluder. a receiver history darker than the occluder's is what the trail of the occluder's darkening looks like, it has to beat the current noise to be taken
        history_tap history = fetch_history(prev_uv, prev_size, texture_size, prev_expected_z, gate_lo, gate_hi);
        if (mover_near > 0.0f)
        {
            const history_tap surface_history = fetch_history(uv + surface_vel, prev_size, texture_size, prev_expected_z, gate_lo, gate_hi);
            const float       trail_margin    = surface_history.vis < history.vis ? (cur_sigma + 0.02f) * mover_close : 0.0f;
            if (surface_history.accept > 0.0f && (history.accept <= 0.0f || abs(surface_history.vis - cur_vis) + trail_margin < abs(history.vis - cur_vis)))
            {
                history = surface_history;
            }
        }

        // per-pixel age ramps up to the max frames, alpha = 1 / age, an aabb clamp with an adaptive window keeps the history bounded by what the current frame can plausibly produce
        const float depth_accept = history.accept;
        if (depth_accept > 0.0f)
        {
            const float prev_age = history.age;
            const float raw_hist = history.vis;

            // the aabb gate catches reprojection ghosts during motion, idle vibration (suspension, engine) stays below its velocity ramp and history that survived 8+ frames of depth and velocity gating is locked in
            // fast_trust past ~44 px/frame is where the widened depth gate lets stale history slip through, the age cap below shortens the history there
            const float motion_trust = smoothstep(2.0f, 8.0f, vel_px);
            const float fast_trust   = smoothstep(10.0f, 44.0f, vel_px);
            // a moving occluder nearby changes the ao of this pixel even when it is still on screen, so it gets the full moving treatment: no age immunity, tight clamp, unconditional occluder release and a shorter history
            const float release_gate = max(motion_trust, mover_near);

            const float aabb_half         = (v_max - v_min) * 0.5f;
            const float aabb_strength_vel = max(smoothstep(8.0f, 32.0f, vel_px), mover_near);
            // age grants immunity from the aabb clamp so a settled pixel cannot flicker, at speed that immunity would lock in moving occluder trails, so it fades out
            const float age_conf      = saturate(prev_age / 8.0f) * (1.0f - max(fast_trust, mover_near));
            const float aabb_strength = aabb_strength_vel * (1.0f - age_conf);

            // adaptive clamp window, the margin widens by the local aabb extent as the gate relaxes so rest holds the true mean while full motion on young history keeps the tight anti ghost clamp
            const float clamp_margin    = 0.025f + (1.0f - aabb_strength) * aabb_half;
            // moving history darker than every current sample is a departed occluder (the wheel contact ao left on the road), the dark side stays tight while moving
            const float clamp_margin_lo = lerp(clamp_margin, 0.0f, release_gate);
            const float clamped         = clamp(raw_hist, v_min - clamp_margin_lo, v_max + clamp_margin);

            // disocclusion via clip distance with a threshold scaled by the local aabb variance, tight on flat surfaces, loose in noisy pockets
            const float clip_lo     = lerp(0.05f, 0.20f, saturate(aabb_half * 2.0f));
            const float clip_hi     = clip_lo + 0.15f;
            const float clip_amount = abs(raw_hist - clamped);
            const float trust       = 1.0f - smoothstep(clip_lo, clip_hi, clip_amount);
            // speed only shortens the history, the history following the occluder lags the parts of it that move differently from its mean, so within its ao radius the history is kept short
            const float fast_max_age = lerp(4.0f, 16.0f, has_object_depth ? depth_accept * trust : 0.0f);
            const float max_age   = min(lerp(g_history_max_frames, fast_max_age, fast_trust), lerp(lerp(g_history_max_frames, 8.0f, mover_near), 2.0f, mover_close));
            const float eff_trust = lerp(1.0f, trust, aabb_strength);
            // occluder trail release, the brightening under a departing occluder is measured against the local aabb extent, history darker than every depth matched current sample is dropped outright while moving
            const float occluder_release = release_gate * max(smoothstep(0.03f, 0.10f, cur_vis - raw_hist - aabb_half), smoothstep(0.01f, 0.04f, v_min - raw_hist));
            const float eff_accept       = depth_accept * eff_trust * (1.0f - occluder_release);

            // Scale the retained sample count once, then add this frame as one sample.
            new_age  = 1.0f + min(prev_age, max_age - 1.0f) * eff_accept;
            hist_vis = clamped;

            float3 previous_bent = 0.0f;
            [unroll]
            for (uint tap = 0u; tap < 4u; ++tap)
            {
                int2 p = clamp(history.base + int2(tap & 1u, tap >> 1u), int2(0, 0), int2(prev_size) - 1);
                previous_bent += tex2.Load(int3(p, 0)).xyz * history.weights[tap];
            }
            hist_bent = clamp(previous_bent, bent_min - 0.04f, bent_max + 0.04f);

            blend_current = rcp(new_age);
        }
    }

    // the directional history follows the ao history, bounded by the current neighbourhood
    const float  resolved_ao   = saturate(lerp(hist_vis, cur_vis, blend_current));
    const float3 resolved_bent = lerp(hist_bent, cur_bent, blend_current);
    tex_uav[pixel]  = float4(resolved_bent, resolved_ao);
    tex_uav2[pixel] = float4(view_z, new_age / g_history_max_frames, 0.0f, 0.0f);
}

#elif defined(SSAO_DENOISE)

// cross bilateral denoise of the accumulated result, a 3x3 kernel widening to 5x5 where the history is young
// tex = history (bent offset * visibility, visibility), tex2 = (view depth, age / max frames), tex_uav = (bent normal, visibility)
#define TILE_W     THREAD_GROUP_COUNT_X
#define TILE_H     THREAD_GROUP_COUNT_Y
#define HALO       2
#define LDS_W      (TILE_W + 2 * HALO)
#define LDS_H      (TILE_H + 2 * HALO)
#define LDS_N      (LDS_W * LDS_H)
groupshared float4 g_lds_ao[LDS_N];
groupshared float4 g_lds_normal_depth[LDS_N];

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID, uint3 group_thread_id : SV_GroupThreadID, uint3 group_id : SV_GroupID)
{
    const int2 out_size   = int2(get_render_resolution_active());
    const int2 max_coord  = out_size - 1;
    const uint thread_idx = group_thread_id.y * TILE_W + group_thread_id.x;

    const int2 tile_origin = int2(group_id.xy) * int2(TILE_W, TILE_H) - HALO;
    for (uint i = thread_idx; i < LDS_N; i += TILE_W * TILE_H)
    {
        const int2 scoord     = clamp(tile_origin + int2(i % LDS_W, i / LDS_W), int2(0, 0), max_coord);
        g_lds_ao[i]           = tex.Load(int3(scoord, 0));
        g_lds_normal_depth[i] = float4(get_normal(uint2(scoord)), tex2.Load(int3(scoord, 0)).r);
    }
    GroupMemoryBarrierWithGroupSync();

    const int2 c = int2(thread_id.xy);
    if (any(c > max_coord))
    {
        return;
    }

    const int2   lds_center  = int2(group_thread_id.xy) + HALO;
    const int    center_idx  = lds_center.y * LDS_W + lds_center.x;
    const float4 center_ao   = g_lds_ao[center_idx];
    const float3 n_c         = g_lds_normal_depth[center_idx].xyz;
    const float  z_c         = g_lds_normal_depth[center_idx].w;
    const float  center_age  = tex2.Load(int3(c, 0)).g;
    const float  depth_scale = 1.0f / max(z_c, 1e-3f);

    // pixels that just lost their history under motion are the noisy ones, widen the depth gate, soften the normal weight and drop the center below a neighbor weight so the kernel actually averages, converged pixels keep the tight gates so static detail stays crisp
    const float noisy         = saturate(1.0f - center_age);
    const float depth_gate_hi = lerp(0.04f, 0.22f, noisy);
    const float normal_pow    = lerp(8.0f, 1.0f, noisy);
    const float center_w      = lerp(4.0f, 0.6f, noisy);

    // local linear depth model: predict each tap's depth from the center plus the screen space depth gradient, so coplanar taps on a sloped surface pass the gate at full weight regardless of view angle. the gradient takes the least varying side per axis so a depth edge among the axial taps biases toward the continuous surface instead of smearing across it
    const float zxp = g_lds_normal_depth[center_idx + 1].w;
    const float zxn = g_lds_normal_depth[center_idx - 1].w;
    const float zyp = g_lds_normal_depth[center_idx + LDS_W].w;
    const float zyn = g_lds_normal_depth[center_idx - LDS_W].w;
    const float gx  = abs(zxp - z_c) < abs(z_c - zxn) ? (zxp - z_c) : (z_c - zxn);
    const float gy  = abs(zyp - z_c) < abs(z_c - zyn) ? (zyp - z_c) : (z_c - zyn);

    float  sum       = center_w * center_ao.w;
    float  wsum      = center_w;
    float4 bent_sum  = center_ao * center_w;

    static const int2 offsets8[8] =
    {
        int2(-1, -1), int2(0, -1), int2(1, -1),
        int2(-1,  0),              int2(1,  0),
        int2(-1,  1), int2(0,  1), int2(1,  1)
    };
    static const float weights8[8] =
    {
        1.0f, 2.0f, 1.0f,
        2.0f,       2.0f,
        1.0f, 2.0f, 1.0f
    };

    [unroll]
    for (int j = 0; j < 8; ++j)
    {
        const int2   koff     = offsets8[j];
        const int    p_idx    = (lds_center.y + koff.y) * LDS_W + lds_center.x + koff.x;
        const float4 ao       = g_lds_ao[p_idx];
        const float4 n_z      = g_lds_normal_depth[p_idx];
        // gate on the deviation from the local plane, not the raw depth delta
        const float  z_pred   = z_c + gx * koff.x + gy * koff.y;
        const float  r        = abs(n_z.w - z_pred) * depth_scale;
        const float  depth_w  = 1.0f - smoothstep(0.005f, depth_gate_hi, r);
        const float  normal_w = pow(saturate(dot(n_z.xyz, n_c)), normal_pow);
        const float  w        = weights8[j] * depth_w * normal_w;
        sum  += ao.w * w;
        wsum += w;
        // the directional field stays on its surface even while noisy ao uses a wider spatial gate
        bent_sum += ao * weights8[j] * (1.0f - smoothstep(0.005f, 0.04f, r)) * ssao_normal_weight(dot(n_z.xyz, n_c));
    }

    // pixels that just lost their history (revealed behind the moving steering wheel, after a fast move) widen to the 5x5 ring, converged pixels keep the 3x3
    static const int2 offsets16[16] =
    {
        int2(-2, -2), int2(-1, -2), int2(0, -2), int2(1, -2), int2(2, -2),
        int2(-2, -1),                                         int2(2, -1),
        int2(-2,  0),                                         int2(2,  0),
        int2(-2,  1),                                         int2(2,  1),
        int2(-2,  2), int2(-1,  2), int2(0,  2), int2(1,  2), int2(2,  2)
    };
    static const float weights16[16] =
    {
        0.25f, 0.5f, 1.0f, 0.5f, 0.25f,
        0.5f,                    0.5f,
        1.0f,                    1.0f,
        0.5f,                    0.5f,
        0.25f, 0.5f, 1.0f, 0.5f, 0.25f
    };

    const float outer_w = noisy * noisy;
    [branch]
    if (outer_w > 0.01f)
    {
        [unroll]
        for (int k = 0; k < 16; ++k)
        {
            const int2   koff     = offsets16[k];
            const int    p_idx    = (lds_center.y + koff.y) * LDS_W + lds_center.x + koff.x;
            const float4 ao       = g_lds_ao[p_idx];
            const float4 n_z      = g_lds_normal_depth[p_idx];
            const float  z_pred   = z_c + gx * koff.x + gy * koff.y;
            const float  r        = abs(n_z.w - z_pred) * depth_scale;
            const float  depth_w  = 1.0f - smoothstep(0.005f, depth_gate_hi, r);
            const float  normal_w = pow(saturate(dot(n_z.xyz, n_c)), normal_pow);
            const float  w        = outer_w * weights16[k] * depth_w * normal_w;
            sum  += ao.w * w;
            wsum += w;
            bent_sum += ao * outer_w * weights16[k] * (1.0f - smoothstep(0.005f, 0.04f, r)) * ssao_normal_weight(dot(n_z.xyz, n_c));
        }
    }

    const float3 direction   = n_c * bent_sum.w + bent_sum.xyz;
    const float3 bent_normal = dot(direction, n_c) > 1e-5f ? normalize(direction) : n_c;
    tex_uav[c] = float4(bent_normal, saturate(sum / max(wsum, 1e-4f)));
}

#else

#define CORTAO_SAMPLE_COUNT             16
#define CORTAO_RAY_COUNT                16
#define CORTAO_OCCLUDER_LOOKUP_TABLE    1
#define CORTAO_BENT_NORMAL_LOOKUP_TABLE 1
#define CORTAO_CUSTOM_NOISE             1
// the depth buffer only holds the camera facing skin of an occluder. an occluder between the receiver and the camera that also rises above the receiver's surface (a bumper over the road, a table top over the floor) hides volume that extends over the receiver, so its disc is enlarged to stand in for it. the enlargement grows with the occluder's distance over the radius, which leaves close and thin occluders such as leaves as they are
#define CORTAO_OCCLUDER_RADIUS_SCALE(direction, viewDirection, normal, distanceRatio) (1.0f + 1.5f * (distanceRatio) * saturate(dot(direction, viewDirection)) * saturate(dot(direction, normal)))

#include "cortao/CORTAOMainPass.h"

// interleaved gradient noise (jimenez 2014): each 3x3 neighbourhood covers the [0, 1) range evenly, so the temporal prefilter and the denoiser cancel it
float cortao_ign(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

// R3 low-discrepancy sequence for spiral rotation, radius dither and ray rotation.
// Advance in fixed point so large frame counters retain sub-turn precision.
cortao_float4 cortao_customNoise(cortao_int2 pixelCoords)
{
    const float2 pixel = float2(pixelCoords);
    const uint frame = buffer_frame.frame;
    const uint3 phase = frame * uint3(3518319155u, 2882110345u, 2360945575u);
    const float3 rotation = float3(phase >> 8u) * (1.0f / 16777216.0f);
    return frac(float4(
        cortao_ign(pixel),
        dot(pixel, float2(0.2202203f, 0.3591321f)),
        dot(pixel, float2(0.1251447f, 0.6607579f)),
        cortao_ign(float2(pixel.x, -pixel.y))) + float4(rotation, 0.5f * float(frame & 1u)));
}

// tex3 = packed depth and normal mips, tex4 = bent normal table, tex3d = occluder table
cortao_uint cortao_samplePackedData(cortao_float2 texCoords, cortao_float mipmap)
{
    // Black border in active viewport coordinates. The prepass writes floor-sized mips.
    if (any(texCoords < 0.0f) || any(texCoords >= 1.0f))
        return 0u;

    uint2 size = uint2(get_render_resolution_active());
    uint max_mip = min(7u, uint(firstbithigh(min(size.x, size.y))));
    uint mip = uint(clamp(floor(mipmap + 0.5f), 0.0f, float(max_mip)));
    uint2 mip_size = size >> mip;
    uint2 pixel = min(uint2(texCoords * size) >> mip, mip_size - 1u);
    return asuint(tex3.Load(int3(pixel, mip)).r);
}

cortao_half3 cortao_loadNormal(cortao_int2 pixelCoords)
{
    return cortao_load_view_normal(pixelCoords);
}

// r16 unorm stand in for the r16 uint table, the bits come back exactly
cortao_uint cortao_sampleOccluderLookupTable(cortao_float3 texCoords)
{
    return uint(tex3d.SampleLevel(samplers[sampler_point_clamp], texCoords, 0.0f).r * 65535.0f + 0.5f);
}

// 256x256 r8g8 unorm holding the raw snorm bytes, one texel per ray mask
cortao_half3 cortao_loadBentNormalLookupTable(cortao_uint rayBits)
{
    uint2 bytes  = uint2(tex4.Load(int3(rayBits & 0xFFu, rayBits >> 8u, 0)).rg * 255.0f + 0.5f);
    int2 values  = int2(bytes) - int2(bytes >= 128u) * 256;
    float2 xy    = max(float2(values) / 127.0f, -1.0f);
    return float3(xy, sqrt(max(0.0f, 1.0f - dot(xy, xy))));
}

void cortao_writeResult(cortao_int2 pixelCoords, cortao_half occlusion, cortao_half3 bentNormal, cortao_half3x3 tangentSpaceTransform, cortao_uint rayBits)
{
    // Filter the deviation from the receiving normal so smoothing preserves curvature.
    // An open hemisphere has no bend. A fully blocked one carries no directional weight.
    const float visibility = saturate(occlusion);
    float3 offset = 0.0f;
    if (rayBits != 0u && visibility > 0.0f)
    {
        float3 delta = normalize(bentNormal) - tangentSpaceTransform[2];
        offset = view_to_world(float3(delta.xy, -delta.z), false);
    }
    tex_uav[pixelCoords] = float4(offset * visibility, visibility);
}

CORTAOMainPassParameters cortao_main_pass_parameters()
{
    // spartan projection is left handed, x' = p00 x / z + p20, y' = p11 y / z + p21, uv.y points down
    float4 projection  = cortao_projection_terms();
    float2 resolution  = get_render_resolution_active();
    float inverse_x    = 1.0f / projection.x;
    float inverse_y    = 1.0f / projection.y;
    float apparent_max = inverse_y * g_ao_screen_radius_ratio * 2.0f;

    CORTAOMainPassParameters params;
    params.packedResolution                   = cortao_packed_resolution();
    params.randomSeed                         = cortao_frame_seed();
    params.normalBias                         = inverse_x / resolution.x * sqrt(2.0f);
    params.radius                             = g_ao_radius;
    params.unprojectParams                    = float4(2.0f * inverse_x, -2.0f * inverse_y, (-1.0f - projection.z) * inverse_x, (1.0f - projection.w) * inverse_y);
    params.projectionParams                   = float4(0.5f * projection.x, -0.5f * projection.y, -0.5f - 0.5f * projection.z, -0.5f + 0.5f * projection.w);
    params.maxApparentCircleRadiusCoefficient = apparent_max / sqrt(apparent_max * apparent_max + 1.0f);
    params.mipBias                            = log2(projection.y * resolution.y * 0.5f) + g_ao_mip_bias;
    params.inverseMaxSlopeCompensation        = 1.0f / g_ao_max_slope_compensation;
    params.backfaceThreshold                  = g_ao_backface_threshold;
    params.backfaceBoost                      = g_ao_backface_boost;
    params._padding0                          = 0;
    params._padding1                          = 0;
    params._padding2                          = 0;
    return params;
}

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    const uint2 pos = thread_id.xy;
    if (any(pos >= uint2(get_render_resolution_active())))
    {
        return;
    }

    // sky has no receiving surface, keep open visibility for the filters
    if (get_depth(pos) == 0.0f)
    {
        tex_uav[pos] = float4(0, 0, 0, 1);
        return;
    }

    cortao_mainPass(pos, cortao_main_pass_parameters());
}

#endif
