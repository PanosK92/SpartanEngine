/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "brdf.hlsl"
//====================

struct Pixel_Input
{
    float4 position    : SV_POSITION;
    float3 position_ws : POSITION;
    float2 uv          : TEXCOORD;
    float3 normal      : NORMAL;
};

Pixel_Input mainVS(Vertex_PosUvNorTan input)
{
    Pixel_Input output;

    input.position.w   = 1.0f;
    output.position    = mul(input.position, buffer_pass.transform);
    output.position_ws = output.position.xyz;
    output.normal      = normalize(mul(input.normal, (float3x3)buffer_pass.transform)).xyz;
    output.uv          = input.uv;

    return output;
}

float4 mainPS(Pixel_Input input) : SV_TARGET
{
    Surface surface;
    surface.Build(input.uv * buffer_frame.resolution_render, true, true, true);

    Light light;
    light.Build(input.position_ws, input.normal, 0.0f);

    AngularInfo angular_info;
    angular_info.Build(light, surface);

    float3 specular = BRDF_Specular_Isotropic(surface, angular_info);
    float3 diffuse  = BRDF_Diffuse(surface, angular_info);

    return float4((diffuse * surface.diffuse_energy + specular) * light.radiance , surface.alpha);
}
