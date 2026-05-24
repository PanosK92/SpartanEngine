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

//= INCLUDES =========
#include "common.hlsl"
//====================

// gpu-driven meshlet cull, two paths share the same kernel
// fast path: per-meshlet sphere frustum, per-meshlet cone backface, per-meshlet hi-z occlusion against the projected sphere
// fallback path: per-renderable aabb frustum + hi-z, used for skinned (deformation invalidates per-meshlet bounds) and the hw-instancing fallback (single task fans out to N instances)
// per-instance draws still take the fast path, the per-instance world transform is rebuilt from the packed instance buffer so each instance's cone and bounds are tested independently
// survivors are wave-aggregated into meshlet_instances and triangle_dispatch_args.group_count_x is bumped by the same amount in lockstep
// flags bit 0 skinned, bit 1 per_instance, bit 2 hw_instanced, bit 3 two_sided (read by the triangle pass)

// extracts the four side planes of the camera frustum from view_projection in world space
// only the side planes are used, near is unreliable on jittered projections and far is at infinity for reverse-z
// row-vector convention places the camera world-space planes on the columns of view_projection
void get_frustum_side_planes(out float4 plane_l, out float4 plane_r, out float4 plane_b, out float4 plane_t)
{
    matrix vp = buffer_frame.view_projection;
    plane_l = float4(vp._m00 + vp._m03, vp._m10 + vp._m13, vp._m20 + vp._m23, vp._m30 + vp._m33);
    plane_r = float4(vp._m03 - vp._m00, vp._m13 - vp._m10, vp._m23 - vp._m20, vp._m33 - vp._m30);
    plane_b = float4(vp._m01 + vp._m03, vp._m11 + vp._m13, vp._m21 + vp._m23, vp._m31 + vp._m33);
    plane_t = float4(vp._m03 - vp._m01, vp._m13 - vp._m11, vp._m23 - vp._m21, vp._m33 - vp._m31);

    // normalize so the radius compare lives in world units, the planes get reused for every task in the wave so doing it once is fine
    plane_l /= max(length(plane_l.xyz), 1e-8f);
    plane_r /= max(length(plane_r.xyz), 1e-8f);
    plane_b /= max(length(plane_b.xyz), 1e-8f);
    plane_t /= max(length(plane_t.xyz), 1e-8f);
}

bool sphere_in_side_planes(float3 center, float radius, float4 plane_l, float4 plane_r, float4 plane_b, float4 plane_t)
{
    float dl = dot(plane_l.xyz, center) + plane_l.w;
    float dr = dot(plane_r.xyz, center) + plane_r.w;
    float db = dot(plane_b.xyz, center) + plane_b.w;
    float dt = dot(plane_t.xyz, center) + plane_t.w;
    float min_dist = min(min(dl, dr), min(db, dt));
    return min_dist >= -radius;
}

