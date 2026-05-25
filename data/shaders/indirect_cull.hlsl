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
// fast path: per-meshlet sphere frustum, per-meshlet cone backface (sqrt-free), per-meshlet hi-z via analytical sphere projection
// fallback path: per-renderable aabb frustum + 8-corner hi-z, used for skinned (deformation invalidates per-meshlet bounds) and the hw-instancing fallback (single task fans out to N instances)
// per-instance draws still take the fast path, the per-instance world transform is rebuilt from the packed instance buffer so each instance's cone and bounds are tested independently
// survivors are wave-aggregated into meshlet_instances and triangle_dispatch_args.group_count_x is bumped by the same amount in lockstep
// flags bit 0 skinned, bit 1 per_instance, bit 2 hw_instanced, bit 3 two_sided (read by the triangle pass)
//
// hot-path budget per cull task in the fast path: one mat-vec mul to project the meshlet sphere, four hi-z taps, one sqrt for max_world_scale, no sqrts in the cone test
// the old path expanded the sphere to its cube and projected eight corners which dominated the dense foliage profile

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

// shared mip pick + 4-corner depth gather, the box uvs are in full-texture [0,1] space, the scale by buffer_frame.resolution_scale
// happens here so callers stay in canonical uv coordinates
// dropping the center sample is safe because the chosen mip ensures the box fits in roughly one texel and the four corner
// reads cover every texel the box can touch after the one-texel border expansion below
float hiz_min_depth_over_box(float2 min_uv, float2 max_uv, float max_mip_level)
{
    float2 render_size;
    tex.GetDimensions(render_size.x, render_size.y);

    float2 uv_extent = max_uv - min_uv;
    float2 size_px   = uv_extent * render_size;
    float  mip       = ceil(log2(max(max(size_px.x, size_px.y), 1.0f)));
    mip              = clamp(mip, 0, max_mip_level);

    float2 mip_texel = exp2(mip) / render_size;
    min_uv = saturate(min_uv - mip_texel);
    max_uv = saturate(max_uv + mip_texel);

    float4 scaled_uvs = float4(min_uv, max_uv) * buffer_frame.resolution_scale;
    float d0 = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xy, mip).r;
    float d1 = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zy, mip).r;
    float d2 = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.xw, mip).r;
    float d3 = tex.SampleLevel(GET_SAMPLER(sampler_point_clamp), scaled_uvs.zw, mip).r;
    return min(min(d0, d1), min(d2, d3));
}

// fast analytical hi-z for a world-space sphere
// projects the center once and derives a conservative ndc rectangle from the row-axis sensitivities of view_projection
// this replaces the per-thread 8-corner cube projection in the meshlet path, the bound is also tighter than the cube
// projection that surrounds the sphere, so it culls more aggressively and chooses a smaller hi-z footprint
bool sphere_hiz_visible(float3 center_world, float radius_world, float max_mip_level)
{
    matrix vp = buffer_frame.view_projection;

    // partials of clip-space output w.r.t. world-space input, wave uniform so dxc keeps these in scalar registers
    float3 ax_x = float3(vp._m00, vp._m10, vp._m20);
    float3 ax_y = float3(vp._m01, vp._m11, vp._m21);
    float3 ax_z = float3(vp._m02, vp._m12, vp._m22);
    float3 ax_w = float3(vp._m03, vp._m13, vp._m23);

    float ax_x_len = length(ax_x);
    float ax_y_len = length(ax_y);
    float ax_z_len = length(ax_z);
    float ax_w_len = length(ax_w);

    float cx = dot(center_world, ax_x) + vp._m30;
    float cy = dot(center_world, ax_y) + vp._m31;
    float cz = dot(center_world, ax_z) + vp._m32;
    float cw = dot(center_world, ax_w) + vp._m33;

    float rx = radius_world * ax_x_len;
    float ry = radius_world * ax_y_len;
    float rz = radius_world * ax_z_len;
    float rw = radius_world * ax_w_len;

    // sphere entirely behind the camera, side-frustum has already rejected this so this is a paranoia branch
    if (cw + rw <= 0.0f)
        return false;

    // sphere straddles the near plane, the perspective divide is unstable so skip occlusion conservatively
    if (cw - rw <= 0.0f)
        return true;

    // each ndc extreme is one of four (numerator extreme) * (1 / denominator extreme), enumerate and reduce
    float inv_w_close = 1.0f / (cw - rw);
    float inv_w_far   = 1.0f / (cw + rw);

    float xlc = (cx - rx) * inv_w_close;
    float xlf = (cx - rx) * inv_w_far;
    float xhc = (cx + rx) * inv_w_close;
    float xhf = (cx + rx) * inv_w_far;
    float ylc = (cy - ry) * inv_w_close;
    float ylf = (cy - ry) * inv_w_far;
    float yhc = (cy + ry) * inv_w_close;
    float yhf = (cy + ry) * inv_w_far;

    float2 min_ndc = float2(min(min(xlc, xlf), min(xhc, xhf)), min(min(ylc, ylf), min(yhc, yhf)));
    float2 max_ndc = float2(max(max(xlc, xlf), max(xhc, xhf)), max(max(ylc, ylf), max(yhc, yhf)));

    // closest sphere depth in reverse-z is max numerator over min positive denominator
    float closest_box_z = (cz + rz) * inv_w_close;

    if (max_ndc.x < -1.0f || min_ndc.x > 1.0f || max_ndc.y < -1.0f || min_ndc.y > 1.0f)
        return false;

    float2 uv_a   = saturate(ndc_to_uv(min_ndc));
    float2 uv_b   = saturate(ndc_to_uv(max_ndc));
    float2 min_uv = min(uv_a, uv_b);
    float2 max_uv = max(uv_a, uv_b);

    float furthest_z = hiz_min_depth_over_box(min_uv, max_uv, max_mip_level);
    return closest_box_z > furthest_z - 0.01f;
}

