/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "common.hlsl"
//====================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (any(int2(thread_id.xy) >= pass_get_resolution_out()))
        return;

    Surface surface;
    surface.Build(thread_id.xy, true, true, false);

    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // if this is an opaque pass, ignore all transparent pixels.
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // if this is an transparent pass, ignore all opaque pixels.
    bool early_exit_3 = pass_is_transparent() && surface.is_sky();    // if this is a transparent pass, ignore sky pixels (they only render in the opaque)
    if (early_exit_1 || early_exit_2 || early_exit_3)
        return;

    float4 color = float4(0.0f, 0.0f, 0.0f, 1.0f);

    // draw the sky
    if (surface.is_sky()) 
    {
        color.rgb += tex_environment.SampleLevel(samplers[sampler_bilinear_clamp], direction_sphere_uv(surface.camera_to_pixel), 0).rgb;
        color.rgb *= saturate(buffer_light.intensity_range_angle_bias.x); // modulate it's intensity in order to fake day/night.
    }
    else // everything else.
    {
        // light - diffuse and Specular.
        float3 light_diffuse  = tex_light_diffuse[thread_id.xy].rgb;
        float3 light_specular = tex_light_specular[thread_id.xy].rgb;

        // light - refraction
        float3 light_refraction = 0.0f;
        if (surface.is_transparent())
        {
            // compute refraction UV offset.
            float ior                   = 1.5; // glass
            float scale                 = 0.03f;
            float distance_falloff      = clamp(1.0f / world_to_view(surface.position).z, -3.0f, 3.0f);
            float2 refraction_normal    = world_to_view(surface.normal.xyz, false).xy ;
            float2 refraction_uv_offset = refraction_normal * distance_falloff * scale * max(0.0f, ior - 1.0f);

            // only refract what's behind the surface.
            float depth_surface           = get_linear_depth(surface.depth);
            float depth_surface_refracted = get_linear_depth(surface.uv + refraction_uv_offset);
            float is_behind               = step(depth_surface - 0.02f, depth_surface_refracted); // step does a >=, but when the depth is equal, we still want refract, so we use a bias of 0.02.

            // refraction from ALDI.
            float roughness2      = surface.roughness * surface.roughness;
            float frame_mip_count = pass_get_f3_value().x;
            float mip_level       = lerp(0, frame_mip_count, roughness2);
            light_refraction      = tex_frame.SampleLevel(samplers[sampler_trilinear_clamp], surface.uv + refraction_uv_offset * is_behind, mip_level).rgb;
        }
        
        // compose everything.
        float3 light_ds = (light_diffuse + surface.gi) * surface.albedo + light_specular;
        color.rgb       += lerp(light_ds, light_refraction, 1.0f - surface.alpha);
    }

    // add volumetric fog
    color.rgb += tex_light_volumetric[thread_id.xy].rgb;

    tex_uav[thread_id.xy] = saturate_16(color);
}