// hi-z test for a world-space aabb, picks the smallest mip whose two texel covers the box and rejects against the closest sample
// shared by the per-meshlet sphere path (sphere expanded to its world aabb) and the per-renderable fallback
bool aabb_hiz_visible(float3 box_min, float3 box_max, float max_mip_level)
{
    float3 corners_world[8] =
    {
        box_min,
        float3(box_min.x, box_min.y, box_max.z),
        float3(box_min.x, box_max.y, box_min.z),
        float3(box_min.x, box_max.y, box_max.z),
        float3(box_max.x, box_min.y, box_min.z),
        float3(box_max.x, box_min.y, box_max.z),
        float3(box_max.x, box_max.y, box_min.z),
        box_max
    };

    float2 min_ndc      = float2( 1e30f,  1e30f);
    float2 max_ndc      = float2(-1e30f, -1e30f);
    float closest_box_z = -1e30f;
    bool any_behind     = false;
    bool any_front      = false;
    [unroll]
    for (int i = 0; i < 8; ++i)
    {
        float4 clip = mul(float4(corners_world[i], 1.0f), buffer_frame.view_projection);
        if (clip.w <= 0.0f)
        {
            any_behind = true;
        }
        else
        {
            float3 ndc    = clip.xyz / clip.w;
            min_ndc       = min(min_ndc, ndc.xy);
            max_ndc       = max(max_ndc, ndc.xy);
            closest_box_z = max(closest_box_z, ndc.z);
            any_front     = true;
        }
    }

    if (!any_front)
        return false;

    // partial behind cases skip occlusion, we already know at least one corner is in front and the side-frustum test ran upstream
    if (any_behind)
        return true;

    if (max_ndc.x < -1.0f || min_ndc.x > 1.0f || max_ndc.y < -1.0f || min_ndc.y > 1.0f)
        return false;

    float2 uv_a   = saturate(ndc_to_uv(min_ndc));
    float2 uv_b   = saturate(ndc_to_uv(max_ndc));
    float2 min_uv = min(uv_a, uv_b);
    float2 max_uv = max(uv_a, uv_b);

    float2 render_size;
    tex.GetDimensions(render_size.x, render_size.y);
    float4 box_uvs = float4(min_uv, max_uv);

    float2 uv_extent = max_uv - min_uv;
    int2   size      = uv_extent * render_size;
    float  mip       = ceil(log2(max(max(size.x, size.y), 1)));
    mip              = clamp(mip, 0, max_mip_level);

    float  level_lower = max(mip - 1, 0);
    float2 scale_mip   = exp2(-level_lower);
    float2 a           = floor(box_uvs.xy * scale_mip * render_size);
    float2 b           = ceil(box_uvs.zw * scale_mip * render_size);
    float2 dims        = b - a;
    if (dims.x <= 2 && dims.y <= 2)
        mip = level_lower;

    float2 mip_texel = exp2(mip) / render_size;
    box_uvs.xy = saturate(box_uvs.xy - mip_texel);
    box_uvs.zw = saturate(box_uvs.zw + mip_texel);

    float4 scaled_uvs = box_uvs * buffer_frame.resolution_scale;
    float2 center_uv  = (scaled_uvs.xy + scaled_uvs.zw) * 0.5f;
    float4 depth = float4(
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xy, mip).r,
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zy, mip).r,
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xw, mip).r,
        tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zw, mip).r
    );
    float depth_center = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), center_uv, mip).r;

    float furthest_z = min(min(min(min(depth.x, depth.y), depth.z), depth.w), depth_center);
    return closest_box_z > furthest_z - 0.01f;
}

// largest world-axis scale of the upper 3x3, used to lift the local-space meshlet radius into world units
float max_world_scale(float4x4 m)
{
    float sx = length(float3(m._m00, m._m01, m._m02));
    float sy = length(float3(m._m10, m._m11, m._m12));
    float sz = length(float3(m._m20, m._m21, m._m22));
    return max(sx, max(sy, sz));
}

// signed-byte cone unpack, dxc lowers this to a single byte_address load and four shifts
int4 unpack_cone_axis_cutoff(uint packed)
{
    int4 v = int4(packed << 24, packed << 16, packed << 8, packed);
    return v >> 24;
}

