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

// = INCLUDES ========
#include "Common.hlsl"
#include "Fog.hlsl"
//====================

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    Surface surface;
    surface.Build(thread_id.xy);
    
    // If this is a transparent pass, ignore all opaque pixels, and vice versa.
    if ((g_is_transparent_pass && surface.is_opaque()) || (!g_is_transparent_pass && surface.is_transparent() && !surface.is_sky()))
        return;

    // Compute fog
    float3 fog = get_fog_factor(surface.position.y, surface.camera_to_pixel_length);

    // Modulate fog with ambient light
    float ambient_light = saturate(g_directional_light_intensity / 128000.0f);
    fog *= ambient_light * 0.25f;

    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    [branch]
    if (surface.is_sky()) // Sky
    {
        color.rgb   += tex_environment.SampleLevel(sampler_bilinear_clamp, direction_sphere_uv(surface.camera_to_pixel), 0).rgb;
        color.rgb   *= saturate(g_directional_light_intensity / 128000.0f);
        fog         *= luminance(color.rgb);
    }
    else // Everything else
    {
        // Sample from textures
        float3 light_diffuse    = tex_light_diffuse[thread_id.xy].rgb;
        float3 light_specular   = tex_light_specular[thread_id.xy].rgb;

        // Accumulate diffuse and specular light
        color.rgb += (light_diffuse * surface.albedo + light_specular) * surface.alpha * surface.alpha;
    }

    // Accumulate fog
    color.rgb += fog; // regular
    color.rgb += tex_light_volumetric[thread_id.xy].rgb; // volumetric

    // Overwrite for opaque pass
    if (surface.is_opaque() || surface.is_sky())
    {
        tex_out_rgba[thread_id.xy] = saturate_16(color);
    }
    // Accumulation for transparent pass
    else
    {
        tex_out_rgba[thread_id.xy] += saturate_16(color);
    }
}
