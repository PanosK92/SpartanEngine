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

//= INCLUDES =========
#include "common.hlsl"
//====================

// based on the grid shader found in: https://github.com/deg3x/GraphicsPlayground

// parameters
static const float thickness_norm  = 2.0f; // grid line size (in pixels)
static const float thickness_bold  = 2.0f * thickness_norm;
static const float frequency_bold  = 10.0f;
static const float max_camera_dist = 200.0f;
static const float3 color_default  = float3(0.70, 0.70, 0.70);
static const float3 color_bold     = float3(0.85, 0.85, 0.85);
static const float3 color_axis_x   = float3(0.85, 0.40, 0.30);
static const float3 color_axis_z   = float3(0.40, 0.50, 0.85);

struct PixelInput
{
    float4 position       : SV_POSITION;
    float4 position_world : POSITION_WORLD;
};

PixelInput main_vs(Vertex_PosUvNorTan input)
{
    PixelInput output;

    input.position.w      = 1.0f;
    output.position_world = mul(input.position, buffer_pass.transform);
    output.position       = mul(output.position_world, buffer_frame.view_projection_unjittered);

    return output;
}

float4 main_ps(PixelInput input) : SV_TARGET
{
    float3 world_pos = input.position_world.xyz;

    // grid test normal
    float2 derivative  = fwidth(world_pos.xz);
    float2 grid_aa     = derivative * 1.5;
    float2 grid_uv     = 1.0 - abs(frac(world_pos.xz) * 2.0 - 1.0);
    float2 line_width  = thickness_norm * derivative;
    float2 draw_width  = clamp(line_width, derivative, 0.5);
    float2 grid_test   = 1.0 - smoothstep(draw_width - grid_aa, draw_width + grid_aa, grid_uv);
    grid_test         *= clamp(line_width / draw_width, 0.0, 1.0);

    float grid_norm       = lerp(grid_test.x, 1.0, grid_test.y);
    float alpha_grid_norm = clamp(grid_norm, 0.0, 0.8);

    // grid test bold
    derivative = fwidth(world_pos.xz / frequency_bold);
    grid_aa    = derivative * 1.5;
    grid_uv    = 1.0 - abs(frac(world_pos.xz / frequency_bold) * 2.0 - 1.0);
    line_width = thickness_bold * derivative;
    draw_width = clamp(line_width, derivative, 0.5);
    grid_test  = 1.0 - smoothstep(draw_width - grid_aa, draw_width + grid_aa, grid_uv);
    grid_test *= clamp(line_width / draw_width, 0.0, 1.0);

    float grid_bold       = lerp(grid_test.x, 1.0, grid_test.y);
    float alpha_grid_bold = clamp(grid_bold, 0.0, 0.9);

    // final grid alpha
    float alpha_grid = max(alpha_grid_norm, alpha_grid_bold);

    // color test
    float3 color_output = lerp(color_default, color_bold, grid_bold);

    float align_axis_x = step(abs(world_pos.z), grid_test.y);
    float align_axis_z = step(abs(world_pos.x), grid_test.x);
    float sum          = clamp(align_axis_x + align_axis_z, 0.0, 1.0);

    color_output = lerp(color_output, color_axis_z, align_axis_z);
    color_output = lerp(color_output, color_axis_x, align_axis_x);

    // camera distance test
    float camera_dist = length(buffer_frame.camera_position.xz - world_pos.xz);
    float alpha_dist  = 1.0 - smoothstep(0.0, max_camera_dist, camera_dist);

    float alpha = min(alpha_dist, alpha_grid);

    return float4(color_output, alpha);
}
