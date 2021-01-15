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

//= INCLUDES =========
#include "Common.hlsl"
//====================

#if DIRECTIONAL
static const uint light_array_size = 4;
#else
static const uint light_array_size = 1;
#endif

// Attenuation over distance
float get_light_attenuation_distance(const Light light, const float3 position)
{
    float distance_to_pixel = length(position - light.position);
    float attenuation       = saturate(1.0f - distance_to_pixel / light.far);
    return attenuation * attenuation;
}

// Attenuation over angle (approaching the outer cone)
float get_light_attenuation_angle(const Light light, const float3 direction)
{
    float light_dot_pixel   = dot(direction, light.direction);
    float cutoffAngle       = 1.0f - light.angle;
    float epsilon           = cutoffAngle - cutoffAngle * 0.9f;
    float attenuation       = saturate((light_dot_pixel - cutoffAngle) / epsilon);
    return attenuation * attenuation;
}

// Final attenuation for all suported lights
float get_light_attenuation(const Light light, const float3 position)
{
    float attenuation = 0.0f;
    
    #if DIRECTIONAL
    attenuation   = saturate(dot(-cb_light_direction.xyz, float3(0.0f, 1.0f, 0.0f)));
    #elif POINT
    attenuation   = get_light_attenuation_distance(light, position);
    #elif SPOT
    attenuation   = get_light_attenuation_distance(light, position) * get_light_attenuation_angle(light, cb_light_direction.xyz);
    #endif

    return attenuation;
}

float3 get_light_direction(Light light, Surface surface)
{
    float3 direction = 0.0f;
    
    #if DIRECTIONAL
    direction   = normalize(cb_light_direction.xyz);
    #elif POINT
    direction   = normalize(surface.position - light.position);
    #elif SPOT
    direction   = normalize(surface.position - light.position);
    #endif

    return direction;
}

