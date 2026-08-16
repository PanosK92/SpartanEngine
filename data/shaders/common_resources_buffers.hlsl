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

#ifndef SPARTAN_COMMON_RESOURCES_BUFFERS
#define SPARTAN_COMMON_RESOURCES_BUFFERS

#include "shared_buffers.h"
#include "common_resources_bindless.hlsl"

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
matrix get_view_projection_previous_unjittered() { return pass_is_right_eye() ? buffer_frame.view_projection_previous_unjittered_right : buffer_frame.view_projection_previous_unjittered; }
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

#endif
