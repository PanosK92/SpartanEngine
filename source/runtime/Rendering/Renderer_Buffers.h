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

#pragma once

//= INCLUDES ===============
#include "../Math/Vector2.h"
#include "../Math/Vector3.h"
#include "../Math/Matrix.h"
#include "Color.h"
//==========================

namespace spartan
{
    // low frequency - updates once per frame
    struct Cb_Frame
    {
        math::Matrix view;
        math::Matrix view_inv;
        math::Matrix view_previous;
        math::Matrix projection;
        math::Matrix projection_inv;
        math::Matrix projection_previous;
        math::Matrix view_projection;
        math::Matrix view_projection_inv;
        math::Matrix view_projection_ortho;
        math::Matrix view_projection_unjittered;
        math::Matrix view_projection_previous;
        math::Matrix view_projection_previous_unjittered;

        math::Vector2 resolution_render;
        math::Vector2 resolution_output;

        math::Vector2 taa_jitter_current;
        math::Vector2 taa_jitter_previous;

        float camera_aperture;
        float delta_time;
        uint32_t frame;
        uint32_t options;

        math::Vector3 camera_position;
        float camera_near;

        math::Vector3 camera_forward;
        float camera_far;

        float camera_last_movement_time;
        float hdr_enabled;
        float hdr_max_nits;
        float padding;

        math::Vector3 camera_position_previous;
        float resolution_scale;

        double time;
        float camera_fov;
        float padding2;

        math::Vector3 wind;
        float gamma;

        math::Vector3 camera_right;
        float camera_exposure;

        // clouds
        float cloud_coverage;
        float cloud_shadows;
        float padding3;
        float padding4;

        void set_bit(const bool set, const uint32_t bit)
        {
            options = set ? (options |= bit) : (options & ~bit);
        }
    };

    // push constant buffer - updates per pass/draw
    // draw_index references a Sb_DrawData entry in the bindless draw data buffer,
    // which holds per-draw transforms and material info. material_index and
    // is_transparent are pass-level state used by compute shaders (lighting, composition)
    // that don't have per-draw data. the float array carries generic per-pass parameters.
    struct Pcb_Pass
    {
        uint32_t draw_index     = 0;
        uint32_t material_index = 0;
        uint32_t is_transparent = 0;
        uint32_t padding        = 0;

        // generic per-pass parameters, laid out as 3 x float4:
        // v[0..2]  = f3_value  (e.g. light count, fog, mip level)
        // v[3]     = f2_value.x
        // v[4..6]  = f3_value2 (e.g. light index, texel size)
        // v[7]     = f2_value.y
        // v[8..11] = f4_value  (e.g. light coordinate, color)
        float v[12] = {};

        void set_f3_value(const math::Vector3& value) { v[0] = value.x; v[1] = value.y; v[2] = value.z; }
        void set_f3_value(float x, float y = 0.0f, float z = 0.0f) { v[0] = x; v[1] = y; v[2] = z; }

        void set_f3_value2(const math::Vector3& value) { v[4] = value.x; v[5] = value.y; v[6] = value.z; }
        void set_f3_value2(float x, float y, float z) { v[4] = x; v[5] = y; v[6] = z; }

        void set_f4_value(const Color& color) { v[8] = color.r; v[9] = color.g; v[10] = color.b; v[11] = color.a; }
        void set_f4_value(float x, float y, float z, float w) { v[8] = x; v[9] = y; v[10] = z; v[11] = w; }

        void set_f2_value(float x, float y) { v[3] = x; v[7] = y; }
    };

    struct Sb_Material
    {
        math::Vector4 color = math::Vector4::Zero;

        math::Vector2 tiling_uv = math::Vector2::Zero;
        math::Vector2 offset_uv = math::Vector2::Zero;
        math::Vector2 invert_uv = math::Vector2::Zero;

        float roughness_mul = 0.0f;
        float metallic_mul  = 0.0f;
        float normal_mul    = 0.0f;
        float height_mul    = 0.0f;

        uint32_t flags    = 0;
        float local_width = 0.0f;
        float padding;
        float subsurface_scattering;

        float sheen;
        float local_height   = 0.0f;
        float world_space_uv = 0.0f;
        float padding2;

        float anisotropic;
        float anisotropic_rotation;
        float clearcoat;
        float clearcoat_roughness;
    };

    struct Sb_Light
    {
        Color color;
        math::Vector3 position;
        float intensity;
        math::Vector3 direction;
        float range;
        float angle;
        uint32_t flags;
        uint32_t screen_space_shadows_slice_index;
        float area_width;  // area light width in meters
        float area_height; // area light height in meters
        math::Matrix view_projection[6];
        math::Vector2 atlas_offsets[6];
        math::Vector2 atlas_scales[6];
        math::Vector2 atlas_texel_sizes[6];
    };

    struct Sb_Aabb
    {
        math::Vector3 min;
        float is_occluder;
        math::Vector3 max;
        float padding2;
    };

    // ray tracing geometry info for vertex/index buffer access
    struct Sb_GeometryInfo
    {
        uint64_t vertex_buffer_address;
        uint64_t index_buffer_address;
        uint32_t vertex_offset;
        uint32_t index_offset;
        uint32_t vertex_count;
        uint32_t index_count;
    };

    // gpu-driven indirect draw arguments (matches VkDrawIndexedIndirectCommand layout)
    struct Sb_IndirectDrawArgs
    {
        uint32_t index_count    = 0;
        uint32_t instance_count = 0;
        uint32_t first_index    = 0;
        int32_t  vertex_offset  = 0;
        uint32_t first_instance = 0;
    };

    // per-draw data for gpu-driven rendering (indexed by draw_id in shaders)
    struct Sb_DrawData
    {
        math::Matrix transform;          // current world transform
        math::Matrix transform_previous; // previous frame world transform
        uint32_t material_index = 0;     // index into the bindless material parameters array
        uint32_t is_transparent = 0;     // transparency flag
        uint32_t aabb_index     = 0;     // index into the aabb buffer for culling
        uint32_t padding        = 0;
    };
}
