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
    matrix view_projection;
    matrix view_projection_inverted;
    matrix view_projection_orthographic;
    matrix view_projection_unjittered;
    matrix view_projection_previous;

    float2 resolution_render;
    float2 resolution_output;

    float2 taa_jitter_current;
    float2 taa_jitter_previous;
    
    float time;
    float delta_time;
    uint frame;
    uint options;
    
    float3 camera_position;
    float camera_near;
    
    float3 camera_direction;
    float camera_far;

    float gamma;
    float camera_last_movement_time;
    float2 padding_1;

    float3 camera_position_previous;
    float padding_2;
};

struct LightBufferData
{
    matrix view_projection[6];
    
    float intensity;
    float range;
    float angle;
    float bias;

    float4 color;
    
    float3 position;
    float normal_bias;
    
    float3 direction;
    uint options;
};

cbuffer BufferFrame : register(b0) { FrameBufferData buffer_frame;  }; // low frequency    - updates once per frame
cbuffer BufferLight : register(b1) { LightBufferData buffer_light;  }; // medium frequency - updates per light

struct PassBufferData
{
    matrix transform;
    matrix m_value;
};

[[vk::push_constant]]
PassBufferData buffer_pass;

// lighting properties
bool light_is_directional()           { return buffer_light.options & uint(1U << 0); }
bool light_is_point()                 { return buffer_light.options & uint(1U << 1); }
bool light_is_spot()                  { return buffer_light.options & uint(1U << 2); }
bool light_has_shadows()              { return buffer_light.options & uint(1U << 3); }
bool light_has_shadows_transparent()  { return buffer_light.options & uint(1U << 4); }
bool light_is_volumetric()            { return buffer_light.options & uint(1U << 5); }

// frame properties
bool is_taa_enabled()                  { return any(buffer_frame.taa_jitter_current); }
bool is_ssr_enabled()                  { return buffer_frame.options & uint(1U << 0); }
bool is_ssgi_enabled()                 { return buffer_frame.options & uint(1U << 1); }
bool is_screen_space_shadows_enabled() { return buffer_frame.options & uint(1U << 2); }
bool is_fog_enabled()                  { return buffer_frame.options & uint(1U << 3); }
bool is_fog_volumetric_enabled()       { return buffer_frame.options & uint(1U << 4); }

// pass properties
matrix pass_get_transform_previous()      { return buffer_pass.m_value; }
float2 pass_get_resolution_in()           { return float2(buffer_pass.m_value._m03, buffer_pass.m_value._m22); }
float2 pass_get_resolution_out()          { return float2(buffer_pass.m_value._m23, buffer_pass.m_value._m30); }
float3 pass_get_f3_value()                { return float3(buffer_pass.m_value._m00, buffer_pass.m_value._m01, buffer_pass.m_value._m02); }
float3 pass_get_f3_value2()               { return float3(buffer_pass.m_value._m20, buffer_pass.m_value._m21, buffer_pass.m_value._m31); }
float4 pass_get_f4_value()                { return float4(buffer_pass.m_value._m10, buffer_pass.m_value._m11, buffer_pass.m_value._m12, buffer_pass.m_value._m13); }
bool pass_is_transparent()                { return buffer_pass.m_value._m33; }
bool pass_is_reflection_probe_available() { return pass_get_f4_value().x == 1.0f; } // this is more risky
bool pass_is_opaque()                     { return !pass_is_transparent(); }
uint pass_get_material_index()            { return buffer_pass.m_value._m32; }
