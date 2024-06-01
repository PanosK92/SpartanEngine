/*
Copyright(c) 2016-2024 Panos Karabelas

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
    // properties
    uint   flags;
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
    float3 sheen_tint;
    float  subsurface_scattering;
    float  ior;
    float  occlusion;
    float3 gi;
    float3 emissive;
    float3 F0;
    uint2  pos;
    float2 uv;
    float  depth;
    float3 position;
    float3 normal;
    float3 camera_to_pixel;
    float  camera_to_pixel_length;
    float3 specular_energy;
    float3 diffuse_energy;

    // easy access to certain properties
    bool has_single_texture_roughness_metalness() { return flags & uint(1U << 0);  }
    bool has_texture_height()                     { return flags & uint(1U << 1);  }
    bool has_texture_normal()                     { return flags & uint(1U << 2);  }
    bool has_texture_albedo()                     { return flags & uint(1U << 3);  }
    bool has_texture_roughness()                  { return flags & uint(1U << 4);  }
    bool has_texture_metalness()                  { return flags & uint(1U << 5);  }
    bool has_texture_alpha_mask()                 { return flags & uint(1U << 6);  }
    bool has_texture_emissive()                   { return flags & uint(1U << 7);  }
    bool has_texture_occlusion()                  { return flags & uint(1U << 8);  }
    bool texture_slope_based()                    { return flags & uint(1U << 9);  }
    bool vertex_animate_wind()                    { return flags & uint(1U << 10); }
    bool vertex_animate_water()                   { return flags & uint(1U << 11); }
    bool is_tessellated()                         { return flags & uint(1U << 12); }
    bool is_water()                               { return ior == 1.33f; }
    bool is_glass()                               { return ior == 1.52f; }
    bool is_sky()                                 { return alpha == 0.0f; }
    bool is_opaque()                              { return alpha == 1.0f; }
    bool is_transparent()                         { return alpha > 0.0f && alpha < 1.0f && ior != 1.0f; } // the ior is to avoid treating alpha tested objects as transparent
    
    void Build(uint2 position_screen, float2 resolution_out, bool use_albedo, bool replace_color_with_one)
    {
        // access resources
        float4 sample_albedo   = use_albedo ? tex_albedo[position_screen] : 0.0f;
        float4 sample_normal   = tex_normal[position_screen];
        float4 sample_material = tex_material[position_screen];
        float sample_depth     = tex_depth[position_screen].r;
        Material material      = buffer_materials[sample_normal.a];
        
        // fill properties
        pos                   = position_screen;
        uv                    = (position_screen + 0.5f) / (resolution_out * buffer_frame.resolution_scale);
        depth                 = sample_depth;
        normal                = sample_normal.xyz;
        flags                 = material.flags;
        albedo                = replace_color_with_one ? 1.0f : sample_albedo.rgb;
        alpha                 = sample_albedo.a;
        roughness             = sample_material.r;
        metallic              = sample_material.g;
        emissive              = sample_material.b;
        F0                    = lerp(0.04f, albedo, metallic);
        anisotropic           = material.anisotropic;
        anisotropic_rotation  = material.anisotropic_rotation;
        clearcoat             = material.clearcoat;
        clearcoat_roughness   = material.clearcoat_roughness;
        sheen                 = material.sheen;
        sheen_tint            = material.sheen_tint;
        subsurface_scattering = material.subsurface_scattering;
        ior                   = material.ior;
        specular_energy       = 1.0f;
        diffuse_energy        = 1.0f;

        // roughness is authored as perceptual roughness, as is convention
        roughness_alpha         = roughness * roughness;
        roughness_alpha_squared = roughness_alpha * roughness_alpha;

        // ssgi
        {
            occlusion = 1.0f;
            gi        = 0.0f;

            if (is_ssgi_enabled() && pass_is_opaque())
            {
                float2 uv_unscaled = (position_screen + 0.5f) / resolution_out;
                float4 ssgi        = tex_ssgi.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp_border), uv_unscaled, 0.0f);
                occlusion          = ssgi.a;
                gi                 = ssgi.rgb;

                // combine occlusion with material occlusion
                occlusion = min(sample_material.a, occlusion);
            }
        }

        position               = get_position(depth, uv);
        camera_to_pixel        = position - buffer_frame.camera_position.xyz;
        camera_to_pixel_length = length(camera_to_pixel);
        camera_to_pixel        = normalize(camera_to_pixel);
    }
};

struct Light
{
    // properties
    uint   flags;
    float3 color;
    float3 position;
    float  intensity;
    float3 to_pixel;
    float3 forward;
    float  distance_to_pixel;
    float  angle;
    float  near;
    float  far;
    float3 radiance;
    float  n_dot_l;
    float  attenuation;
    float2 resolution;
    float2 texel_size;
    matrix transform[2];
 
    bool is_directional()           { return flags & uint(1U << 0); }
    bool is_point()                 { return flags & uint(1U << 1); }
    bool is_spot()                  { return flags & uint(1U << 2); }
    bool has_shadows()              { return flags & uint(1U << 3); }
    bool has_shadows_transparent()  { return flags & uint(1U << 4); }
    bool has_shadows_screen_space() { return flags & uint(1U << 5); }
    bool is_volumetric()            { return flags & uint(1U << 6); }
    uint get_array_index()          { return (uint)pass_get_f3_value2().y; }

    // attenuation over distance
    float compute_attenuation_distance(const float3 surface_position)
    {
        float distance_to_pixel = length(surface_position - position);
        float attenuation       = saturate(1.0f - distance_to_pixel / far);
        return attenuation * attenuation;
    }

    float compute_attenuation_angle()
    {
        float cos_outer         = cos(angle);
        float cos_inner         = cos(angle * 0.9f);
        float cos_outer_squared = cos_outer * cos_outer;
        float scale             = 1.0f / max(0.001f, cos_inner - cos_outer);
        float offset            = -cos_outer * scale;
        float cd                = dot(to_pixel, forward);
        float attenuation       = saturate(cd * scale + offset);
        
        return attenuation * attenuation;
    }

    float2 compute_resolution()
    {
        float2 resolution;

        uint layer_count;
        tex_light_depth.GetDimensions(resolution.x, resolution.y, layer_count);

        return resolution;
    }

    float compute_attenuation(const float3 surface_position)
    {
        float attenuation = 0.0f;
        
        if (is_directional())
        {
            attenuation = saturate(dot(-forward.xyz, float3(0.0f, 1.0f, 0.0f)));
        }
        else if (is_point())
        {
            attenuation = compute_attenuation_distance(surface_position);
        }
        else if (is_spot())
        {
            attenuation = compute_attenuation_distance(surface_position) * compute_attenuation_angle();
        }

        return attenuation;
    }

    float3 compute_direction(float3 light_position, float3 fragment_position)
    {
        float3 direction = 0.0f;
        
        if (is_directional())
        {
            direction = normalize(forward.xyz);
        }
        else if (is_point() || is_spot())
        {
            direction = normalize(fragment_position - light_position);
        }

        return direction;
    }

    float compare_depth(float3 uv, float compare)
    {
        return tex_light_depth.SampleCmpLevelZero(samplers_comparison[sampler_compare_depth], uv, compare).r;
    }
    
    float sample_depth(float3 uv)
    {
         return tex_light_depth.SampleLevel(samplers[sampler_bilinear_clamp_border], uv, 0).r;
    }
    
    float3 sample_color(float3 uv)
    {
         return tex_light_color.SampleLevel(samplers[sampler_bilinear_clamp_border], uv, 0).rgb;
    }

    void Build(float3 surface_position, float3 surface_normal, float occlusion)
    {
        Light_ light = buffer_lights[(uint)pass_get_f3_value2().x];

        flags             = light.flags;
        transform         = light.transform;
        color             = light.color.rgb;
        position          = light.position.xyz;
        intensity         = light.intensity;
        near              = 0.01f;
        far               = light.range;
        angle             = light.angle;
        forward           = light.direction.xyz;
        distance_to_pixel = length(surface_position - position);
        to_pixel          = compute_direction(position, surface_position);
        n_dot_l           = saturate(dot(surface_normal, -to_pixel));
        attenuation       = compute_attenuation(surface_position);
        resolution        = compute_resolution();
        texel_size        = 1.0f / resolution;

        // apply occlusion
        float occlusion_factor = is_ssgi_enabled() ? occlusion : 1.0;
        radiance               = color * intensity * attenuation * n_dot_l * occlusion_factor;
    }

    void Build(Surface surface)
    {
        Build(surface.position, surface.normal, surface.occlusion);
    }

    void Build()
    {
        Surface surface;
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
        n = normalize(surface.normal);           // outward direction of surface point
        v = normalize(-surface.camera_to_pixel); // direction from surface point to view
        l = normalize(-light.to_pixel);          // direction from surface point to light
        h = normalize(l + v);                    // direction of the vector between l and v

        n_dot_l = saturate(dot(n, l));
        n_dot_v = saturate(dot(n, v));
        n_dot_h = saturate(dot(n, h));
        l_dot_h = saturate(dot(l, h));
        v_dot_h = saturate(dot(v, h));
    }
};

#endif // SPARTAN_COMMON_STRUCT
