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
#include "brdf.hlsl"
//==================

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID, uint group_index : SV_GroupIndex, uint3 group_id : SV_GroupID)
{
    if (thread_id.x >= uint(g_resolution_rt.x) || thread_id.y >= uint(g_resolution_rt.y))
        return;

    // Construct surface
    Surface surface;
    surface.Build(thread_id.xy);

    // If this is a transparent pass, ignore all opaque pixels, and vice versa.
    bool early_exit_1 = g_is_transparent_pass && surface.is_opaque();
    bool early_exit_2 = !g_is_transparent_pass && surface.is_transparent();
    if (early_exit_1 || early_exit_2 || surface.is_sky())
        return;

    // Light - Ambient
    float3 light_ambient = saturate(g_directional_light_intensity / 128000.0f);
    
    // Apply ambient occlusion to ambient light
    light_ambient *= surface.occlusion;

    // Light - IBL
    float3 light_ibl = 0.0f;
    if (any(light_ambient))
    {
        float3 diffuse_energy = 1.0f;
        light_ibl             = Brdf_Specular_Ibl(surface, surface.normal, surface.camera_to_pixel, diffuse_energy) * light_ambient;
        light_ibl             += Brdf_Diffuse_Ibl(surface, surface.normal) * light_ambient * diffuse_energy; // Tone down diffuse such as that only non metals have it

        // Fade out for transparents
        light_ibl *= surface.alpha;
    }

    tex_out_rgba[thread_id.xy] += float4(saturate_16(light_ibl), tex_out_rgba[thread_id.xy].a);
}
