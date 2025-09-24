/*
Copyright(c) 2015-2025 Panos Karabelas

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
        float hdr_white_point;

        math::Vector3 camera_position_previous;
        float resolution_scale;

        double time;
        float camera_fov;
        float padding;

        math::Vector3 wind;
        float gamma;

        math::Vector3 camera_right;
        float camera_exposure;

        void set_bit(const bool set, const uint32_t bit)
        {
            options = set ? (options |= bit) : (options & ~bit);
        }
    };

    // 128 byte push constant buffer - updates per pass/draw
    struct Pcb_Pass
    {
        math::Matrix transform = math::Matrix::Identity;
        math::Matrix m_value   = math::Matrix::Identity;

        void set_transform_previous(const math::Matrix& transform_previous)
        {
            m_value = transform_previous;
        }

        void set_f2_value(float x, float y)
        {
            m_value.m23 = x;
            m_value.m30 = y;
        }

        void set_f3_value(const math::Vector3& value)
        {
            m_value.m00 = value.x;
            m_value.m01 = value.y;
            m_value.m02 = value.z;
        };

        void set_f3_value(const float x, const float y = 0.0f, const float z = 0.0f)
        {
            m_value.m00 = x;
            m_value.m01 = y;
            m_value.m02 = z;
        };

        void set_f3_value2(const math::Vector3& value)
        {
            m_value.m20 = value.x;
            m_value.m21 = value.y;
            m_value.m31 = value.z;
        };

        void set_f3_value2(const float x, const float y, const float z)
        {
            m_value.m20 = x;
            m_value.m21 = y;
            m_value.m31 = z;
        };

        void set_f4_value(const Color& color)
        {
            m_value.m10 = color.r;
            m_value.m11 = color.g;
            m_value.m12 = color.b;
            m_value.m33 = color.a;
        };

        void set_f4_value(const float x, const float y, const float z, const float w)
        {
            m_value.m10 = x;
            m_value.m11 = y;
            m_value.m12 = z;
            m_value.m33 = w;
        };

        void set_is_transparent_and_material_index(const bool is_transparent, const uint32_t material_index = 0)
        {
            m_value.m03 = static_cast<float>(material_index);
            m_value.m13 = is_transparent ? 1.0f : 0.0f;
        }
    };

    struct Sb_Material
    {
        math::Vector4 color = math::Vector4::Zero;

        math::Vector2 tiling_uv = math::Vector2::Zero;
        math::Vector2 offset_uv = math::Vector2::Zero;

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

        struct OceanParameters
        {
            float scale;
            float spreadBlend;
            float swell;
            float gamma;
            float shortWavesFade;

            float windDirection;
            float fetch;
            float windSpeed;
            float repeatTime;
            float angle;
            float alpha;
            float peakOmega;

            float depth;
            float lowCutoff;
            float highCutoff;

            float foamDecayRate;
            float foamBias;
            float foamThreshold;
            float foamAdd;

            float displacementScale;
            float slopeScale;
            float lengthScale;
        } jonswap_parameters;
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
}
