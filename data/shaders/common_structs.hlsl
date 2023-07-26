/*
Copyright(c) 2016-2023 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The abofe copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

#ifndef SPARTAN_COMMON_STRUCT
#define SPARTAN_COMMON_STRUCT

struct Surface
{
    // Properties
    float3 albedo;
    float  alpha;
    float  roughness;
    float  roughness_alpha;
    float  roughness_alpha_squared;
    float  metallic;
    float  clearcoat;
    float  clearcoat_roughness;
    float  anisotropic;
    float  anisotropic_rotation;
    float  sheen;
    float  sheen_tint;
    float  occlusion;
    float3 gi;
    float3 emissive;
    float3 F0;
    float2 uv;
    float  depth;
    float3 position;
    float3 normal;
    float3 camera_to_pixel;
    float  camera_to_pixel_length;
    float3 specular_energy;
    float3 diffuse_energy;
    
    // Activision GTAO paper: https://www.activision.com/cdn/research/s2016_pbs_activision_occlusion.pptx
    float3 multi_bounce_ao(float visibility, float3 albedo)
    {
        float3 a = 2.0404 * albedo - 0.3324;
        float3 b = -4.7951 * albedo + 0.6417;
        float3 c = 2.7552 * albedo + 0.6903;
        float x  = visibility;
        return max(x, ((x * a + b) * x + c) * x);
    }
    
    void Build(uint2 position_screen, bool use_albedo, bool use_ssgi, bool replace_color_with_one)
    {
        // Sample render targets
        float4 sample_albedo     = use_albedo ? tex_albedo[position_screen] : 0.0f;
        float4 sample_normal     = tex_normal[position_screen];
        float4 sample_material   = tex_material[position_screen];
        float4 sample_material_2 = tex_material_2[position_screen];
        float sample_depth       = get_depth(position_screen);

        // Misc
        uv     = (position_screen + 0.5f) / pass_get_resolution_out();
        depth  = sample_depth;
        normal = sample_normal.xyz;
 
        albedo               = replace_color_with_one ? 1.0f : sample_albedo.rgb;
        alpha                = sample_albedo.a;
        roughness            = sample_material.r;
        metallic             = sample_material.g;
        emissive             = sample_material.b;
        F0                   = lerp(0.04f, albedo, metallic);
        anisotropic          = sample_material_2.x;
        anisotropic_rotation = sample_material_2.y;
        clearcoat            = sample_material_2.z;
        clearcoat_roughness  = sample_material_2.w;
        sheen                = sample_normal.w;
        sheen_tint           = 1.0f; // TODO
        specular_energy      = 1.0f;
        diffuse_energy       = 1.0f;

        // Roughness is authored as perceptual roughness; as is convention,
        // convert to material roughness by squaring the perceptual roughness.
        roughness_alpha         = roughness * roughness;
        roughness_alpha_squared = roughness_alpha * roughness_alpha;

        // SSGI
        {
            occlusion = 1.0f;
            gi        = 0.0f;

            if (is_ssgi_enabled() && use_ssgi)
            {
                // Sample ssgi texture
                float4 ssgi = tex_ssgi[position_screen];
                occlusion   = ssgi.a;
                gi          = ssgi.rgb;

                // Combine occlusion with material occlusion (baked texture).
                occlusion = min(sample_material.a, occlusion);
            }
        }

        position               = get_position_ws_from_depth(uv, depth);
        camera_to_pixel        = position - buffer_frame.camera_position.xyz;
        camera_to_pixel_length = length(camera_to_pixel);
        camera_to_pixel        = normalize(camera_to_pixel);
    }

    bool is_sky()         { return alpha == 0.0f; }
    bool is_opaque()      { return alpha == 1.0f; }
    bool is_transparent() { return alpha > 0.0f && alpha < 1.0f; }
};

struct Light
{
    // Properties
    float3 color;
    float3 position;
    float  intensity;
    float3 to_pixel;
    float3 forward;
    float  distance_to_pixel;
    float  angle;
    float  bias;
    float  normal_bias;
    float  near;
    float  far;
    float3 radiance;
    float  n_dot_l;
    uint   array_size;
    float  attenuation;

    // attenuation functions are derived from Frostbite
    // https://media.contentapi.ea.com/content/dam/eacom/frostbite/files/course-notes-moving-frostbite-to-pbr-v2.pdf

    // Attenuation over distance
    float compute_attenuation_distance(const float3 surface_position)
    {
        float distance_to_pixel = length(surface_position - position);
        float attenuation       = saturate(1.0f - distance_to_pixel / far);
        return attenuation * attenuation;
    }

    // Attenuation over angle (approaching the outer cone)
    float compute_attenuation_angle()
    {
        float cos_outer         = cos(angle);
        float cos_inner         = cos(angle * 0.9f);
        float cos_outer_squared = cos_outer * cos_outer;
        float scale             = 1.0f / max(0.001f, cos_inner - cos_outer);
        float offset            = -cos_outer * scale;

        float cd           = dot(to_pixel, forward);
        float attenuation  = saturate(cd * scale + offset);
        return attenuation * attenuation;
    }

    // Final attenuation for all supported lights
    float compute_attenuation(const float3 surface_position)
    {
        float attenuation = 0.0f;
        
        if (light_is_directional())
        {
            attenuation = saturate(dot(-forward.xyz, float3(0.0f, 1.0f, 0.0f)));
        }
        else if (light_is_point())
        {
            attenuation = compute_attenuation_distance(surface_position);
        }
        else if (light_is_spot())
        {
            attenuation = compute_attenuation_distance(surface_position) * compute_attenuation_angle();
        }

        return attenuation;
    }

    float3 compute_direction(float3 light_position, float3 fragment_position)
    {
        float3 direction = 0.0f;
        
        if (light_is_directional())
        {
            direction = normalize(forward.xyz);
        }
        else if (light_is_point())
        {
            direction = normalize(fragment_position - light_position);
        }
        else if (light_is_spot())
        {
            direction = normalize(fragment_position - light_position);
        }

        return direction;
    }

    void Build(float3 surface_position, float3 surface_normal, float occlusion)
    {
        color             = buffer_light.color.rgb;
        position          = buffer_light.position.xyz;
        intensity         = buffer_light.intensity_range_angle_bias.x;
        far               = buffer_light.intensity_range_angle_bias.y;
        angle             = buffer_light.intensity_range_angle_bias.z;
        bias              = buffer_light.intensity_range_angle_bias.w;
        forward           = buffer_light.direction.xyz;
        normal_bias       = buffer_light.normal_bias;
        near              = 0.1f;
        distance_to_pixel = length(surface_position - position);
        to_pixel          = compute_direction(position, surface_position);
        n_dot_l           = saturate(dot(surface_normal, -to_pixel)); // Pre-compute n_dot_l since it's used in many places
        attenuation       = compute_attenuation(surface_position);
        array_size        = light_is_directional() ? 4 : 1;

        // Apply occlusion
        if (is_ssgi_enabled())
        {
            radiance = color * intensity * attenuation * n_dot_l * occlusion;
        }
        else
        {
            radiance = color * intensity * attenuation * n_dot_l;
        }
    }

    void Build(Surface surface)
    {
        Build(surface.position, surface.normal, surface.occlusion);
    }
};

struct AngularInfo
{
    float3 n;
    float3 l;
    float3 v;
    float3 h;
    float l_dot_h;
    float v_dot_h;
    float n_dot_v;
    float n_dot_h;
    float n_dot_l;

    void Build(Light light, Surface surface)
    {
        n = normalize(surface.normal);           // Outward direction of surface point
        v = normalize(-surface.camera_to_pixel); // Direction from surface point to view
        l = normalize(-light.to_pixel);          // Direction from surface point to light
        h = normalize(l + v);                    // Direction of the vector between l and v

        n_dot_l = saturate(dot(n, l));
        n_dot_v = saturate(dot(n, v));
        n_dot_h = saturate(dot(n, h));
        l_dot_h = saturate(dot(l, h));
        v_dot_h = saturate(dot(v, h));
    }
};

#endif // SPARTAN_COMMON_STRUCT
