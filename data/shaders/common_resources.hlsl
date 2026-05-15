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

#ifndef SPARTAN_COMMON_RESOURCES
#define SPARTAN_COMMON_RESOURCES

#include "shared_buffers.h"

// g-buffer
Texture2D tex_albedo   : register(t0);
Texture2D tex_normal   : register(t1);
Texture2D tex_material : register(t2);
Texture2D tex_velocity : register(t3);
Texture2D tex_depth    : register(t4);

// ray-tracing
RaytracingAccelerationStructure tlas : register(t5);

// other
Texture2D tex_ssao : register(t6);

// misc
Texture2D tex   : register(t7);
Texture2D tex2  : register(t8);
Texture2D tex3  : register(t9);
Texture2D tex4  : register(t10);
Texture2D tex5  : register(t11);
Texture2D tex6  : register(t12);
Texture3D tex3d : register(t13);

// noise
Texture2D tex_perlin : register(t14);

// volumetric cloud 3D noise textures
Texture3D tex3d_cloud_shape  : register(t20); // 128^3 Perlin-Worley + Worley FBM
Texture3D tex3d_cloud_detail : register(t21); // 32^3 high-frequency detail
// restir reservoir textures (shared across path tracing, temporal, and spatial passes)
Texture2D<float4> tex_reservoir_prev0 : register(t22);
Texture2D<float4> tex_reservoir_prev1 : register(t23);
Texture2D<float4> tex_reservoir_prev2 : register(t24);
Texture2D<float4> tex_reservoir_prev3 : register(t25);
Texture2D<float4> tex_reservoir_prev4 : register(t26);

// wind field, baked once per frame, sampled by all wind-driven geometry
// rg = flow vector (signed, [-1,1]), b = gust pressure (0..1), a = micro turbulence (0..1)
Texture2D<float4> tex_wind_field : register(t27);

// geometry info buffer for ray tracing (per-blas-instance offsets)
RWStructuredBuffer<GeometryInfo> geometry_infos : register(u20);

// restir reservoir uav bindings
RWTexture2D<float4> tex_reservoir0 : register(u21);
RWTexture2D<float4> tex_reservoir1 : register(u22);
RWTexture2D<float4> tex_reservoir2 : register(u23);
RWTexture2D<float4> tex_reservoir3 : register(u24);
RWTexture2D<float4> tex_reservoir4 : register(u25);

// bindless arrays
Texture2D material_textures[]                            : register(t15, space1);
StructuredBuffer<MaterialParameters> material_parameters : register(t16, space2);
StructuredBuffer<LightParameters> light_parameters       : register(t17, space3);
StructuredBuffer<aabb> aabbs                             : register(t18, space4);
SamplerComparisonState samplers_comparison[]             : register(s0,  space6);
SamplerState samplers[]                                  : register(s1,  space7);

// storage textures/buffers (image_format unknown allows flexible format binding)
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav                           : register(u0);
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav2                          : register(u1);
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav3                          : register(u2);
[[vk::image_format("unknown")]] RWTexture2D<float4> tex_uav4                          : register(u3);
[[vk::image_format("unknown")]] RWTexture3D<float4> tex3d_uav                         : register(u4);
[[vk::image_format("unknown")]] RWTexture2DArray<float4> tex_uav_sss                  : register(u5);
RWStructuredBuffer<uint> visibility                                                   : register(u6); // unused, kept for descriptor layout stability
globallycoherent RWStructuredBuffer<uint> g_atomic_counter                            : register(u7); // used by FidelityFX SPD
[[vk::image_format("unknown")]] globallycoherent RWTexture2D<float4> tex_uav_mips[12] : register(u8); // used by FidelityFX SPD
// integer format textures (vrs, etc)
RWTexture2D<uint> tex_uav_uint : register(u30);

// bindless draw data - per-draw transforms, material indices, etc.
StructuredBuffer<DrawData> draw_data                     : register(t19, space5);

StructuredBuffer<PulledVertex> geometry_vertices    : register(t20, space8);
StructuredBuffer<uint> geometry_indices             : register(t22, space9);
StructuredBuffer<PackedInstance> geometry_instances : register(t23, space10);

// vertex attribute unpackers, must match the cpu-side encoders in rhi_vertex.h
float2 unpack_vertex_uv(uint packed)
{
    return f16tof32(uint2(packed & 0xFFFFu, packed >> 16));
}

float3 unpack_vertex_oct(uint packed)
{
    // sign-extend snorm16 lanes into [-1, 1]
    int sx   = (int)(packed << 16) >> 16;
    int sy   = (int)packed >> 16;
    float2 f = max(float2(sx, sy) * (1.0f / 32767.0f), -1.0f);

    // branchless octahedral wrap (rune stubbe form)
    float3 n = float3(f, 1.0f - abs(f.x) - abs(f.y));
    float t  = max(-n.z, 0.0f);
    n.x     += n.x >= 0.0f ? -t : t;
    n.y     += n.y >= 0.0f ? -t : t;
    return normalize(n);
}