// hi-z test for a world-space aabb, kept for the skinned and hw-instanced fallback paths
// the hot per-meshlet case goes through sphere_hiz_visible above which is several times cheaper
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

    float furthest_z = hiz_min_depth_over_box(min_uv, max_uv, max_mip_level);
    return closest_box_z > furthest_z - 0.01f;
}

// largest world-axis scale of the upper 3x3, used to lift the local-space meshlet radius into world units
// computes squared lengths first and only sqrt the winner, shaves two of the three sqrts in the hot per-task loop
float max_world_scale(float4x4 m)
{
    float3 r0 = float3(m._m00, m._m01, m._m02);
    float3 r1 = float3(m._m10, m._m11, m._m12);
    float3 r2 = float3(m._m20, m._m21, m._m22);
    float sx_sq = dot(r0, r0);
    float sy_sq = dot(r1, r1);
    float sz_sq = dot(r2, r2);
    return sqrt(max(sx_sq, max(sy_sq, sz_sq)));
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

            // per-instance distance cull, the cpu Render::UpdateFrustumAndDistanceCulling can only test the whole renderable bbox
            // which always passes for consolidated world-spanning entities (forest trees, rocks), so the per-instance translation has
            // to be re-tested here, max_render_distance_squared == 0 disables the check (used for terrain tiles and anything else with
            // a non-finite max distance), skipping the survivor write here is the difference between a few hundred thousand and several
            // million triangles hitting the visible_triangles buffer for a dense forest, which directly controls whether distant
            // terrain keeps its slots in the wave atomic race
            bool passes_distance = true;
            if (draw.max_render_distance_squared > 0.0f)
            {
                float3 instance_pos     = float3(world_xform._m30, world_xform._m31, world_xform._m32);
                float3 to_camera        = instance_pos - buffer_frame.camera_position;
                float  distance_squared = dot(to_camera, to_camera);
                passes_distance         = distance_squared <= draw.max_render_distance_squared;
            }

            MeshletBounds mb     = meshlet_bounds[task.meshlet_index];
            // dequantize the compressed bounds against the lod aabb that the cpu packer used in build_meshlets
            float3 center_local  = meshlet_decode_center(mb, draw.lod_aabb_min, draw.lod_aabb_extent);
            float  radius_local  = meshlet_decode_radius(mb, draw.lod_aabb_diag);
            float3 center_world  = mul(float4(center_local, 1.0f), world_xform).xyz;
            float  scale_max     = max_world_scale(world_xform);
            float  radius_world  = radius_local * scale_max;

            // per-meshlet side-frustum sphere reject, gated on the distance cull above so out-of-range instances cannot resurrect
            is_visible = passes_distance && sphere_in_side_planes(center_world, radius_world, plane_l, plane_r, plane_b, plane_t);

            // per-meshlet backface cone, skipped for two-sided materials (the triangle cull would have to keep both sides anyway) and degenerate cones
            // skinned never reaches here, per_instance reaches here with the correct per-instance rotation baked into world_xform
            // sqrt-free form: visible if lhs <= radius_world (rhs already exceeds lhs), otherwise square both sides since both are nonneg
            if (is_visible && !is_two_sided)
            {
                int4 cone = unpack_cone_axis_cutoff(mb.cone_axis_cutoff);
                if (cone.w < 127)
                {
                    float3 cone_axis_local = float3(cone.xyz) / 127.0f;
                    float  cone_cutoff     = float(cone.w) / 127.0f;
                    float3 cone_axis_world = normalize(mul(cone_axis_local, (float3x3)world_xform));
                    float3 to_meshlet      = center_world - buffer_frame.camera_position;
                    float  to_meshlet_sq   = dot(to_meshlet, to_meshlet);
                    float  lhs             = dot(to_meshlet, cone_axis_world);
                    float  lhs_off         = lhs - radius_world;
                    if (lhs_off > 0.0f && lhs_off * lhs_off >= cone_cutoff * cone_cutoff * to_meshlet_sq)
                        is_visible = false;
                }
            }

            // per-meshlet hi-z, analytical sphere projection, the bound is tighter than the cube that surrounds the sphere
            if (is_visible)
            {
                is_visible = sphere_hiz_visible(center_world, radius_world, max_mip_level);
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
