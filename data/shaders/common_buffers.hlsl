/*
Copyright(c) 2016-2024 Panos Karabelas

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

#ifndef SPARTAN_COMMON_BUFFERS
#define SPARTAN_COMMON_BUFFERS

// constant buffer holding data related to the currenty frame
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
    
    float directional_light_intensity;
    float delta_time;
    uint frame;
    uint options;
    
    float3 camera_position;
    float camera_near;
    
    float3 camera_direction;
    float camera_far;

    float camera_last_movement_time;
    float hdr_enabled;
    float hdr_max_nits;
    float hdr_white_point;

    float3 camera_position_previous;
    float resolution_scale;
    
    double time;
    float2 padding;
};

// 128 byte push constant buffer used by everything in the engine
struct PassBufferData
{
    matrix transform;
    matrix values;
};

[[vk::push_constant]]
PassBufferData buffer_pass;
cbuffer BufferFrame : register(b0) { FrameBufferData buffer_frame;  };

// easy access to certain frame constant buffer properties
bool is_taa_enabled()  { return any(buffer_frame.taa_jitter_current); }
bool is_ssr_enabled()  { return buffer_frame.options & uint(1U << 0); }
bool is_ssgi_enabled() { return buffer_frame.options & uint(1U << 1); }

// easy access to the push constant properties
matrix pass_get_transform_previous() { return buffer_pass.values; }
float2 pass_get_f2_value()           { return float2(buffer_pass.values._m23, buffer_pass.values._m30); }
float3 pass_get_f3_value()           { return float3(buffer_pass.values._m00, buffer_pass.values._m01, buffer_pass.values._m02); }
float3 pass_get_f3_value2()          { return float3(buffer_pass.values._m20, buffer_pass.values._m21, buffer_pass.values._m31); }
float4 pass_get_f4_value()           { return float4(buffer_pass.values._m10, buffer_pass.values._m11, buffer_pass.values._m12, buffer_pass.values._m33); }

uint pass_get_material_index() { return buffer_pass.values._m03; }
bool pass_is_transparent()     { return buffer_pass.values._m13 == 1.0f; }
bool pass_is_opaque()          { return !pass_is_transparent(); }
// _m32 is available for use

#endif // SPARTAN_COMMON_BUFFERS
