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
#include "..\Math\Vector2.h"
#include "..\Math\Vector3.h"
#include "..\Math\Matrix.h"
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
        float camera_near;
        float camera_far;
    
        Math::Vector3 camera_position;
        float bloom_intensity;

        float sharpen_strength;
        Math::Vector3 camera_direction;

        float sharpen_clamp;
        float motion_blur_strength;
        float gamma;
        float tonemapping;

        float exposure;
        float directional_light_intensity;
        float ssr_enabled;
        float shadow_resolution;

        Math::Vector2 taa_jitter_offset_previous;
        Math::Vector2 taa_jitter_offset;
    };
    
    // Medium frequency - Updates a few dozen times
    struct BufferUber
    {
        Math::Matrix transform;

        Math::Vector4 mat_albedo;
    
        Math::Vector2 mat_tiling_uv;
        Math::Vector2 mat_offset_uv;
    
        float mat_roughness_mul;
        float mat_metallic_mul;
        float mat_normal_mul;
        float mat_height_mul;
    
        Math::Vector4 color;
    
        Math::Vector3 transform_axis;
        float blur_sigma;
    
        Math::Vector2 blur_direction;
        Math::Vector2 resolution;

        bool operator==(const BufferUber& rhs) const
        {
            return
                transform           == rhs.transform            &&
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
                resolution          == rhs.resolution;
        }
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
    };
    
    // Light buffer
    struct BufferLight
    {
        Math::Matrix view_projection[6];
        Math::Vector4 intensity_range_angle_bias;
        Math::Vector4 normalBias_shadow_volumetric_contact;
        Math::Vector4 color;
        Math::Vector4 position;
        Math::Vector4 direction;
    
        bool operator==(const BufferLight& rhs)
        {
            return
                view_projection                         == rhs.view_projection                      &&
                intensity_range_angle_bias              == rhs.intensity_range_angle_bias           &&
                normalBias_shadow_volumetric_contact    == rhs.normalBias_shadow_volumetric_contact &&
                color                                   == rhs.color                                &&
                position                                == rhs.position                             &&
                direction                               == rhs.direction;
        }
    };
}
