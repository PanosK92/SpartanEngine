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

//= INCLUDES =====================      
#include "BRDF.hlsl"
#include "ShadowMapping.hlsl"
#include "VolumetricLighting.hlsl"
#include "Fog.hlsl"
//================================

float attunation_distance(const Light light)
{
    float attenuation = saturate(1.0f - light.distance_to_pixel / light.far);
    return attenuation * attenuation;
}

float attunation_angle(const Light light)
{
    float cutoffAngle       = 1.0f - light.angle;
    float light_dot_pixel   = dot(direction.xyz, light.direction);
    float epsilon           = cutoffAngle - cutoffAngle * 0.9f;
    float attenuation       = saturate((light_dot_pixel - cutoffAngle) / epsilon); // attenuate when approaching the outer cone
    return attenuation * attenuation;
}

[numthreads(thread_group_count, thread_group_count, 1)]
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
    
    // Sample there rest of the textures
    float4 sample_normal    = tex_normal.Load(int3(thread_id.xy, 0));
    float4 sample_material  = tex_material.Load(int3(thread_id.xy, 0));
    float sample_depth      = tex_depth.Load(int3(thread_id.xy, 0)).r;
    #if TRANSPARENT
    float sample_hbao       = 1.0f; // we don't do ao for transparents
    #else
    float sample_hbao       = tex_hbao.Load(int3(thread_id.xy, 0)).r;
    #endif

    // Post-process samples
    int mat_id      = round(sample_normal.a * 65535);
    float occlusion = sample_material.a;

    const float2 uv = (thread_id.xy + 0.5f) / g_resolution;
    float3 light_diffuse    = 0.0f;
    float3 light_specular   = 0.0f;
    float3 light_volumetric = 0.0f;
    
    // Fill surface struct
    Surface surface;
    surface.uv                      = uv;
    surface.depth                   = sample_depth;
    surface.position                = get_position(surface.depth, surface.uv);
    surface.normal                  = normal_decode(sample_normal.xyz);
    surface.camera_to_pixel         = normalize(surface.position - g_camera_position.xyz);
    surface.camera_to_pixel_length  = length(surface.position - g_camera_position.xyz);

    // Create material
    Material material;
    {
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
    }

    // Fill light struct
    Light light;
    light.color             = color.rgb;
    light.position          = position.xyz;
    light.near              = 0.1f;
    light.far               = intensity_range_angle_bias.y;
    light.angle             = intensity_range_angle_bias.z;
    light.bias              = intensity_range_angle_bias.w;
    light.normal_bias       = normal_bias;
    light.distance_to_pixel = length(surface.position - light.position);
    #if DIRECTIONAL
    light.array_size    = 4;
    light.direction     = normalize(direction.xyz);
    light.color         *= saturate(dot(normalize(-direction.xyz), surface.normal)) * intensity_range_angle_bias.x;
    #elif POINT
    light.array_size    = 1;
    light.direction     = normalize(surface.position - light.position);
    light.color         *= intensity_range_angle_bias.x;
    light.color         *= attunation_distance(light); // attenuate
    #elif SPOT
    light.array_size    = 1;
    light.direction     = normalize(surface.position - light.position);
    light.color         *= intensity_range_angle_bias.x;
    light.color         *= attunation_distance(light) * attunation_angle(light); // attenuate
    #endif
    light.n_dot_l       = dot(-light.direction, surface.normal);
    
    // Compute shadows and volumetric fog/light
    float4 shadow = 1.0f;
    {
        // Shadow mapping
        #if SHADOWS
        {
            shadow = Shadow_Map(surface, light);

            // Volumetric lighting (requires shadow maps)
            #if VOLUMETRIC
            {
                light_volumetric += VolumetricLighting(surface, light) * get_fog_factor(surface) * 20.0f * color.rgb;
            }
            #endif
        }
        #endif
        
        // Screen space shadows
        #if SHADOWS_SCREEN_SPACE
        {
            shadow.a = min(shadow.a, ScreenSpaceShadows(surface, light));
        }
        #endif
    }

    // Compute multi-bounce ambient occlusion
    float3 multi_bounce_ao = MultiBounceAO(material.occlusion, sample_albedo.rgb);

    // Modulate light with shadow color, visibility and ambient occlusion
    light.color *= shadow.rgb * shadow.a * multi_bounce_ao;

    // Reflectance equation
    [branch]
    if (any(light.color) && !material.is_sky)
    {
        // Compute some vectors and dot products
        float3 l        = -light.direction;
        float3 v        = -surface.camera_to_pixel;
        float3 h        = normalize(v + l);
        float l_dot_h   = saturate(dot(l, h));
        float v_dot_h   = saturate(dot(v, h));
        float n_dot_v   = saturate(dot(surface.normal, v));
        float n_dot_l   = saturate(light.n_dot_l);
        float n_dot_h   = saturate(dot(surface.normal, h));

        float3 diffuse_energy       = 1.0f;
        float3 reflective_energy    = 1.0f;
        
        // Specular
        float3 specular = 0.0f;
        if (material.anisotropic == 0.0f)
        {
            specular = BRDF_Specular_Isotropic(material, n_dot_v, n_dot_l, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }
        else
        {
            specular = BRDF_Specular_Anisotropic(material, surface, v, l, h, n_dot_v, n_dot_l, n_dot_h, l_dot_h, diffuse_energy, reflective_energy);
        }

        // Specular clearcoat
        float3 specular_clearcoat = 0.0f;
        if (material.clearcoat != 0.0f)
        {
            specular_clearcoat = BRDF_Specular_Clearcoat(material, n_dot_h, v_dot_h, diffuse_energy, reflective_energy);
        }

        // Sheen
        float3 specular_sheen = 0.0f;
        if (material.sheen != 0.0f)
        {
            specular_sheen = BRDF_Specular_Sheen(material, n_dot_v, n_dot_l, n_dot_h, diffuse_energy, reflective_energy);
        }
        
        // Diffuse
        float3 diffuse = BRDF_Diffuse(material, n_dot_v, n_dot_l, v_dot_h);

        // Tone down diffuse such as that only non metals have it
        diffuse *= diffuse_energy;

        // SSR
        float3 light_reflection = 0.0f;
        #if SCREEN_SPACE_REFLECTIONS
        float2 sample_ssr = tex_ssr.Load(int3(thread_id.xy, 0)).xy;
        [branch]
        if (sample_ssr.x * sample_ssr.y != 0.0f)
        {
            // saturate as reflections will accumulate int tex_frame overtime, causing more light to go out that it comes in.
            light_reflection = saturate(tex_frame.SampleLevel(sampler_bilinear_clamp, sample_ssr, 0).rgb);
            light_reflection *= reflective_energy;
            light_reflection *= 1.0f - material.roughness; // fade with roughness as we don't have blurry screen space reflections yet
        }
        #endif

        float3 radiance = light.color * n_dot_l;
        
        light_diffuse  = saturate_16(diffuse * radiance);
        light_specular = saturate_16((specular + specular_clearcoat + specular_sheen) * radiance + light_reflection);
        
    }

    tex_out_rgb[thread_id.xy]   += light_diffuse;
    tex_out_rgb2[thread_id.xy]  += light_specular;
    tex_out_rgb3[thread_id.xy]  += saturate_16(light_volumetric);
}
