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

// per-triangle cull, dispatched indirectly with one workgroup per surviving meshlet
// each thread handles one triangle, survivors are wave-aggregated into visible_triangles and the final draw's vertex_count is bumped by 3 * wave_survivor_count
// the per-meshlet header (mi, draw, mb, world transform) is loaded into groupshared once and reused by every thread, this is the largest scalar load saving in the kernel
// flags bit 0 skinned skip backface (deformation invalidates static normals)
// flags bit 3 two-sided material skip backface

groupshared MeshletInstance gs_mi;
groupshared DrawData        gs_draw;
groupshared MeshletBounds   gs_mb;
groupshared float4x4        gs_world_xform;
groupshared bool            gs_skip_backface;
groupshared uint            gs_triangle_count;
groupshared uint            gs_base_index_pos;

[numthreads(MESHLET_MAX_TRIANGLES, 1, 1)]
void main_cs(uint3 gid : SV_GroupID, uint3 lid : SV_GroupThreadID)
{
    uint mi_idx       = gid.x;
    uint triangle_idx = lid.x;

    uint max_meshlet_instances = (uint)pass_get_f4_value().x;
    uint max_visible_triangles = (uint)pass_get_f4_value().y;

    // header load is wave-uniform across the workgroup, one thread reads, all threads consume after the barrier
    if (lid.x == 0)
    {
        if (mi_idx < max_meshlet_instances)
        {
            gs_mi             = meshlet_instances[mi_idx];
            gs_draw           = indirect_draw_data[gs_mi.draw_index];
            gs_mb             = meshlet_bounds[gs_mi.meshlet_index];
            gs_world_xform    = mul(pull_instance_transform(gs_draw.instance_offset, gs_mi.instance_index), gs_draw.transform);
            gs_skip_backface  = ((gs_draw.flags & 1u) | (gs_draw.flags & 8u)) != 0u;
            // first_index and triangle_count come out of the compressed bounds, the helpers stay in lockstep with the cpu packer in build_meshlets
            gs_triangle_count = meshlet_decode_triangle_count(gs_mb);
            gs_base_index_pos = gs_draw.lod_first_index + meshlet_decode_first_index(gs_mb);
        }
        else
        {
            gs_triangle_count = 0u;
        }
    }
    GroupMemoryBarrierWithGroupSync();

    bool survives = false;
    uint packed   = 0u;

    if (triangle_idx < gs_triangle_count)
    {
        // load the three vertex indices for this triangle from the global index buffer
        uint base_index_pos = gs_base_index_pos + triangle_idx * 3u;
        uint i0 = geometry_indices[base_index_pos + 0u] + gs_draw.lod_vertex_offset;
        uint i1 = geometry_indices[base_index_pos + 1u] + gs_draw.lod_vertex_offset;
        uint i2 = geometry_indices[base_index_pos + 2u] + gs_draw.lod_vertex_offset;

        PulledVertex v0 = geometry_vertices[i0];
        PulledVertex v1 = geometry_vertices[i1];
        PulledVertex v2 = geometry_vertices[i2];

        // transform to world space using the cached world transform, engine convention places the vector on the left of mul
        float3 p0_world = mul(float4(v0.position, 1.0f), gs_world_xform).xyz;
        float3 p1_world = mul(float4(v1.position, 1.0f), gs_world_xform).xyz;
        float3 p2_world = mul(float4(v2.position, 1.0f), gs_world_xform).xyz;

        bool keep = true;

        if (!gs_skip_backface)
        {
            // engine convention is left-handed coords with cw front-face winding
            // for a cw-from-camera triangle in lh coords, cross(p1-p0, p2-p0) points back at the camera so a front-facing triangle has dot(face_normal, view_dir) <= 0
            float3 face_normal = cross(p1_world - p0_world, p2_world - p0_world);
            float3 view_dir    = p0_world - buffer_frame.camera_position;
            keep               = dot(face_normal, view_dir) <= 0.0f;
        }

        if (keep)
        {
            // transform to clip space for frustum + sub-pixel tests
            float4 p0_clip = mul(float4(p0_world, 1.0f), buffer_frame.view_projection);
            float4 p1_clip = mul(float4(p1_world, 1.0f), buffer_frame.view_projection);
            float4 p2_clip = mul(float4(p2_world, 1.0f), buffer_frame.view_projection);

            // drop triangles fully behind the camera
            if (p0_clip.w <= 0.0f && p1_clip.w <= 0.0f && p2_clip.w <= 0.0f)
            {
                keep = false;
            }
            else if (p0_clip.w > 0.0f && p1_clip.w > 0.0f && p2_clip.w > 0.0f)
            {
                // half-space form keeps the test stable across reverse-z and avoids the inverted compare when w flips sign
                bool out_left   = (p0_clip.x + p0_clip.w < 0.0f) && (p1_clip.x + p1_clip.w < 0.0f) && (p2_clip.x + p2_clip.w < 0.0f);
                bool out_right  = (p0_clip.w - p0_clip.x < 0.0f) && (p1_clip.w - p1_clip.x < 0.0f) && (p2_clip.w - p2_clip.x < 0.0f);
                bool out_bottom = (p0_clip.y + p0_clip.w < 0.0f) && (p1_clip.y + p1_clip.w < 0.0f) && (p2_clip.y + p2_clip.w < 0.0f);
                bool out_top    = (p0_clip.w - p0_clip.y < 0.0f) && (p1_clip.w - p1_clip.y < 0.0f) && (p2_clip.w - p2_clip.y < 0.0f);
                bool out_near   = (p0_clip.w - p0_clip.z < 0.0f) && (p1_clip.w - p1_clip.z < 0.0f) && (p2_clip.w - p2_clip.z < 0.0f);
                bool out_far    = (p0_clip.z          < 0.0f)    && (p1_clip.z          < 0.0f)    && (p2_clip.z          < 0.0f);
                if (out_left || out_right || out_bottom || out_top || out_near || out_far)
                {
                    keep = false;
                }
                else
                {
                    // sub-pixel reject only on the fully-in-front path, behind-camera vertices break the perspective divide
                    float2 s0 = p0_clip.xy / p0_clip.w;
                    float2 s1 = p1_clip.xy / p1_clip.w;
                    float2 s2 = p2_clip.xy / p2_clip.w;
                    float2 min_ndc = min(s0, min(s1, s2));
                    float2 max_ndc = max(s0, max(s1, s2));
                    float2 px      = get_render_resolution_active() * 0.5f;
                    int2   min_px  = int2(floor(min_ndc * px));
                    int2   max_px  = int2(floor(max_ndc * px));
                    if (all(min_px == max_px))
                        keep = false;
                }
            }
        }

        survives = keep;
        packed   = VISIBLE_TRI_PACK(mi_idx, triangle_idx);
    }

    // wave-aggregated atomic, one InterlockedAdd per wave instead of per surviving triangle, this is the single biggest perf win in the dense-foliage path
    uint wave_count = WaveActiveCountBits(survives);
    uint lane_off   = WavePrefixCountBits(survives);
    uint wave_base  = 0u;
    uint overflow_u = 0u;
    if (WaveIsFirstLane() && wave_count > 0u)
    {
        uint old;
        InterlockedAdd(indirect_draw_args[0].index_count, wave_count * 3u, old);
        wave_base  = old / 3u;
        overflow_u = (wave_base + wave_count) > max_visible_triangles ? 1u : 0u;
    }
    wave_base  = WaveReadLaneFirst(wave_base);
    overflow_u = WaveReadLaneFirst(overflow_u);

    if (survives)
    {
        uint triangle_slot = wave_base + lane_off;
        if (triangle_slot < max_visible_triangles)
            visible_triangles[triangle_slot] = packed;
    }

    // clamp on overflow, only the wave that crossed the cap pays the second atomic, uint avoids the bool-broadcast portability pitfall on some drivers
    if (overflow_u != 0u && WaveIsFirstLane())
        InterlockedMin(indirect_draw_args[0].index_count, max_visible_triangles * 3u);
}
