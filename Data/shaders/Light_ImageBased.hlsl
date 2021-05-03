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

// = INCLUDES ======
#include "BRDF.hlsl"
//==================

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    // Construct surface
    Surface surface;
    surface.Build(input.uv * g_resolution_rt);

    // Discard sky pixels
    if (surface.is_sky())
        discard;

    // Light - Ambient
    float3 light_ambient = saturate(g_directional_light_intensity / 128000.0f);
    
    // Apply ambient occlusion to ambient light
    light_ambient *= surface.occlusion;

    // Light - IBL
    float3 light_ibl = 0.0f;
    if (any(light_ambient))
    { 
        float3 diffuse_energy   = 1.0f;
        light_ibl               = Brdf_Specular_Ibl(surface, surface.normal, surface.camera_to_pixel, diffuse_energy) * light_ambient;
        light_ibl               += Brdf_Diffuse_Ibl(surface, surface.normal) * light_ambient * diffuse_energy; // Tone down diffuse such as that only non metals have it

        // Fade out for transparents
        light_ibl *= surface.alpha;
    }

    return float4(saturate_16(light_ibl), 1.0f);
}
