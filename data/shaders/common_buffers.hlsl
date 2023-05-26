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

struct FrameBufferData
{
    matrix view;
    matrix projection;
    matrix projection_inverted;
    matrix projection_orthographic;
    matrix view_projection;
    matrix view_projection_inverted;
    matrix view_projection_orthographic;
    matrix view_projection_unjittered;
    matrix view_projection_previous;

    float delta_time;
    float time;
    uint frame;
    float camera_aperture;

    float camera_shutter_speed;
    float camera_iso;
    float camera_near;
    float camera_far;

    float3 camera_position;
    float bloom_intensity;
    
    float sharpen_strength;
    float3 camera_direction;
    
    float gamma;
    float tone_mapping;
    float directional_light_intensity;
    float shadow_resolution;

    float2 resolution_render;
    float2 resolution_output;

    float2 taa_jitter_current;
    float2 taa_jitter_previous;

    float fog_density;
    uint options;
    uint frame_mip_count;
    uint ssr_mip_count;

    float2 resolution_environment;
    float exposure;
    float luminance_min_nits;

    //float g_luminance_max_nits;
    //float g_paper_white_nits;
    //float2 g_padding;
};

struct UberBufferData
{
    matrix transform;
    matrix transform_previous;

    float blur_radius;
    float blur_sigma;
    float2 blur_direction;

    float2 resolution_rt;
    float2 resolution_in;

    bool single_texture_roughness_metalness;
    float radius;
    float2 padding2;

    float4 mat_color;

    float2 mat_tiling;
    float2 mat_offset;

    float mat_roughness;
    float mat_metallness;
    float mat_normal;
    float mat_height;

    uint mat_id;
    uint mat_textures;
    uint is_transparent_pass;
    uint mip_count;

    float3 extents;
    uint work_group_count;

    uint reflection_probe_available;
    float3 position;
};

struct LightBufferData
{
    matrix view_projection[6];
    float4 intensity_range_angle_bias;
    float4 color;
    float4 position;
    float4 direction;

    float normal_bias;
    uint options;
    float2 padding;
};

struct MaterialBufferData
{
    float4 clearcoat_clearcoatRough_aniso_anisoRot;
    float4 sheen_sheenTint_pad;
};

struct ImGuiBufferData
{
    matrix transform;

    uint texture_flags;
    uint mip_level;
    float2 padding;
};

cbuffer BufferFrame    : register(b0) { FrameBufferData buffer_frame;              }; // Low frequency    - Updates once per frame
cbuffer BufferUber     : register(b1) { UberBufferData buffer_uber;                }; // Medium frequency - Updates per render pass
cbuffer BufferLight    : register(b2) { LightBufferData buffer_light;              }; // Medium frequency - Updates per light
cbuffer BufferMaterial : register(b3) { MaterialBufferData buffer_materials[1024]; }; // Low frequency    - Updates once per frame (maybe)
cbuffer BufferImGui    : register(b4) { ImGuiBufferData buffer_imgui;              }; // High frequency   - Update multiply times per frame

// g-buffer texture options
bool has_texture_height()                     { return buffer_uber.mat_textures & uint(1U << 0); }
bool has_texture_normal()                     { return buffer_uber.mat_textures & uint(1U << 1); }
bool has_texture_albedo()                     { return buffer_uber.mat_textures & uint(1U << 2); }
bool has_texture_roughness()                  { return buffer_uber.mat_textures & uint(1U << 3); }
bool has_texture_metalness()                  { return buffer_uber.mat_textures & uint(1U << 4); }
bool has_texture_alpha_mask()                 { return buffer_uber.mat_textures & uint(1U << 5); }
bool has_texture_emissive()                   { return buffer_uber.mat_textures & uint(1U << 6); }
bool has_texture_occlusion()                  { return buffer_uber.mat_textures & uint(1U << 7); }
bool has_single_texture_roughness_metalness() { return buffer_uber.single_texture_roughness_metalness; }

// lighting options
bool light_is_directional()           { return buffer_light.options & uint(1U << 0); }
bool light_is_point()                 { return buffer_light.options & uint(1U << 1); }
bool light_is_spot()                  { return buffer_light.options & uint(1U << 2); }
bool light_has_shadows()              { return buffer_light.options & uint(1U << 3); }
bool light_has_shadows_transparent()  { return buffer_light.options & uint(1U << 4); }
bool light_has_shadows_screen_space() { return buffer_light.options & uint(1U << 5); }
bool light_is_volumetric()            { return buffer_light.options & uint(1U << 6); }

// frame options
bool is_taa_enabled()                  { return any(buffer_frame.taa_jitter_current); }
bool is_ssr_enabled()                  { return buffer_frame.options & uint(1U << 0); }
bool is_ssgi_enabled()                 { return buffer_frame.options & uint(1U << 1); }
bool is_volumetric_fog_enabled()       { return buffer_frame.options & uint(1U << 2); }
bool is_screen_space_shadows_enabled() { return buffer_frame.options & uint(1U << 3); }

// misc
bool is_opaque_pass()      { return buffer_uber.is_transparent_pass == 0; }
bool is_transparent_pass() { return buffer_uber.is_transparent_pass == 1; }
