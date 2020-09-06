/*
Copyright(c) 2016-2020 Panos Karabelas

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
//==========================

namespace Spartan
{
    // Low frequency buffer - Updates once per frame
    struct BufferFrame
    {
        Math::Matrix view;
        Math::Matrix projection;
        Math::Matrix projection_ortho;
        Math::Matrix view_projection;
        Math::Matrix view_projection_inv;
        Math::Matrix view_projection_ortho;
        Math::Matrix view_projection_unjittered;

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

        float sharpen_strength;
        Math::Vector3 camera_direction;

        float gamma;
        float tonemapping;
        float directional_light_intensity;
        float ssr_enabled;

        float shadow_resolution;
        float fog;
        Math::Vector2 padding;

        Math::Vector2 taa_jitter_offset_previous;
        Math::Vector2 taa_jitter_offset;

        bool operator==(const BufferFrame& rhs) const
        {
            return
                view                        == rhs.view &&
                projection                  == rhs.projection &&
                projection_ortho            == rhs.projection_ortho &&
                view_projection             == rhs.view_projection &&
                view_projection_inv         == rhs.view_projection_inv &&
                view_projection_ortho       == rhs.view_projection_ortho &&
                view_projection_unjittered  == rhs.view_projection_unjittered &&
                delta_time                  == rhs.delta_time &&
                time                        == rhs.time &&
                frame                       == rhs.frame &&
                camera_aperture             == rhs.camera_aperture &&
                camera_shutter_speed        == rhs.camera_shutter_speed &&
                camera_iso                  == rhs.camera_iso &&
                camera_near                 == rhs.camera_near &&
                camera_far                  == rhs.camera_far &&
                camera_position             == rhs.camera_position &&
                sharpen_strength            == rhs.sharpen_strength &&
                camera_direction            == rhs.camera_direction &&
                gamma                       == rhs.gamma &&
                tonemapping                 == rhs.tonemapping &&
                directional_light_intensity == rhs.directional_light_intensity &&
                ssr_enabled                 == rhs.ssr_enabled &&
                shadow_resolution           == rhs.shadow_resolution &&
                fog                         == rhs.fog &&
                taa_jitter_offset_previous  == rhs.taa_jitter_offset_previous &&
                taa_jitter_offset           == rhs.taa_jitter_offset;
        }
        bool operator!=(const BufferFrame& rhs) const { return !(*this == rhs); }
    };
    
    // Low frequency buffer - Updates once per frame
    static const uint32_t m_max_material_instances = 1024; // must match the shader
    struct BufferMaterial
    {
        std::array<Math::Vector4, m_max_material_instances> mat_clearcoat_clearcoatRough_anis_anisRot;
        std::array<Math::Vector4, m_max_material_instances> mat_sheen_sheenTint_pad;

        bool operator==(const BufferMaterial& rhs) const
        {
            return
                mat_clearcoat_clearcoatRough_anis_anisRot == rhs.mat_clearcoat_clearcoatRough_anis_anisRot &&
                mat_sheen_sheenTint_pad == rhs.mat_sheen_sheenTint_pad;
        }

        bool operator!=(const BufferMaterial& rhs) const { return !(*this == rhs); }
    };

    // Medium frequency - Updates a few dozen times
    struct BufferUber
    {
        Math::Matrix transform;

        Math::Vector4 color;
    
        Math::Vector3 transform_axis;
        float blur_sigma;
    
        Math::Vector2 blur_direction;
        Math::Vector2 resolution;

        Math::Vector4 mat_albedo;

        Math::Vector2 mat_tiling_uv;
        Math::Vector2 mat_offset_uv;

        float mat_roughness_mul;
        float mat_metallic_mul;
        float mat_normal_mul;
        float mat_height_mul;

        float mat_id;
        uint32_t mip_index;
        Math::Vector2 padding;

        bool operator==(const BufferUber& rhs) const
        {
            return
                transform           == rhs.transform            &&
                mat_id              == rhs.mat_id               &&
                mat_albedo          == rhs.mat_albedo           &&
                mat_tiling_uv       == rhs.mat_tiling_uv        &&
                mat_offset_uv       == rhs.mat_offset_uv        &&
                mat_roughness_mul   == rhs.mat_roughness_mul    &&
                mat_metallic_mul    == rhs.mat_metallic_mul     &&
                mat_normal_mul      == rhs.mat_normal_mul       &&
                mat_height_mul      == rhs.mat_height_mul       &&
                color               == rhs.color                &&
                transform_axis      == rhs.transform_axis       &&
                blur_sigma          == rhs.blur_sigma           &&
                blur_direction      == rhs.blur_direction       &&
                mip_index           == rhs.mip_index            &&
                resolution          == rhs.resolution;
        }

        bool operator!=(const BufferUber& rhs) const { return !(*this == rhs); }
    };
    
    // High frequency - Updates at least as many times as there are objects in the scene
    struct BufferObject
    {
        Math::Matrix object;
        Math::Matrix wvp_current;
        Math::Matrix wvp_previous;
    
        bool operator==(const BufferObject& rhs) const
        {
            return
                object          == rhs.object       &&
                wvp_current     == rhs.wvp_current  &&
                wvp_previous    == rhs.wvp_previous;
        }

        bool operator!=(const BufferObject& rhs) const { return !(*this == rhs); }
    };
    
    // Light buffer
    struct BufferLight
    {
        Math::Matrix view_projection[6];
        Math::Vector4 intensity_range_angle_bias;
        Math::Vector3 color;
        float normal_bias;
        Math::Vector4 position;
        Math::Vector4 direction;
    
        bool operator==(const BufferLight& rhs)
        {
            return
                view_projection             == rhs.view_projection              &&
                intensity_range_angle_bias  == rhs.intensity_range_angle_bias   &&
                normal_bias                 == rhs.normal_bias                  &&
                color                       == rhs.color                        &&
                position                    == rhs.position                     &&
                direction                   == rhs.direction;
        }
    };
}
