/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "fog.hlsl"
//====================

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    // create surface
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    // initialize
    float3 light_diffuse       = 0.0f;
    float3 light_specular      = 0.0f;
    float3 light_emissive      = 0.0f;
    float3 light_atmospheric   = 0.0f;
    float alpha                = 0.0f;
    float distance_from_camera = 0.0f;

    // during the compute pass, fill in the sky pixels
    if (surface.is_sky() && pass_is_opaque())
    {
        light_emissive       = tex2.SampleLevel(samplers[sampler_bilinear_clamp], direction_sphere_uv(surface.camera_to_pixel), 0).rgb;
        alpha                = 0.0f;
        distance_from_camera = FLT_MAX_16;
    }
    // for the opaque pass, fill in the opaque pixels, and for the transparent pass, fill in the transparent pixels
    else if ((pass_is_opaque() && surface.is_opaque()) || (pass_is_transparent() && surface.is_transparent()))
    {
        light_diffuse        = tex3.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        light_specular       = tex4.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
        light_emissive       = surface.emissive * surface.albedo * 10.0f;
        alpha                = surface.alpha;
        distance_from_camera = surface.camera_to_pixel_length;
    }
    
    // fog
    {
        uint sky_mip = 4; // it just looks good
    
        // compute view direction
        float3 camera_position = buffer_frame.camera_position;
        float3 view_dir        = normalize(surface.position - camera_position);
    
        // sample sky in the view direction using your function
        float2 view_uv        = direction_sphere_uv(view_dir);
        float3 sky_color_view = tex2.SampleLevel(samplers[sampler_trilinear_clamp], view_uv, sky_mip).rgb;
    
        // sample sky in the light direction
        Light light;
        light.Build(0, surface); // 0 index is the where the directional light always lives
        float3 light_dir       = normalize(-light.forward);
        float2 light_uv        = direction_sphere_uv(light_dir);
        float3 sky_color_light = tex2.SampleLevel(samplers[sampler_trilinear_clamp], light_uv, sky_mip).rgb;
    
        // simple henyey-greenstein phase function for forward scattering
        float g         = 0.8f; // forward scattering strength
        float cos_theta = dot(view_dir, light_dir);
        float phase     = (1.0f - g * g) / (4.0f * PI * pow(1.0f + g * g - 2.0f * g * cos_theta, 1.5f));
    
        // blend view and light contributions
        float light_weight = 0.4f; // how much the light direction contributes
        float3 sky_color   = lerp(sky_color_view, sky_color_light, light_weight * phase);
    
        float fog_atmospheric = get_fog_atmospheric(distance_from_camera, surface.position.y);
        float3 fog_emissive   = tex5.SampleLevel(samplers[sampler_point_clamp], surface.uv, 0).rgb;
    
        // additive: ambient in-scatter + volumetric
        float3 fog_inscatter = fog_atmospheric * sky_color;
        light_atmospheric    = fog_inscatter + fog_emissive;
    }

    float accumulate      = (pass_is_transparent() && !surface.is_transparent()) ? 1.0f : 0.0f; // transparent surfaces will sample the background via refraction, no need to blend
    tex_uav[thread_id.xy] = validate_output(float4(light_diffuse * surface.albedo + light_specular + light_emissive + light_atmospheric, alpha) + tex_uav[thread_id.xy] * accumulate);
}
