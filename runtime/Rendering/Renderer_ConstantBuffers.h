/*
Copyright(c) 2016-2023 Panos Karabelas

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

namespace Spartan
{
    // Low frequency buffer - Updates once per frame
    struct Cb_Frame
    {
        Math::Matrix view;
        Math::Matrix projection;
        Math::Matrix projection_inverted;
        Math::Matrix projection_ortho;
        Math::Matrix view_projection;
        Math::Matrix view_projection_inv;
        Math::Matrix view_projection_ortho;
        Math::Matrix view_projection_unjittered;
        Math::Matrix view_projection_previous;

        float delta_time;
        float time;
        uint32_t frame;
        float camera_aperture;

        float camera_shutter_speed;
        float camera_iso;
        float camera_near;
        float camera_far;

        Math::Vector3 camera_position;
        float bloom_intensity;

        float sharpness;
        Math::Vector3 camera_direction;

        float gamma;
        float tonemapping;
        float directional_light_intensity;
        float shadow_resolution;

        Math::Vector2 resolution_render;
        Math::Vector2 resolution_output;

        Math::Vector2 taa_jitter_current;
        Math::Vector2 taa_jitter_previous;

        float fog;
        uint32_t options;
        uint32_t frame_mip_count;
        uint32_t ssr_mip_count;

        Math::Vector2 resolution_environment;
        float exposure;
        float luminance_min;

        //float luminance_max;
        //float paper_white;
        //Math::Vector2 padding;

        void set_bit(const bool set, const uint32_t bit)
        {
            options = set ? (options |= bit) : (options & ~bit);
        }

        bool operator==(const Cb_Frame& rhs) const
        {
            return
                view                        == rhs.view                        &&
                projection                  == rhs.projection                  &&
                projection_inverted         == rhs.projection_inverted         &&
                projection_ortho            == rhs.projection_ortho            &&
                view_projection             == rhs.view_projection             &&
                view_projection_inv         == rhs.view_projection_inv         &&
                view_projection_ortho       == rhs.view_projection_ortho       &&
                view_projection_unjittered  == rhs.view_projection_unjittered  &&
                view_projection_previous    == rhs.view_projection_previous    &&
                delta_time                  == rhs.delta_time                  &&
                time                        == rhs.time                        &&
                frame                       == rhs.frame                       &&
                camera_aperture             == rhs.camera_aperture             &&
                camera_shutter_speed        == rhs.camera_shutter_speed        &&
                camera_iso                  == rhs.camera_iso                  &&
                camera_near                 == rhs.camera_near                 &&
                camera_far                  == rhs.camera_far                  &&
                camera_position             == rhs.camera_position             &&
                sharpness                   == rhs.sharpness                   &&
                camera_direction            == rhs.camera_direction            &&
                gamma                       == rhs.gamma                       &&
                tonemapping                 == rhs.tonemapping                 &&
                directional_light_intensity == rhs.directional_light_intensity &&
                shadow_resolution           == rhs.shadow_resolution           &&
                fog                         == rhs.fog                         &&
                resolution_output           == rhs.resolution_output           &&
                resolution_render           == rhs.resolution_render           &&
                taa_jitter_current          == rhs.taa_jitter_current          &&
                taa_jitter_previous         == rhs.taa_jitter_previous         &&
                options                     == rhs.options                     &&
                frame_mip_count             == rhs.frame_mip_count             &&
                ssr_mip_count               == rhs.ssr_mip_count               &&
                resolution_environment      == rhs.resolution_environment      &&
                exposure                    == rhs.exposure                    &&
                luminance_min               == rhs.luminance_min;
                //luminance_max               == rhs.luminance_max               &&
                //paper_white                 == rhs.paper_white;
        }

        bool operator!=(const Cb_Frame& rhs) const { return !(*this == rhs); }
    };
    
    // High frequency - Updates like crazy
    struct Cb_Uber
    {
        Math::Matrix transform          = Math::Matrix::Identity;
        Math::Matrix transform_previous = Math::Matrix::Identity;

        float blur_radius            = 5.0f;
        float blur_sigma             = 0.0f;
        Math::Vector2 blur_direction = Math::Vector2::Zero;

        Math::Vector2 resolution_rt  = Math::Vector2::Zero;
        Math::Vector2 resolution_in  = Math::Vector2::Zero;

        bool mat_single_texture_rougness_metalness = false;
        float radius                               = 0.0f;
        Math::Vector2 padding                      = Math::Vector2::Zero;

        Math::Vector4 mat_color = Math::Vector4::Zero;

        Math::Vector2 mat_tiling_uv = Math::Vector2::Zero;
        Math::Vector2 mat_offset_uv = Math::Vector2::Zero;

        float mat_roughness_mul = 0.0f;
        float mat_metallic_mul  = 0.0f;
        float mat_normal_mul    = 0.0f;
        float mat_height_mul    = 0.0f;

        uint32_t mat_id              = 0;
        uint32_t mat_textures        = 0;
        uint32_t is_transparent_pass = 0;
        uint32_t mip_count           = 0;

        Math::Vector3 extents     = Math::Vector3::Zero;
        uint32_t work_group_count = 0;

        uint32_t reflection_proble_available = 0;
        Math::Vector3 position               = Math::Vector3::Zero;

        bool operator==(const Cb_Uber& rhs) const
        {
            return
                transform                             == rhs.transform                             &&
                transform_previous                    == rhs.transform_previous                    &&
                mat_id                                == rhs.mat_id                                &&
                mat_color                             == rhs.mat_color                             &&
                mat_tiling_uv                         == rhs.mat_tiling_uv                         &&
                mat_offset_uv                         == rhs.mat_offset_uv                         &&
                mat_roughness_mul                     == rhs.mat_roughness_mul                     &&
                mat_metallic_mul                      == rhs.mat_metallic_mul                      &&
                mat_normal_mul                        == rhs.mat_normal_mul                        &&
                mat_height_mul                        == rhs.mat_height_mul                        &&
                blur_radius                           == rhs.blur_radius                           &&
                blur_sigma                            == rhs.blur_sigma                            &&
                blur_direction                        == rhs.blur_direction                        &&
                is_transparent_pass                   == rhs.is_transparent_pass                   &&
                resolution_rt                         == rhs.resolution_rt                         &&
                resolution_in                         == rhs.resolution_in                         &&
                mip_count                             == rhs.mip_count                             &&
                work_group_count                      == rhs.work_group_count                      &&
                reflection_proble_available           == rhs.reflection_proble_available           &&
                radius                                == rhs.radius                                &&
                extents                               == rhs.extents                               &&
                mat_textures                          == rhs.mat_textures                          &&
                mat_single_texture_rougness_metalness == rhs.mat_single_texture_rougness_metalness &&
                position                              == rhs.position;
        }

        bool operator!=(const Cb_Uber& rhs) const { return !(*this == rhs); }
    };
    
    // Light buffer
    struct Cb_Light
    {
        Math::Matrix view_projection[6];
        Math::Vector4 intensity_range_angle_bias;
        Color color;
        Math::Vector4 position;
        Math::Vector4 direction;
        float normal_bias;
        uint32_t options;
        Math::Vector2 padding;
    
        bool operator==(const Cb_Light& rhs)
        {
            return
                view_projection[0]         == rhs.view_projection[0]         &&
                view_projection[1]         == rhs.view_projection[1]         &&
                view_projection[2]         == rhs.view_projection[2]         &&
                view_projection[3]         == rhs.view_projection[3]         &&
                view_projection[4]         == rhs.view_projection[4]         &&
                view_projection[5]         == rhs.view_projection[5]         &&
                intensity_range_angle_bias == rhs.intensity_range_angle_bias &&
                normal_bias                == rhs.normal_bias                &&
                color                      == rhs.color                      &&
                position                   == rhs.position                   &&
                direction                  == rhs.direction                  &&
                options                    == rhs.options;
        }
    };

    // Material buffer
    static const uint32_t m_max_material_instances = 1024; // must match common_buffers.hlsl
    struct _material
    {
        Math::Vector4 clearcoat_clearcoatRough_anis_anisRot;
        Math::Vector4 sheen_sheenTint_pad;

        bool operator==(const _material& rhs) const
        {
            return clearcoat_clearcoatRough_anis_anisRot == rhs.clearcoat_clearcoatRough_anis_anisRot
                && sheen_sheenTint_pad == rhs.sheen_sheenTint_pad;
        }
    };
    struct Cb_Material
    {
        std::array<_material, m_max_material_instances> materials;

        bool operator==(const Cb_Material& rhs) const
        {
            return std::equal(materials.begin(), materials.end(), rhs.materials.begin(), rhs.materials.end());
        }

        bool operator!=(const Cb_Material& rhs) const { return !(*this == rhs); }
    };

    // High frequency - update multiply times per frame, ImGui driven
    struct Cb_ImGui
    {
        Math::Matrix transform                 = Math::Matrix::Identity;

        uint32_t options_texture_visualisation = 0;
        uint32_t mip_level                     = 0;
        Math::Vector2 padding                  = Math::Vector2::Zero;

        bool operator==(const Cb_ImGui& rhs) const
        {
            return
                transform                     == rhs.transform                     &&
                options_texture_visualisation == rhs.options_texture_visualisation &&
                mip_level                     == rhs.mip_level;
        }
    };
}