// gpu-driven indirect drawing uav bindings
// indirect_draw_args is a single-slot buffer used as the args for the final non-indexed indirect draw
//   slot 0 layout matches VkDrawIndirectCommand for the first 16 bytes: vertex_count, instance_count, first_vertex, first_instance
//   the cpu primes instance_count = 1 each frame, vertex_count is atomically bumped by triangle cull (in 3-vertex steps)
RWStructuredBuffer<IndirectDrawArgs> indirect_draw_args : register(u31);
RWStructuredBuffer<DrawData> indirect_draw_data         : register(u32);
// meshlet_instances is the meshlet-cull survivor list, the triangle cull dispatches one workgroup per entry
RWStructuredBuffer<MeshletInstance> meshlet_instances   : register(u33);
// visible_triangles is the triangle-cull survivor list, packed via VISIBLE_TRI_PACK
RWStructuredBuffer<uint> visible_triangles              : register(u34);
// triangle_dispatch_args is the indirect dispatch args for the triangle cull, group_count_x is the meshlet survivor count
RWStructuredBuffer<IndirectDispatchArgs> triangle_dispatch_args : register(u35);

RWStructuredBuffer<Particle>      particle_buffer_a : register(u36);
RWStructuredBuffer<uint>          particle_counter  : register(u38);
RWStructuredBuffer<EmitterParams> particle_emitter  : register(u39);

// gpu texture compression
RWStructuredBuffer<uint>  tex_compress_in      : register(u40);
RWStructuredBuffer<uint4> tex_compress_out     : register(u41); // bc3, bc5 (16 bytes per block)
RWStructuredBuffer<uint2> tex_compress_out_bc1 : register(u42); // bc1 (8 bytes per block)

// per-meshlet bounds (read-only, declared as rw to keep slot management uniform with other indirect buffers)
RWStructuredBuffer<MeshletBounds> meshlet_bounds : register(u43);

// per-instance cull tasks (read-only, declared as rw to keep slot management uniform with other indirect buffers)
RWStructuredBuffer<CullTask> cull_tasks : register(u44);

// clustered lighting, written by light_cluster_assign and read by light
// grid is (first_index, count) per cluster, indices is the flat list of light slot ids
// single grid shared by both vr eyes, built in the left eye view-projection space, the right eye projects
// its world space samples through the same matrices for the lookup, ipd induced offset is well under one tile
RWStructuredBuffer<uint2> cluster_light_grid    : register(u45);
RWStructuredBuffer<uint>  cluster_light_indices : register(u46);

// cluster assign telemetry, currently a single overflow counter bumped when a cluster overshoots CLUSTER_MAX_LIGHTS
RWStructuredBuffer<uint> cluster_stats : register(u47);

// compact list of volumetric light indices, written by the cpu in UpdateLights, scanned by the volumetric fog loop
// declared rw for binding uniformity with the other indirect/cluster buffers, treated as read only inside the shader
RWStructuredBuffer<uint> volumetric_light_indices : register(u48);

// buffers
#ifdef API_D3D12
// d3d12 path: root 32-bit constants at b1 (vk::push_constant is ignored by dxil)
cbuffer BufferPass : register(b1) { PassBufferData buffer_pass; };
#else
[[vk::push_constant]]
PassBufferData buffer_pass;
#endif
cbuffer BufferFrame : register(b0) { FrameBufferData buffer_frame; };

// easy access to buffer_frame members
bool is_taa_enabled()                    { return any(buffer_frame.taa_jitter_current); }
bool is_ray_traced_reflections_enabled() { return buffer_frame.options & uint(1U << 0); }
bool is_ssao_enabled()                   { return buffer_frame.options & uint(1U << 1); }
bool is_ray_traced_shadows_enabled()     { return buffer_frame.options & uint(1U << 2); }
bool is_restir_pt_enabled()              { return buffer_frame.options & uint(1U << 3); }

// per-draw data is stored in a static so both vertex and pixel shaders can access it
// vertex shaders populate this from the appropriate buffer (draw_data for cpu-driven, indirect_draw_data via MeshletInstance for gpu-driven)
static DrawData _draw;

// per-draw accessors - read from the static draw data populated by the vertex shader entry point
matrix pass_get_transform()          { return _draw.transform; }
matrix pass_get_transform_previous() { return _draw.transform_previous; }
uint   pass_get_material_index()     { return _draw.material_index; }

// pass-level state - read from push constant (works in both raster and compute shaders)
bool pass_is_transparent() { return buffer_pass.is_transparent != 0; }
bool pass_is_opaque()      { return buffer_pass.is_transparent == 0; }
uint pass_get_eye_index()  { return buffer_pass.eye_index; }

// stereo/per-eye matrix selectors
// compute passes run once per eye and push buffer_pass.eye_index so view-dependent math picks
// the correct eye; raster passes with SV_ViewID can pass the view id through this path as well.
// when multiview is off the right-eye members are not populated and these collapse to the
// primary matrices.
bool is_multiview_active()            { return buffer_frame.is_multiview != 0; }
bool pass_is_right_eye()              { return is_multiview_active() && buffer_pass.eye_index == 1; }

