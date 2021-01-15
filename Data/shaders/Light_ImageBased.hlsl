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
    const float2 uv         = input.uv;
    const int2 screen_pos   = uv * g_resolution;
    const float4 normal     = tex_normal.Load(int3(screen_pos, 0));
    const bool is_sky       = round(normal.a * 65535) == 0;

    // Discard sky pixels
    if (is_sky)
        discard;

    // Construct material
    const float4 sample_material = tex_material.Load(int3(screen_pos, 0));
    Surface surface;
    surface.albedo     = tex_albedo.Load(int3(screen_pos, 0));
    surface.roughness  = sample_material.r;
    surface.metallic   = sample_material.g;
    surface.emissive   = sample_material.b;
    surface.F0         = lerp(0.04f, surface.albedo.rgb, surface.metallic);

    const bool is_transparent = surface.albedo.a != 1.0f;

    // Sample some textures
    const float depth   = tex_depth.Load(int3(screen_pos, 0)).r;
    float ssao          = !is_transparent ? tex_ssao.SampleLevel(sampler_point_clamp, uv, 0).r : 1.0f; // if hbao is disabled, the texture will be 1x1 white pixel, so we use a sampler

    // Light - Ambient
    float3 light_ambient = saturate(g_directional_light_intensity / 128000.0f);
    
    // Apply ambient occlusion to ambient light
    light_ambient *= MultiBounceAO(ssao, surface.albedo.rgb);
    
    // Light - IBL
    const float3 camera_to_pixel = get_view_direction(depth, uv);
    float3 diffuse_energy   = 1.0f;
    float3 light_ibl        = Brdf_Specular_Ibl(surface, normal.xyz, camera_to_pixel, diffuse_energy) * light_ambient;
    light_ibl               += Brdf_Diffuse_Ibl(surface, normal.xyz) * light_ambient * diffuse_energy; // Tone down diffuse such as that only non metals have it

    // Fade out for transparents
    light_ibl *= surface.albedo.a;

    // Light - Refraction (only for transparents)
    float3 light_refraction = 0.0f;
    if (is_transparent)
    {
        // Compute refraction UV
        float ior               = 1.5; // glass
        float2 normal_view_2d   = normalize(mul(float4(normal.xyz, 0.0f), g_view).xyz).xy;
        float2 refraction_uv    = uv + normal_view_2d * ior * 0.03f;

        // Only refract what's behind the surface
        if (get_linear_depth(refraction_uv) > get_linear_depth(depth)) // refraction (only refract what's behind the surface)
        {
            light_refraction = tex_frame.SampleLevel(sampler_bilinear_clamp, refraction_uv, 0).rgb;
        }
        else
        {
            light_refraction = tex_frame.SampleLevel(sampler_bilinear_clamp, uv, 0).rgb;
        }
    }

    return float4(saturate_16(light_ibl + light_refraction), 1.0f);
}
