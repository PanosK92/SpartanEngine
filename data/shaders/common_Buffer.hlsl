/*
Copyright(c) 2016-2021 Panos Karabelas

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
    matrix g_projection_inverted;
    matrix g_projection_orthographic;
    matrix g_view_projection;
    matrix g_view_projection_inverted;
    matrix g_view_projection_orthographic;
    matrix g_view_projection_unjittered;
    matrix g_view_projection_previous;

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
    float g_shadow_resolution;
    
    float2 g_resolution_render;
    float2 g_resolution_output;

    float2 g_taa_jitter_current;
    float2 g_taa_jitter_previous;

    float g_fog_density;
    uint g_options;
    uint g_frame_mip_count;
    uint g_ssr_mip_count;

    float2 g_resolution_environment;
    float2 g_padding;
};

// Medium frequency - Updates per render pass
cbuffer BufferUber : register(b1)
{
    matrix g_transform;
    matrix g_transform_previous;

    float3 g_float3;
    float g_blur_sigma;
    
    float2 g_blur_direction;
    float2 g_resolution_rt;

    float2 g_resolution_in;
    bool single_texture_roughness_metalness;
    float g_radius;

    float4 g_mat_color;

    float2 g_mat_tiling;
    float2 g_mat_offset;

    float g_mat_roughness;
    float g_mat_metallness;
    float g_mat_normal;
    float g_mat_height;

    uint g_mat_id;
    uint g_mat_textures;
    uint g_is_transparent_pass;
    uint g_mip_count;

    float3 g_extents;
    uint g_work_group_count;

    uint g_reflection_probe_available;
    float3 g_padding2;
};

// High frequency - Updates per light
cbuffer LightBuffer : register(b2)
{
    matrix cb_light_view_projection[6];
    float4 cb_light_intensity_range_angle_bias;
    float4 cb_light_color;
    float4 cb_light_position;
    float4 cb_light_direction;
    float cb_light_normal_bias;
    uint cb_options;
    float2 cb_padding;
};

// Low frequency - Updates once per frame
static const int g_max_materials = 1024;
cbuffer BufferMaterial : register(b3)
{
    float4 mat_clearcoat_clearcoatRough_aniso_anisoRot[g_max_materials];
    float4 mat_sheen_sheenTint_pad[g_max_materials];
}

// High frequency - update multiply times per frame, ImGui driven
cbuffer ImGuiBuffer : register(b4)
{
    matrix imgui_transform;
    uint imgui_texture_flags;
    float3 g_padding4;
}

// Options g-buffer textures                  
bool has_texture_height()                     { return g_mat_textures & uint(1U << 0); }
bool has_texture_normal()                     { return g_mat_textures & uint(1U << 1); }
bool has_texture_albedo()                     { return g_mat_textures & uint(1U << 2); }
bool has_texture_roughness()                  { return g_mat_textures & uint(1U << 3); }
bool has_texture_metalness()                  { return g_mat_textures & uint(1U << 4); }
bool has_texture_alpha_mask()                 { return g_mat_textures & uint(1U << 5); }
bool has_texture_emissive()                   { return g_mat_textures & uint(1U << 6); }
bool has_texture_occlusion()                  { return g_mat_textures & uint(1U << 7); }
bool has_single_texture_roughness_metalness() { return single_texture_roughness_metalness; }

// Options lighting
bool light_is_directional()           { return cb_options & uint(1U << 0); }
bool light_is_point()                 { return cb_options & uint(1U << 1); }
bool light_is_spot()                  { return cb_options & uint(1U << 2); }
bool light_has_shadows()              { return cb_options & uint(1U << 3); }
bool light_has_shadows_transparent()  { return cb_options & uint(1U << 4); }
bool light_has_shadows_screen_space() { return cb_options & uint(1U << 5); }
bool light_is_volumetric()            { return cb_options & uint(1U << 6); }

// Options passes
bool is_taa_enabled()                  { return any(g_taa_jitter_current); }
bool is_ssr_enabled()                  { return g_options & uint(1U << 0); }
bool is_ssao_enabled()                 { return g_options & uint(1U << 1); }
bool is_volumetric_fog_enabled()       { return g_options & uint(1U << 2); }
bool is_screen_space_shadows_enabled() { return g_options & uint(1U << 3); }
bool is_ssao_gi_enabled()              { return g_options & uint(1U << 4); }

// Options texture visualisation
bool texture_visualise()        { return imgui_texture_flags & uint(1U << 10); }
bool texture_pack()             { return imgui_texture_flags & uint(1U << 11); }
bool texture_gamma_correction() { return imgui_texture_flags & uint(1U << 12); }
bool texture_boost()            { return imgui_texture_flags & uint(1U << 13); }
bool texture_abs()              { return imgui_texture_flags & uint(1U << 14); }
bool texture_channel_r()        { return imgui_texture_flags & uint(1U << 15); }
bool texture_channel_g()        { return imgui_texture_flags & uint(1U << 16); }
bool texture_channel_b()        { return imgui_texture_flags & uint(1U << 17); }
bool texture_channel_a()        { return imgui_texture_flags & uint(1U << 18); }
bool texture_sample_point()     { return imgui_texture_flags & uint(1U << 19); }

// Misc
bool is_opaque_pass()      { return g_is_transparent_pass == 0; }
bool is_transparent_pass() { return g_is_transparent_pass == 1; }