matrix get_view()                     { return pass_is_right_eye() ? buffer_frame.view_right                     : buffer_frame.view; }
matrix get_view_inverted()            { return pass_is_right_eye() ? buffer_frame.view_inverted_right            : buffer_frame.view_inverted; }
matrix get_projection()               { return pass_is_right_eye() ? buffer_frame.projection_right               : buffer_frame.projection; }
matrix get_projection_inverted()      { return pass_is_right_eye() ? buffer_frame.projection_inverted_right      : buffer_frame.projection_inverted; }
matrix get_view_projection()          { return pass_is_right_eye() ? buffer_frame.view_projection_right          : buffer_frame.view_projection; }
matrix get_view_projection_inverted() { return pass_is_right_eye() ? buffer_frame.view_projection_inverted_right : buffer_frame.view_projection_inverted; }
matrix get_view_projection_previous() { return pass_is_right_eye() ? buffer_frame.view_projection_previous_right : buffer_frame.view_projection_previous; }
float3 get_camera_position()          { return pass_is_right_eye() ? buffer_frame.camera_position_right          : buffer_frame.camera_position; }

// explicit-view variants: used by raster pixel shaders in multiview passes where a single draw
// covers both eyes, so buffer_pass.eye_index is static and the per-fragment eye must be
// selected from the interpolated SV_ViewID propagated through the vertex payload.
bool   is_right_eye_for_view(uint view_id)            { return is_multiview_active() && view_id == 1; }
matrix get_view_for_view(uint view_id)                { return is_right_eye_for_view(view_id) ? buffer_frame.view_right                     : buffer_frame.view; }
matrix get_view_inverted_for_view(uint view_id)       { return is_right_eye_for_view(view_id) ? buffer_frame.view_inverted_right            : buffer_frame.view_inverted; }
matrix get_view_projection_for_view(uint view_id)     { return is_right_eye_for_view(view_id) ? buffer_frame.view_projection_right          : buffer_frame.view_projection; }
matrix get_view_projection_inverted_for_view(uint v)  { return is_right_eye_for_view(v)       ? buffer_frame.view_projection_inverted_right : buffer_frame.view_projection_inverted; }
float3 get_camera_position_for_view(uint view_id)     { return is_right_eye_for_view(view_id) ? buffer_frame.camera_position_right          : buffer_frame.camera_position; }

// generic pass parameter accessors - read from push constant values[]
// values[0].xyz = f3_value, values[0].w = f2_value.x
// values[1].xyz = f3_value2, values[1].w = f2_value.y
// values[2]     = f4_value
float3 pass_get_f3_value()  { return buffer_pass.values[0].xyz; }
float3 pass_get_f3_value2() { return buffer_pass.values[1].xyz; }
float4 pass_get_f4_value()  { return buffer_pass.values[2]; }
float2 pass_get_f2_value()  { return float2(buffer_pass.values[0].w, buffer_pass.values[1].w); }

// helper to populate _draw from the appropriate source
void pass_load_draw_data_from_buffer()          { _draw = draw_data[buffer_pass.draw_index]; }
void pass_load_draw_data_from_vertex(uint mi)   { _draw.material_index = mi; } // pixel shader: restore material_index from vertex output

// bindless array indices
static const uint material_texture_slots_per_type  = 4;
static const uint material_texture_index_albedo    = 0 * material_texture_slots_per_type;
static const uint material_texture_index_roughness = 1 * material_texture_slots_per_type;
static const uint material_texture_index_metalness = 2 * material_texture_slots_per_type;
static const uint material_texture_index_normal    = 3 * material_texture_slots_per_type;
static const uint material_texture_index_occlusion = 4 * material_texture_slots_per_type;
static const uint material_texture_index_emission  = 5 * material_texture_slots_per_type;
static const uint material_texture_index_height    = 6 * material_texture_slots_per_type;
static const uint material_texture_index_mask      = 7 * material_texture_slots_per_type;
static const uint material_texture_index_packed    = 8 * material_texture_slots_per_type;

static const uint sampler_compare_depth         = 0;
static const uint sampler_point_clamp           = 0;
static const uint sampler_point_clamp_border    = 1;
static const uint sampler_point_wrap            = 2;
static const uint sampler_bilinear_clamp        = 3;
static const uint sampler_bilinear_clamp_border = 4;
static const uint sampler_bilinear_wrap         = 5;
static const uint sampler_trilinear_clamp       = 6;
static const uint sampler_anisotropic_wrap      = 7;

// bindless array access
#define GET_TEXTURE(index_texture) material_textures[pass_get_material_index() + index_texture]
MaterialParameters GetMaterial() { return material_parameters[pass_get_material_index()]; }
#define GET_SAMPLER(index_sampler) samplers[index_sampler]

#endif // SPARTAN_COMMON_RESOURCES