//= INCLUDES ================
#include "BRDF.hlsl"
#include "ShadowMapping.hlsl"
#include "VolumetricFog.hlsl"
#include "Fog.hlsl"
//===========================

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // Sample albedo
    float4 sample_albedo = tex_albedo[thread_id.xy];

    // If this is a transparent pass, ignore all opaque pixels
    #if TRANSPARENT
    if (sample_albedo.a == 1.0f)
        return;
    #endif

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;

    // Sample ssao
    #if TRANSPARENT
    float sample_ssao   = 1.0f; // we don't do ao for transparents
    #else
    float sample_ssao   = tex_ssao.SampleLevel(sampler_point_clamp, uv, 0).r; // if ssao is disabled, the texture will be 1x1 white pixel, so we use a sampler
    #endif

    // Create material
    Surface surface;
    surface.Build(uv, float4(1.0f, 1.0f, 1.0f, sample_albedo.a), tex_normal[thread_id.xy], tex_material[thread_id.xy], tex_depth[thread_id.xy].r, sample_ssao);

    // Fill light struct
    Light light;
    light.color             = cb_light_color.rgb;
    light.position          = cb_light_position.xyz;
    light.near              = 0.1f;
    light.intensity         = cb_light_intensity_range_angle_bias.x;
    light.far               = cb_light_intensity_range_angle_bias.y;
    light.angle             = cb_light_intensity_range_angle_bias.z;
    light.bias              = cb_light_intensity_range_angle_bias.w;
    light.normal_bias       = cb_light_normal_bias;
    light.distance_to_pixel = length(surface.position - light.position);
    light.direction         = get_light_direction(light, surface);
    light.attenuation       = get_light_attenuation(light, surface.position);
    light.n_dot_l           = saturate(dot(surface.normal, -light.direction)); // Pre-compute n_dot_l since it's used in many places
    light.radiance          = light.color * light.intensity * light.attenuation * light.n_dot_l;

    // Shadows
    float4 shadow = 1.0f;
    {
        // Shadow mapping
        #if SHADOWS
        {
            shadow = Shadow_Map(surface, light);
        }
        #endif
        
        // Screen space shadows
        #if SHADOWS_SCREEN_SPACE
        {
            shadow.a = min(shadow.a, ScreenSpaceShadows(surface, light));
        }
        #endif

        // Ensure that the shadow is as transparent as the material
        #if TRANSPARENT
        shadow.a = clamp(shadow.a, surface.albedo.a, 1.0f);
        #endif
    }

    // Compute multi-bounce ambient occlusion
    float3 multi_bounce_ao = MultiBounceAO(surface.occlusion, sample_albedo.rgb);

    // Compute final radiance
    light.radiance *= shadow.rgb * shadow.a * multi_bounce_ao;

    float3 light_diffuse    = 0.0f;
    float3 light_specular   = 0.0f;
    float3 light_volumetric = 0.0f;

    // Reflectance equation
    [branch]
    if (any(light.radiance) && !surface.is_sky())
    {
        // Compute some vectors and dot products
        float3 l        = -light.direction;
        float3 v        = -surface.camera_to_pixel;
        float3 h        = normalize(v + l);
        float l_dot_h   = saturate(dot(l, h));
        float v_dot_h   = saturate(dot(v, h));
        float n_dot_v   = saturate(dot(surface.normal, v));
        float n_dot_h   = saturate(dot(surface.normal, h));

        float3 diffuse_energy       = 1.0f;
        float3 reflective_energy    = 1.0f;
        
        // Specular
        if (surface.anisotropic == 0.0f)
        {
            light_specular += BRDF_Specular_Isotropic(surface, n_dot_v, light.n_dot_l, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }
        else
        {
            light_specular += BRDF_Specular_Anisotropic(surface, v, l, h, n_dot_v, light.n_dot_l, n_dot_h, l_dot_h, diffuse_energy, reflective_energy);
        }

        // Specular clearcoat
        if (surface.clearcoat != 0.0f)
        {
            light_specular += BRDF_Specular_Clearcoat(surface, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }

        // Sheen;
        if (surface.sheen != 0.0f)
        {
            light_specular += BRDF_Specular_Sheen(surface, n_dot_v, light.n_dot_l, n_dot_h, diffuse_energy, reflective_energy);
        }
        
        // Diffuse
        light_diffuse += BRDF_Diffuse(surface, n_dot_v, light.n_dot_l, v_dot_h);

        /* Light - Subsurface scattering fast approximation - Will activate soon
        #if TRANSPARENT
        {
            const float thickness_edge  = 0.1f;
            const float thickness_face  = 1.0f;
            const float distortion      = 0.65f;
            const float ambient         = 1.0f - surface.albedo.a;
            const float scale           = 1.0f;
            const float power           = 0.8f;
            
            float thickness = lerp(thickness_edge, thickness_face, n_dot_v);
            float3 h        = normalize(l + surface.normal * distortion);
            float v_dot_h   = pow(saturate(dot(v, -h)), power) * scale;
            float intensity = (v_dot_h + ambient) * thickness;
            
            light_diffuse += surface.albedo.rgb * light.color * intensity;
        }
        #endif
        */

        // Tone down diffuse such as that only non metals have it
        light_diffuse *= diffuse_energy;
    }

    // Volumetric lighting
    #if VOLUMETRIC
    {
        light_volumetric += VolumetricLighting(surface, light) * light.color * light.intensity * get_fog_factor(surface);
    }
    #endif
    
     // Light - Emissive
    float3 light_emissive = surface.emissive * surface.albedo.rgb * 50.0f;

    tex_out_rgb[thread_id.xy]   += saturate_16(light_diffuse * light.radiance + light_emissive);
    tex_out_rgb2[thread_id.xy]  += saturate_16(light_specular * light.radiance);
    tex_out_rgb3[thread_id.xy]  += saturate_16(light_volumetric);
}
