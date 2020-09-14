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

// Low frequency - Updates once per frame
cbuffer BufferFrame : register(b0)
{
    matrix g_view;
    matrix g_projection;
    matrix g_projection_orthographic;
    matrix g_view_projection;
    matrix g_view_projection_inverted;
    matrix g_view_projection_orthographic;
    matrix g_view_projection_unjittered;

    float g_delta_time;
    float g_time;
    uint g_frame;   
    float g_camera_aperture;
    
    float g_camera_shutter_speed;
    float g_camera_iso;
    float g_camera_near;
    float g_camera_far;

    float3 g_camera_position;
    float g_bloom_intensity;
    
    float g_sharpen_strength;   
    float3 g_camera_direction;
    
    float g_gamma;
    float g_toneMapping;
    float g_directional_light_intensity;
    float g_ssr_enabled;
    
    float g_shadow_resolution;
    float g_fog_density;
    float2 g_padding;

    float2 g_taa_jitter_offset_previous;
    float2 g_taa_jitter_offset;
};

// Low frequency - Updates once per frame
static const int g_max_materials = 1024;
cbuffer BufferMaterial : register(b1)
{
    float4 mat_clearcoat_clearcoatRough_aniso_anisoRot[g_max_materials];
    float4 mat_sheen_sheenTint_pad[g_max_materials];
}

// Medium frequency - Updates per render pass
cbuffer BufferUber : register(b2)
{
    matrix g_transform;

    float4 g_color;
    
    float3 g_transform_axis;
    float g_blur_sigma;
    
    float2 g_blur_direction;
    float2 g_resolution;

    float4 g_mat_color;

    float2 g_mat_tiling;
    float2 g_mat_offset;

    float g_mat_roughness;
    float g_mat_metallic;
    float g_mat_normal;
    float g_mat_height;

    float g_mat_id;
    float g_mip_index;
    float2 g_padding2;
};

// High frequency - Updates per object
cbuffer BufferObject : register(b3)
{
    matrix g_object_transform;
    matrix g_object_wvp_current;
    matrix g_object_wvp_previous;
};

// High frequency - Updates per light
cbuffer LightBuffer : register(b4)
{
    matrix cb_light_view_projection[6];
    float4 cb_light_intensity_range_angle_bias;
    float3 cb_light_color;
    float cb_light_normal_bias;
    float4 cb_light_position;
    float4 cb_light_direction;
};
