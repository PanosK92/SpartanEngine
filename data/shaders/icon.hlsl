/*
Copyright(c) 2016-2025 Panos Karabelas

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

/*
Copyright(c) 2016-2025 Panos Karabelas

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

#define icon_x 64
#define icon_y 64
#define thread_x 32
#define thread_y 32

[numthreads(thread_x, thread_y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // visibility check
    float3 world_pos        = pass_get_f3_value();
    float3 camera_to_entity = normalize(world_pos - buffer_frame.camera_position.xyz);
    float v_dot_l           = dot(buffer_frame.camera_forward.xyz, camera_to_entity);
    if (v_dot_l <= 0.5f)
        return;

    // project world position to screen space
    float4 clip_pos   = mul(float4(world_pos, 1.0f), buffer_frame.view_projection_unjittered);
    float2 screen_pos = float2(clip_pos.x / clip_pos.w, clip_pos.y / clip_pos.w) * 0.5f + 0.5f; // ndc to [0,1]
    screen_pos.y      = 1.0f - screen_pos.y; // flip Y for top-left origin

    // convert to pixel coordinates, centering the icon
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    float2 icon_offset = float2(thread_id.xy) - float2(icon_x * 0.5f, icon_y * 0.5f); // center it
    float2 pixel_coord = screen_pos * resolution_out + icon_offset;

    // check if pixel is within output texture bounds
    if (pixel_coord.x < 0 || pixel_coord.y < 0 || pixel_coord.x >= resolution_out.x || pixel_coord.y >= resolution_out.y)
        return;

    // sample icon texture
    float2 uv         = (float2(thread_id.xy) + 0.5f) / float2(icon_x, icon_y); 
    float4 icon_color = tex.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0);

    // blend
    uint2 pixel_coord_int    = uint2(pixel_coord);
    float4 base_color        = tex_uav[pixel_coord_int];
    tex_uav[pixel_coord_int] = float4(lerp(base_color.rgb, icon_color.rgb, icon_color.a), base_color.a);
}
