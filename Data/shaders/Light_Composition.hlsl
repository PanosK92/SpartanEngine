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

    // If this is a transaprent pass but this pixel is not, skip it
    float4 albedo = tex_albedo[thread_id.xy];
    if (g_is_transprent_pass && albedo.a == 1.0f)
        return;

    // Compute some useful things
    const float2 uv     = (thread_id.xy + 0.5f) / g_resolution;
    const int mat_id    = round(tex_normal[thread_id.xy].a * 65535);
    const bool is_sky   = mat_id == 0;
    
    // Get some useful things
    const float depth               = get_depth(thread_id.xy);
    const float3 position           = get_position(depth, uv);
    const float3 camera_to_pixel    = get_view_direction(position, uv);

    // Compute fog
    float camera_to_pixel_length    = length(position - g_camera_position.xyz);
    float3 fog                      = get_fog_factor(position.y, camera_to_pixel_length);

    // Modulate fog with ambient light
    float ambient_light = saturate(g_directional_light_intensity / 128000.0f);
    fog *= ambient_light * 0.25f;

    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    [branch]
    if (is_sky) // Sky
    {
        color.rgb   += tex_environment.SampleLevel(sampler_bilinear_clamp, direction_sphere_uv(camera_to_pixel), 0).rgb;
        color.rgb   *= saturate(g_directional_light_intensity / 128000.0f);
        fog         *= luminance(color.rgb);
    }
    else // Everything else
    {
        // Sample from textures
        float3 light_diffuse    = tex_light_diffuse[thread_id.xy].rgb;
        float3 light_specular   = tex_light_specular[thread_id.xy].rgb;

        // Accumulate diffuse and specular light
        color.rgb += (light_diffuse * albedo.rgb + light_specular) * albedo.a * albedo.a;
    }

    // Accumulate fog
    color.rgb += fog; // regular
    color.rgb += tex_light_volumetric[thread_id.xy].rgb; // volumetric

    if (!g_is_transprent_pass || is_sky)
    {
        tex_out_rgba[thread_id.xy] = saturate_16(color);
    }
    else
    {
        tex_out_rgba[thread_id.xy] += saturate_16(color);
    }
}
