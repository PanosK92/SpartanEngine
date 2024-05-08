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

// = INCLUDES ========
#include "common.hlsl"
#include "fog.hlsl"
//====================

struct translucency
{
    struct refraction
    {
        static float compute_fade_factor(float2 uv)
        {
            float edge_threshold = 0.05f; // how close to the edge to start fading
            float2 edge_distance = min(uv, 1.0f - uv);
            return saturate(min(edge_distance.x, edge_distance.y) / edge_threshold);
        }
        
        static float3 refract_vector(float3 i, float3 n, float eta)
        {
            // Snell's Law
            float cosi  = dot(-i, n);
            float cost2 = 1.0f - eta * eta * (1.0f - cosi * cosi);
            return eta * i + (eta * cosi - sqrt(abs(cost2))) * n;
        }
        
        static float3 get_color(Surface surface)
        {
            const float scale = 0.05f;
            
            // compute refraction vector
            float3 normal_vector        = world_to_view(surface.normal, false);
            float3 incident_vector      = world_to_view(surface.camera_to_pixel, false);
            float3 refraction_direction = refract_vector(incident_vector, normal_vector, 1.0f / surface.ior);
            
            // compute refracted uv
            float2 refraction_uv_offset = refraction_direction.xy * (scale / surface.camera_to_pixel_length);
            float2 refracted_uv         = saturate(surface.uv + refraction_uv_offset);

            // don't refract what's behind the surface
            float depth_surface    = get_linear_depth(surface.depth); // depth transparent
            float depth_refraction = get_linear_depth(get_depth_opaque(refracted_uv)); // depth opaque
            float is_behind        = depth_surface < depth_refraction;
            refracted_uv           = lerp(refracted_uv, refracted_uv, is_behind);
    
            // get base color
            float frame_mip_count   = pass_get_f3_value().x;
            float mip_level         = lerp(0, frame_mip_count, surface.roughness_alpha);
            float3 color            = tex_frame.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), surface.uv, mip_level).rgb;
            float3 color_refraction = tex_frame.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), refracted_uv, mip_level).rgb;
        
            // screen fade
            float fade_factor = compute_fade_factor(refracted_uv);
            color             = lerp(color, color_refraction, fade_factor);
    
            return color;
        }
    };
    
    struct water
    {
        static float3 get_color(Surface surface, inout float alpha)
        {
            const float MAX_DEPTH            = 100.0f;
            const float ALPHA_FACTOR         = 0.2f;
            const float FOAM_DEPTH_THRESHOLD = 2.0f;
            const float3 light_absorption    = float3(0.3f, 0.2f, 0.1f); // color spectrum light absorption

            // compute water depth
            float water_level       = get_position(surface.uv).y;
            float water_floor_level = get_position(get_depth_opaque(surface.uv), surface.uv).y;
            float water_depth       = clamp(water_level - water_floor_level, 0.0f, MAX_DEPTH);

            // compute color and alpha at that depth with slight adjustments
            float3 color = float3(exp(-light_absorption.x * water_depth), exp(-light_absorption.y * water_depth), exp(-light_absorption.z * water_depth));
            alpha        = 1.0f - exp(-water_depth * ALPHA_FACTOR);
            
            return color;
        }
    };
};

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution_out;
    tex_uav.GetDimensions(resolution_out.x, resolution_out.y);
    if (any(int2(thread_id.xy) >= resolution_out))
        return;

    Surface surface;
    surface.Build(thread_id.xy, resolution_out, true, false);

    bool early_exit_1 = pass_is_opaque() && surface.is_transparent(); // if this is an opaque pass, ignore all transparent pixels.
    bool early_exit_2 = pass_is_transparent() && surface.is_opaque(); // if this is a transparent pass, ignore all opaque pixels.
    bool early_exit_3 = pass_is_transparent() && surface.is_sky();    // if this is a transparent pass, ignore sky pixels (they only render in the opaque)
    if (early_exit_1 || early_exit_2 || early_exit_3)
        return;

    float4 color               = float4(0.0f, 0.0f, 0.0f, surface.alpha); // maintain surface alpha, in case FSR benefits when generating the masks
    float distance_from_camera = surface.camera_to_pixel_length;
    
    if (surface.is_sky())
    {
        color.rgb            += tex_environment.SampleLevel(samplers[sampler_bilinear_clamp], direction_sphere_uv(surface.camera_to_pixel), 0).rgb;
        color.a               = 1.0f;
        distance_from_camera  = FLT_MAX_10;
    }
    else // anything else
    {
        // get light samples
        float3 light_diffuse  = tex_light_diffuse[thread_id.xy].rgb;
        float3 light_specular = tex_light_specular[thread_id.xy].rgb;
        float3 emissive       = surface.emissive * surface.albedo * 10.0f;
        
        // transparent
        float3 light_refracted_background = 0.0f;
        if (surface.is_transparent())
        {
            // refraction
            light_refracted_background = translucency::refraction::get_color(surface);

            // water
            if (surface.is_water())
            {
                light_refracted_background *= translucency::water::get_color(surface, color.a);

                // override g-buffer albedo alpha, for the IBL pass, right after
                tex_uav2[thread_id.xy]  = float4(surface.albedo, color.a);
            }
        }
        
        // compose
        float3 light  = (light_diffuse + surface.gi) * surface.albedo + light_specular + emissive;
        color.rgb    += lerp(light_refracted_background, light, color.a);
    }

    // fog
    float3 volumetric_fog  = tex_light_volumetric[thread_id.xy].rgb;
    color.rgb             += got_fog_radial(distance_from_camera, buffer_frame.camera_position.xyz, buffer_frame.directional_light_intensity) + volumetric_fog;

    tex_uav[thread_id.xy] = saturate_16(color);
}