[numthreads(256, 1, 1)]
void main_cs(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    uint task_index = dispatch_thread_id.x;
    uint task_count = (uint)pass_get_f4_value().x;

    // wave-uniform inputs, planes are extracted once per dispatch and the compiler scalarizes the load across the wave
    float max_mip_level = pass_get_f4_value().y;
    uint  max_meshlet_instances = (uint)pass_get_f4_value().z;
    float4 plane_l, plane_r, plane_b, plane_t;
    get_frustum_side_planes(plane_l, plane_r, plane_b, plane_t);

    bool valid     = task_index < task_count;
    uint emit_count = 0u;
    CullTask task   = (CullTask)0;
    bool is_visible = false;

    if (valid)
    {
        task          = cull_tasks[task_index];
        DrawData draw = indirect_draw_data[task.draw_index];

        bool skinned         = (draw.flags & 1u) != 0u;
        bool is_per_instance = (draw.flags & 2u) != 0u;
        bool is_hw_instanced = (draw.flags & 4u) != 0u;
        bool is_two_sided    = (draw.flags & 8u) != 0u;

        // skinned and hw-instanced fall back to the per-renderable aabb path
        // skinned because deformation can push verts outside the static meshlet sphere
        // hw-instanced because a single task represents N instances and we have no per-instance bounds buffer
        bool use_aabb_fallback = skinned || is_hw_instanced;

        if (use_aabb_fallback)
        {
            Aabb aabb   = aabbs[draw.aabb_index];
            is_visible  = aabb_hiz_visible(aabb.min, aabb.max, max_mip_level);
        }
        else
        {
            // build the world transform once, per-instance fans out the cone and the sphere into per-instance space
            float4x4 world_xform = is_per_instance
                ? mul(pull_instance_transform(draw.instance_offset, task.instance_index), draw.transform)
                : draw.transform;

            MeshletBounds mb     = meshlet_bounds[task.meshlet_index];
            // dequantize the compressed bounds against the lod aabb that the cpu packer used in build_meshlets
            float3 center_local  = meshlet_decode_center(mb, draw.lod_aabb_min, draw.lod_aabb_extent);
            float  radius_local  = meshlet_decode_radius(mb, draw.lod_aabb_diag);
            float3 center_world  = mul(float4(center_local, 1.0f), world_xform).xyz;
            float  scale_max     = max_world_scale(world_xform);
            float  radius_world  = radius_local * scale_max;

            // per-meshlet side-frustum sphere reject
            is_visible = sphere_in_side_planes(center_world, radius_world, plane_l, plane_r, plane_b, plane_t);

            // per-meshlet backface cone, skipped for two-sided materials (the triangle cull would have to keep both sides anyway) and degenerate cones
            // skinned never reaches here, per_instance reaches here with the correct per-instance rotation baked into world_xform
            if (is_visible && !is_two_sided)
            {
                int4 cone = unpack_cone_axis_cutoff(mb.cone_axis_cutoff);
                if (cone.w < 127)
                {
                    float3 cone_axis_local = float3(cone.xyz) / 127.0f;
                    float  cone_cutoff     = float(cone.w) / 127.0f;
                    float3 cone_axis_world = normalize(mul(cone_axis_local, (float3x3)world_xform));
                    float3 to_meshlet      = center_world - buffer_frame.camera_position;
                    float  to_meshlet_len  = length(to_meshlet);
                    float  lhs             = dot(to_meshlet, cone_axis_world);
                    float  rhs             = cone_cutoff * to_meshlet_len + radius_world;
                    if (to_meshlet_len > 0.0f && lhs >= rhs)
                        is_visible = false;
                }
            }

            // per-meshlet hi-z, the sphere expands to its world aabb and reuses the shared occlusion path
            if (is_visible)
            {
                float3 box_min = center_world - radius_world;
                float3 box_max = center_world + radius_world;
                is_visible     = aabb_hiz_visible(box_min, box_max, max_mip_level);
            }
        }

        if (is_visible)
            emit_count = max(task.instance_count, 1u);
    }

    // wave-aggregated atomic, the first lane reserves a contiguous range for the whole wave so dense visibility doesn't hammer one address
    uint wave_total = WaveActiveSum(emit_count);
    uint lane_off   = WavePrefixSum(emit_count);
    uint wave_base  = 0u;
    uint overflow_u = 0u;
    if (WaveIsFirstLane() && wave_total > 0u)
    {
        InterlockedAdd(triangle_dispatch_args[0].group_count_x, wave_total, wave_base);
        overflow_u = (wave_base + wave_total) > max_meshlet_instances ? 1u : 0u;
    }
    wave_base   = WaveReadLaneFirst(wave_base);
    overflow_u  = WaveReadLaneFirst(overflow_u);

    // clamp group_count_x to max_meshlet_instances, the triangle pass guards on mi_idx but a runaway dispatch can still hit the hw indirect-dispatch ceiling
    if (overflow_u != 0u && WaveIsFirstLane())
        InterlockedMin(triangle_dispatch_args[0].group_count_x, max_meshlet_instances);

    if (emit_count == 0u)
        return;

    uint slot = wave_base + lane_off;
    if (slot >= max_meshlet_instances)
        return;
    uint write_count = min(emit_count, max_meshlet_instances - slot);

    for (uint i = 0u; i < write_count; i++)
    {
        MeshletInstance mi;
        mi.draw_index     = task.draw_index;
        mi.meshlet_index  = task.meshlet_index;
        mi.instance_index = task.instance_index + i;
        mi.padding0       = 0u;
        meshlet_instances[slot + i] = mi;
    }
}
