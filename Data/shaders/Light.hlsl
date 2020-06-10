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
//================================

struct PixelOutputType
{
    float3 diffuse      : SV_Target0;
    float3 specular     : SV_Target1;
    float3 volumetric   : SV_Target2;
};

PixelOutputType mainPS(Pixel_PosUv input)
{
    PixelOutputType light_out;
    light_out.diffuse       = 0.0f;
    light_out.specular      = 0.0f;
    light_out.volumetric    = 0.0f;

    // Sample textures
    float4 sample_albedo    = tex_albedo.Sample(sampler_point_clamp, input.uv);
    float4 sample_normal    = tex_normal.Sample(sampler_point_clamp, input.uv);
    float4 sample_material  = tex_material.Sample(sampler_point_clamp, input.uv);
    float4 sample_hbao      = tex_hbao.Sample(sampler_point_clamp, input.uv);

    // Post-process samples
    int mat_id      = round(sample_normal.a * 65535);
    float occlusion = sample_material.a;
    
    // Fill surface struct
    Surface surface;
    surface.uv                      = input.uv;
    surface.depth                   = tex_depth.Sample(sampler_point_clamp, surface.uv).r;
    surface.position                = get_position(surface.depth, surface.uv);
    surface.normal                  = normal_decode(sample_normal.xyz);
    surface.camera_to_pixel         = normalize(surface.position - g_camera_position.xyz);
    surface.camera_to_pixel_length  = length(surface.position - g_camera_position.xyz);

    // Create material
    Material material;
    {
        material.albedo                 = sample_albedo.rgb;
        material.roughness              = sample_material.r;
        material.metallic               = sample_material.g;
        material.emissive               = sample_material.b;
        material.clearcoat              = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].x;
        material.clearcoat_roughness    = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].y;
        material.anisotropic            = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].z;
        material.anisotropic_rotation   = mat_clearcoat_clearcoatRough_aniso_anisoRot[mat_id].w;
        material.sheen                  = mat_sheen_sheenTint_pad[mat_id].x;
        material.sheen_tint             = mat_sheen_sheenTint_pad[mat_id].y;
        material.occlusion              = min(occlusion, sample_hbao.a);
        material.F0                     = lerp(0.04f, material.albedo, material.metallic);
        material.is_transparent         = sample_albedo.a != 1.0f;
        material.is_sky                 = mat_id == 0;
    }

    // Fill light struct
    float light_intensity = intensity_range_angle_bias.x;
    
    Light light;
    light.color             = color.xyz * light_intensity;
    light.position          = position.xyz;
    light.range             = intensity_range_angle_bias.y;
    light.angle             = intensity_range_angle_bias.z;
    light.bias              = intensity_range_angle_bias.w;
    light.normal_bias       = normal_bias;
    light.distance_to_pixel = length(surface.position - light.position);
    #if DIRECTIONAL
    light.array_size    = 4;
    light.direction     = direction.xyz; 
    light.attenuation   = 1.0f;
    #elif POINT
    light.array_size    = 1;
    light.direction     = normalize(surface.position - light.position);
    light.attenuation   = saturate(1.0f - (light.distance_to_pixel / light.range)); light.attenuation *= light.attenuation;    
    #elif SPOT
    light.array_size    = 1;
    light.direction     = normalize(surface.position - light.position);
    float cutoffAngle   = 1.0f - light.angle;
    float theta         = dot(direction.xyz, light.direction);
    float epsilon       = cutoffAngle - cutoffAngle * 0.9f;
    light.attenuation   = saturate((theta - cutoffAngle) / epsilon); // attenuate when approaching the outer cone
    light.attenuation   *= saturate(1.0f - light.distance_to_pixel / light.range); light.attenuation *= light.attenuation;
    #endif
    light.color *= light.attenuation;
    
    // Shadow 
    {
        float4 shadow = 1.0f;
        
        // Shadow mapping
        #if SHADOWS
        {
            shadow = Shadow_Map(surface, light, material.is_transparent);

            // Volumetric lighting (requires shadow maps)
            #if VOLUMETRIC
            {
                light_out.volumetric.rgb = VolumetricLighting(surface, light);
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
    
        // Compute multi-bounce ambient occlusion
        float3 multi_bounce_ao = MultiBounceAO(material.occlusion, sample_albedo.rgb);

        // Modulate light with shadow color, visibility and ambient occlusion
        light.color *= shadow.rgb * shadow.a * multi_bounce_ao;
    }

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
        float n_dot_l   = saturate(dot(surface.normal, l));
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
        float2 sample_ssr = tex_ssr.Sample(sampler_point_clamp, input.uv).xy;
        [branch]
        if (sample_ssr.x * sample_ssr.y != 0.0f)
        {
            // saturate as reflections will accumulate int tex_frame overtime, causing more light to go out that it comes in.
            light_reflection = saturate(tex_frame.Sample(sampler_bilinear_clamp, sample_ssr.xy).rgb);
            light_reflection *= reflective_energy;
            light_reflection *= 1.0f - material.roughness; // fade with roughness as we don't have blurry screen space reflections yet
        }
        #endif

        float3 radiance = light.color * n_dot_l;
        
        light_out.diffuse.rgb   = saturate_16(diffuse * radiance);
        light_out.specular.rgb  = saturate_16((specular + specular_clearcoat + specular_sheen) * radiance + light_reflection);
    }

    return light_out;
}
