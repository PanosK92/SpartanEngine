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

// From Sebastien Lagarde Moving Frostbite to PBR page 69
float3 get_dominant_specular_direction(float3 normal, float3 reflection, float roughness)
{
    const float smoothness = 1.0f - roughness;
    const float lerpFactor = smoothness * (sqrt(smoothness) + roughness);
    
    return lerp(normal, reflection, lerpFactor);
}

float3 sample_environment(float2 uv, float mip_level)
{
    // We are currently using a spherical environment map which has a 2:1 ratio, so at the smallest 
    // mipmap we have to do a bit of blending otherwise we'll get a visible seem in the middle.
    if (mip_level == g_envrionement_max_mip)
    {
        float2 mip_size = float2(2, 1);
        float dx = mip_size.x;

        float3 tl = (tex_environment.SampleLevel(sampler_bilinear_clamp, uv + float2(-dx, 0.0f), mip_level).rgb);
        float3 tr = (tex_environment.SampleLevel(sampler_bilinear_clamp, uv + float2(dx, 0.0f), mip_level).rgb);
        return (tl + tr) / 2.0f;
    }

    return tex_environment.SampleLevel(sampler_trilinear_clamp, uv, mip_level).rgb;
}

float4 mainPS(Pixel_PosUv input) : SV_TARGET
{
    const uint2 pos = input.uv * g_resolution_rt;
    
    // Construct surface
    Surface surface;
    surface.Build(pos, true, true, false);

    // TODO: Use the stencil buffer to avoid transparents or simply add SSR for transparents
    // If this is a transparent pass, ignore all opaque pixels, and vice versa.
    bool early_exit_1 = !g_is_transparent_pass && surface.is_transparent();
    bool early_exit_2 = g_is_transparent_pass && surface.is_opaque();
    if (early_exit_1 || early_exit_2 || surface.is_sky())
        discard;

    // Compute specular energy
    const float n_dot_v          = saturate(dot(-surface.camera_to_pixel, surface.normal));
    const float3 F               = F_Schlick_Roughness(surface.F0, n_dot_v, surface.roughness);
    const float2 envBRDF         = tex_lutIbl.SampleLevel(sampler_bilinear_clamp, float2(n_dot_v, surface.roughness), 0.0f).xy;
    const float3 specular_energy = F * envBRDF.x + envBRDF.y;
    
    // Get ssr color
    float mip_level         = lerp(0, g_ssr_mip_count, surface.roughness);
    const float4 ssr_sample = (is_ssr_enabled() && !g_is_transparent_pass) ? tex_ssr.SampleLevel(sampler_trilinear_clamp, surface.uv, mip_level) : 0.0f;
    const float3 color_ssr  = ssr_sample.rgb;
    const float ssr_alpha   = ssr_sample.a * luminance(specular_energy);

    // Get environment color
    float3 color_environment = 0.0f;
    if (ssr_alpha != 1.0f)
    {
        float3 light_ambient = saturate(g_directional_light_intensity / 128000.0f) * surface.occlusion;
        if (any(light_ambient))
        {
            // Specular
            {
                const float3 reflection            = reflect(surface.camera_to_pixel, surface.normal);
                float3 dominant_specular_direction = get_dominant_specular_direction(surface.normal, reflection, surface.roughness);
                float mip_level                    = lerp(0, g_envrionement_max_mip, surface.roughness);
                color_environment                  = sample_environment(direction_sphere_uv(dominant_specular_direction), mip_level) * specular_energy;
            }

            // Diffuse
            float3 diffuse_energy = compute_diffuse_energy(specular_energy, surface.metallic); // Used to town down diffuse such as that only non metals have it
            color_environment     += sample_environment(direction_sphere_uv(surface.normal), g_envrionement_max_mip) * surface.albedo.rgb * light_ambient * diffuse_energy; 

            // Fade out for transparents
            color_environment *= surface.alpha;
        }
    }

    return float4(saturate_11(lerp(color_environment, color_ssr, ssr_alpha)), 0.0f);
}
