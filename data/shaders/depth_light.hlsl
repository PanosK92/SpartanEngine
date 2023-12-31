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

Pixel_PosUv mainVS(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    Pixel_PosUv output;
    
    uint index_light = (uint)pass_get_f3_value2().y;
    uint index_array = (uint)pass_get_f3_value2().x;
    Light_ light     = buffer_lights[index_light];
    
    output.position  = compute_screen_space_position(input, instance_id, buffer_pass.transform, light.view_projection[index_array], buffer_frame.time);
    output.uv        = input.uv;

    return output;
}

// transparent/colored shadows
float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    Material material = GetMaterial();
    float2 uv         = float2(input.uv.x * material.tiling.x + material.offset.x, input.uv.y * material.offset.y + material.tiling.y);
    float4 color      = tex.SampleLevel(samplers[sampler_anisotropic_wrap], uv, 0);
    
    return float4(degamma(color.rgb), color.a) * material.color;
}
