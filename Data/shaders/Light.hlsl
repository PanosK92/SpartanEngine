/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =====================
#include "BRDF.hlsl"
#include "ShadowMapping.hlsl"
#include "VolumetricLighting.hlsl"
#include "Fog.hlsl"
//================================

[numthreads(thread_group_count_x, thread_group_count_y, 1)]
void mainCS(uint3 thread_id : SV_DispatchThreadID)
{
    if (thread_id.x >= uint(g_resolution.x) || thread_id.y >= uint(g_resolution.y))
        return;

    // Sample albedo
    float4 sample_albedo = tex_albedo.Load(int3(thread_id.xy, 0));

    // If this is a transparent pass, ignore all opaque pixels
    #if TRANSPARENT
    if (sample_albedo.a == 1.0f)
        return;
    #endif

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    
    // Sample there rest of the textures
    float4 sample_normal    = tex_normal.Load(int3(thread_id.xy, 0));
    float4 sample_material  = tex_material.Load(int3(thread_id.xy, 0));
    float sample_depth      = tex_depth.Load(int3(thread_id.xy, 0)).r;
    #if TRANSPARENT
    float sample_hbao       = 1.0f; // we don't do ao for transparents
    #else
    float sample_hbao       = tex_hbao.SampleLevel(sampler_point_clamp, uv, 0).r; // if hbao is disabled, the texture will be 1x1 white pixel, so we use a sampler
    #endif

    // Post-process samples
    int mat_id      = round(sample_normal.a * 65535);
    float occlusion = sample_material.a;

        // Create material
    Material material;
    material.albedo                 = sample_albedo;
    material.roughness              = sample_material.r;
    material.metallic               = sample_material.g;
    material.emissive               = sample_material.b;
    material.clearcoat              = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].x;
    material.clearcoat_roughness    = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].y;
    material.anisotropic            = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].z;
    material.anisotropic_rotation   = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].w;
    material.sheen                  = mat_sheen_sheenTint_pad[mat_id].x;
    material.sheen_tint             = mat_sheen_sheenTint_pad[mat_id].y;
    material.occlusion              = min(occlusion, sample_hbao);
    material.F0                     = lerp(0.04f, material.albedo.rgb, material.metallic);
    material.is_sky                 = mat_id == 0;
    
    // Fill surface struct
    Surface surface;
    surface.uv                      = uv;
    surface.depth                   = sample_depth;
    surface.position                = get_position(surface.depth, surface.uv);
    surface.normal                  = normal_decode(sample_normal.xyz);
    surface.camera_to_pixel         = surface.position - g_camera_position.xyz;
    surface.camera_to_pixel_length  = length(surface.camera_to_pixel);
    surface.camera_to_pixel         = normalize(surface.camera_to_pixel);

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

    // Pre-compute n_dot_l since it's used in many places
    surface.n_dot_l = saturate(dot(surface.normal, -light.direction));
    
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
        shadow.a = clamp(shadow.a, material.albedo.a, 1.0f);
        #endif
    }

    float3 light_diffuse    = 0.0f;
    float3 light_specular   = 0.0f;
    float3 light_volumetric = 0.0f;
    
    // Compute multi-bounce ambient occlusion
    float3 multi_bounce_ao = MultiBounceAO(material.occlusion, sample_albedo.rgb);

    // Compute final radiance
    light.radiance = light.color * light.intensity * light.attenuation * surface.n_dot_l * shadow.rgb * shadow.a * multi_bounce_ao;

    // Reflectance equation
    [branch]
    if (any(light.radiance) && !material.is_sky)
    {
        // Compute some vectors and dot products
        float3 l        = -light.direction;
        float3 v        = -surface.camera_to_pixel;
        float3 h        = normalize(v + l);
        float l_dot_h   = saturate(dot(l, h));
        float v_dot_h   = saturate(dot(v, h));
        float n_dot_v   = saturate(dot(surface.normal, v));
        float n_dot_l   = surface.n_dot_l;
        float n_dot_h   = saturate(dot(surface.normal, h));

        float3 diffuse_energy       = 1.0f;
        float3 reflective_energy    = 1.0f;
        
        // Specular
        if (material.anisotropic == 0.0f)
        {
            light_specular += BRDF_Specular_Isotropic(material, n_dot_v, n_dot_l, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }
        else
        {
            light_specular += BRDF_Specular_Anisotropic(material, surface, v, l, h, n_dot_v, n_dot_l, n_dot_h, l_dot_h, diffuse_energy, reflective_energy);
        }

        // Specular clearcoat
        if (material.clearcoat != 0.0f)
        {
            light_specular += BRDF_Specular_Clearcoat(material, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }

        // Sheen;
        if (material.sheen != 0.0f)
        {
            light_specular += BRDF_Specular_Sheen(material, n_dot_v, n_dot_l, n_dot_h, diffuse_energy, reflective_energy);
        }
        
        // Diffuse
        light_diffuse += BRDF_Diffuse(material, n_dot_v, n_dot_l, v_dot_h);

        // Tone down diffuse such as that only non metals have it
        light_diffuse *= diffuse_energy;

        /* Light - Subsurface scattering fast approximation - Will activate soon
        #if TRANSPARENT
        {
            const float thickness_edge  = 0.1f;
            const float thickness_face  = 1.0f;
            const float distortion      = 0.65f;
            const float ambient         = 1.0f - material.albedo.a;
            const float scale           = 1.0f;
            const float power           = 0.8f;
            
            float thickness = lerp(thickness_edge, thickness_face, n_dot_v);
            float3 h        = normalize(l + surface.normal * distortion);
            float v_dot_h   = pow(saturate(dot(v, -h)), power) * scale;
            float intensity = (v_dot_h + ambient) * thickness;
            
            light_diffuse += material.albedo.rgb * light.color * intensity;
        }
        #endif
        */

        // Light - Reflection
        #if SCREEN_SPACE_REFLECTIONS
        float2 sample_ssr = tex_ssr.Load(int3(thread_id.xy, 0)).xy;
        [branch]
        if (sample_ssr.x * sample_ssr.y != 0.0f)
        {
            // saturate as reflections will accumulate int tex_frame overtime, causing more light to go out that it comes in.
            float3 light_reflection = saturate(tex_frame.SampleLevel(sampler_bilinear_clamp, sample_ssr, 0).rgb);
            light_reflection *= 1.0f - material.roughness; // fade with roughness as we don't have blurry screen space reflections yet
            light_diffuse += light_reflection * diffuse_energy;
            light_specular += light_reflection * reflective_energy;
        }
        #endif
    }

    // Volumetric lighting
    #if VOLUMETRIC
    {
        light_volumetric += VolumetricLighting(surface, light) * light.color * light.intensity * get_fog_factor(surface);
    }
    #endif
    
     // Light - Emissive
    float3 light_emissive = material.emissive * material.albedo.rgb * 50.0f;
    
    // Light - Refraction
    float3 light_refraction = 0.0f;
    #if TRANSPARENT
    {
        float ior               = 1.5; // glass
        float2 normal2D         = mul((float3x3)g_view, sample_normal.xyz).xy;
        float2 refraction_uv    = uv + normal2D * ior * 0.03f;
    
        // Only refract what's behind the surface
        [branch]
        if (get_linear_depth(refraction_uv) > get_linear_depth(surface.depth))
        {
            light_refraction = tex_frame.SampleLevel(sampler_bilinear_clamp, refraction_uv, 0).rgb;
        }
        else
        {
            light_refraction = tex_frame.Load(int3(thread_id.xy, 0)).rgb;
        }
    
        light_refraction = lerp(light_refraction, light_diffuse, material.albedo.a);
    }
    #endif
    
    tex_out_rgb[thread_id.xy]   += saturate_16(light_diffuse * light.radiance + light_emissive + light_refraction);
    tex_out_rgb2[thread_id.xy]  += saturate_16(light_specular * light.radiance);
    tex_out_rgb3[thread_id.xy]  += saturate_16(light_volumetric);
}
