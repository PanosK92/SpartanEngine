/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "common.hlsl"
//====================

#ifndef SPARTAN_COMMON_STRUCT
#define SPARTAN_COMMON_STRUCT

struct OceanParameters
{
    float scale;
    float spreadBlend;
    float swell;
    float gamma;
    float shortWavesFade;

    float windDirection;
    float fetch;
    float windSpeed;
    float repeatTime;
    float angle;
    float alpha;
    float peakOmega;

    float depth;
    float lowCutoff;
    float highCutoff;

    float foamDecayRate;
    float foamBias;
    float foamThreshold;
    float foamAdd;
    
    float displacementScale;
    float slopeScale;
    float lengthScale;

    float debugDisplacement;
    float debugSlope;
    float debugSynthesised;
};

struct Surface
{
    // properties
    uint   flags;
    float3 albedo;
    float  alpha;
    float  roughness;
    float  roughness_alpha;
    float  metallic;
    float  clearcoat;
    float  clearcoat_roughness;
    float  anisotropic;
    float  anisotropic_rotation;
    float  sheen;
    float  subsurface_scattering;
    float  occlusion;
    float3 emissive;
    float3 F0;
    uint2  pos;
    float2 uv;
    float  depth;
    float3 position;
    float3 normal;
    float3 bent_normal;
    float3 camera_to_pixel;
    float  camera_to_pixel_length;
    float3 diffuse_energy;

    OceanParameters ocean_parameters;

    // easy access to certain properties
    bool has_texture_height()            { return flags & uint(1U << 0);  }
    bool has_texture_normal()            { return flags & uint(1U << 1);  }
    bool has_texture_albedo()            { return flags & uint(1U << 2);  }
    bool has_texture_roughness()         { return flags & uint(1U << 3);  }
    bool has_texture_metalness()         { return flags & uint(1U << 4);  }
    bool has_texture_alpha_mask()        { return flags & uint(1U << 5);  }
    bool has_texture_emissive()          { return flags & uint(1U << 6);  }
    bool has_texture_occlusion()         { return flags & uint(1U << 7);  }
    bool is_terrain()                    { return flags & uint(1U << 8);  }
    bool has_wind_animation()            { return flags & uint(1U << 9);  }
    bool color_variation_from_instance() { return flags & uint(1U << 10); }
    bool is_grass_blade()                { return flags & uint(1U << 11); }
    bool is_flower()                     { return flags & uint(1U << 12); }
    bool is_water()                      { return flags & uint(1U << 13); }
    bool is_tessellated()                { return flags & uint(1U << 14); }
    bool is_ocean()                      { return flags & uint(1U << 16); }
    bool is_sky()                        { return alpha == 0.0f; }
    bool is_opaque()                     { return alpha == 1.0f; }
    bool is_transparent()                { return alpha > 0.0f && alpha < 1.0f; }
    
    void Build(uint2 position_screen, float2 resolution_out, bool use_albedo, bool replace_color_with_one)
    {
        // initialize properties
        pos = position_screen;
        uv = (position_screen + 0.5f) / resolution_out;

        // access resources
        float4 sample_albedo        = !use_albedo ? 0.0f : tex_albedo.SampleLevel(samplers[sampler_point_clamp], uv, 0);
        float4 sample_normal        = tex_normal.SampleLevel(samplers[sampler_point_clamp], uv, 0);
        float4 sample_material      = tex_material.SampleLevel(samplers[sampler_point_clamp], uv, 0);
        float sample_depth          = tex_depth.SampleLevel(samplers[sampler_point_clamp], uv, 0).r;
        MaterialParameters material = material_parameters[sample_normal.a];

        // initialize properties
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
        subsurface_scattering = material.subsurface_scattering;
        diffuse_energy        = 1.0f;
        
        // jonswap parameters
        ocean_parameters.alpha             = material.ocean_parameters.alpha;
        ocean_parameters.angle             = material.ocean_parameters.angle;
        ocean_parameters.fetch             = material.ocean_parameters.fetch;
        ocean_parameters.gamma             = material.ocean_parameters.gamma;
        ocean_parameters.peakOmega         = material.ocean_parameters.peakOmega;
        ocean_parameters.repeatTime        = material.ocean_parameters.repeatTime;
        ocean_parameters.scale             = material.ocean_parameters.scale;
        ocean_parameters.shortWavesFade    = material.ocean_parameters.shortWavesFade;
        ocean_parameters.spreadBlend       = material.ocean_parameters.spreadBlend;
        ocean_parameters.swell             = material.ocean_parameters.swell;
        ocean_parameters.windDirection     = material.ocean_parameters.windDirection;
        ocean_parameters.windSpeed         = material.ocean_parameters.windSpeed;
        ocean_parameters.depth             = material.ocean_parameters.depth;
        ocean_parameters.lowCutoff         = material.ocean_parameters.lowCutoff;
        ocean_parameters.highCutoff        = material.ocean_parameters.highCutoff;
        ocean_parameters.foamDecayRate     = material.ocean_parameters.foamDecayRate;
        ocean_parameters.foamBias          = material.ocean_parameters.foamBias;
        ocean_parameters.foamThreshold     = material.ocean_parameters.foamThreshold;
        ocean_parameters.foamAdd           = material.ocean_parameters.foamAdd;
        ocean_parameters.displacementScale = material.ocean_parameters.displacementScale;
        ocean_parameters.slopeScale        = material.ocean_parameters.slopeScale;
        ocean_parameters.lengthScale       = material.ocean_parameters.lengthScale;
        ocean_parameters.debugDisplacement = material.ocean_parameters.debugDisplacement;
        ocean_parameters.debugSlope        = material.ocean_parameters.debugSlope;
        ocean_parameters.debugSynthesised  = material.ocean_parameters.debugSynthesised;

        // roughness is authored as perceptual roughness, as is convention
        roughness_alpha = roughness * roughness;

        // ssao
        bent_normal          = float3(0, 1, 0);
        occlusion            = sample_material.a;
        bool is_fully_opaque = sample_albedo.a >= 0.999f;
        if (is_ssao_enabled() && is_fully_opaque)
        {
            float4 normal_sample = tex_ssao.SampleLevel(samplers[sampler_point_clamp], uv, 0);
            bent_normal          = normal_sample.rgb;
            occlusion            = min(sample_material.a, normal_sample.a); // use the minimum of the ssao and material occlusion
        }
        
        if (!is_fully_opaque)
        {
            occlusion = 1.0f;
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
    float  area_width;
    float  area_height;
    float3 radiance;
    float  n_dot_l;
    float  attenuation;
    float2 resolution;
    matrix transform[6];
    uint screen_space_shadows_slice_index;
    float2 atlas_offset[6];
    float2 atlas_scale[6];
    float2 atlas_texel_size[6];
 
    bool is_directional()           { return flags & uint(1U << 0); }
    bool is_point()                 { return flags & uint(1U << 1); }
    bool is_spot()                  { return flags & uint(1U << 2); }
    bool is_area()                  { return flags & uint(1U << 6); }
    bool has_shadows()              { return flags & uint(1U << 3); }
    bool has_shadows_screen_space() { return flags & uint(1U << 4); }
    bool is_volumetric()            { return flags & uint(1U << 5); }

    float compute_attenuation_distance(const float3 surface_position)
    {
        float d = length(surface_position - position);
        
        // 1. Physically correct Inverse Square Law
        // We add a small epsilon (0.0001) to prevent division by zero at the light source
        float attenuation_phys = 1.0f / (d * d + 0.0001f);
    
        // 2. Windowing function (Forces light to 0 at 'far' distance)
        float distance_falloff  = saturate(1.0f - d / far);
        distance_falloff       *= distance_falloff; // square it for smoother fade
    
        return attenuation_phys * distance_falloff;
    }

    float compute_attenuation_angle()
    {
        float cos_outer   = cos(angle);
        float cos_inner   = cos(angle * 0.9f);
        float scale       = 1.0f / max(0.001f, cos_inner - cos_outer);
        float offset      = -cos_outer * scale;
        float cd          = dot(to_pixel, forward);
        float attenuation = saturate(cd * scale + offset);
        
        return attenuation * attenuation;
    }

    float2 compute_resolution()
    {
        return 1.0f / atlas_texel_size[0]; // assuming all slices are the same resolution
    }

    // builds an orthonormal basis for the area light, handling all orientations including straight up/down
    void compute_area_light_basis(out float3 light_right, out float3 light_up)
    {
        // choose a reference vector that's not parallel to forward
        float3 ref = abs(forward.y) < 0.999f ? float3(0, 1, 0) : float3(1, 0, 0);
        
        light_right = normalize(cross(ref, forward));
        light_up    = normalize(cross(forward, light_right));
    }

    // finds the closest point on the rectangular area light to the surface position
    float3 compute_closest_point_on_area(const float3 surface_position)
    {
        float3 light_right, light_up;
        compute_area_light_basis(light_right, light_up);
        
        float3 to_surface = surface_position - position;
        
        // project onto light plane and clamp to rectangle bounds
        float half_width  = area_width  * 0.5f;
        float half_height = area_height * 0.5f;
        
        float right_proj = clamp(dot(to_surface, light_right), -half_width, half_width);
        float up_proj    = clamp(dot(to_surface, light_up), -half_height, half_height);
        
        return position + light_right * right_proj + light_up * up_proj;
    }

    // finds the representative point on the area light for specular calculations
    // this is the point that contributes most to the specular highlight based on the reflected view ray
    float3 compute_representative_point_on_area(const float3 surface_position, const float3 surface_normal, const float3 view_direction)
    {
        float3 light_right, light_up;
        compute_area_light_basis(light_right, light_up);
        
        // compute reflection vector
        float3 reflection = reflect(-view_direction, surface_normal);
        
        // find where the reflection ray intersects the light plane
        // plane equation: dot(p - position, forward) = 0
        float3 to_light  = position - surface_position;
        float denom      = dot(reflection, -forward);
        
        float3 representative_point;
        
        if (abs(denom) > 0.0001f)
        {
            // ray intersects the plane
            float t            = dot(to_light, -forward) / denom;
            float3 plane_point = surface_position + reflection * max(t, 0.0f);
            
            // project intersection point onto light rectangle and clamp
            float3 point_on_plane = plane_point - position;
            float half_width      = area_width  * 0.5f;
            float half_height     = area_height * 0.5f;
            
            float right_proj = clamp(dot(point_on_plane, light_right), -half_width, half_width);
            float up_proj    = clamp(dot(point_on_plane, light_up), -half_height, half_height);
            
            representative_point = position + light_right * right_proj + light_up * up_proj;
        }
        else
        {
            // reflection is parallel to light plane, fall back to closest point
            representative_point = compute_closest_point_on_area(surface_position);
        }
        
        return representative_point;
    }

    float compute_attenuation_area(const float3 surface_position)
    {
        // find closest point on the area light rectangle to the surface
        float3 closest_point = compute_closest_point_on_area(surface_position);
        
        // compute distance from closest point
        float d = length(surface_position - closest_point);
        
        // inverse square falloff with small epsilon to prevent division by zero
        float attenuation = 1.0f / (d * d + 0.0001f);
        
        // windowing function (forces light to 0 at range)
        float distance_falloff = saturate(1.0f - d / far);
        distance_falloff *= distance_falloff;
        
        return attenuation * distance_falloff;
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
        else if (is_area())
        {
            attenuation = compute_attenuation_area(surface_position);
        }

        return attenuation;
    }

    float compute_attenuation_volumetric(const float3 vol_position)
    {
        float atten = 1.0f;

        if (is_directional())
        {
            // keep sun-elevation atten for consistency, as it's global (no dist)
            atten = saturate(dot(-forward.xyz, float3(0.0f, 1.0f, 0.0f)));
        }
        else if (is_point() || is_spot() || is_area())
        {
            float dist_to_vol = length(vol_position - position);
            float atten_dist  = saturate(1.0f - dist_to_vol / far);
            atten_dist       *= atten_dist;

            atten = atten_dist;

            if (is_spot())
            {
                float3 to_vol           = normalize(vol_position - position); // direction from light to point
                float cos_outer         = cos(angle);
                float cos_inner         = cos(angle * 0.9f);
                float cos_outer_squared = cos_outer * cos_outer;
                float scale             = 1.0f / max(0.001f, cos_inner - cos_outer);
                float offset            = -cos_outer * scale;
                float cd                = dot(to_vol, forward); // use per-sample direction
                float atten_angle       = saturate(cd * scale + offset);
                atten_angle             *= atten_angle;

                atten *= atten_angle;
            }
        }

        return atten;
    }
    
    float3 compute_direction(float3 light_position, float3 fragment_position)
    {
        float3 direction = 0.0f;
        
        if (is_directional())
        {
            direction = normalize(forward.xyz);
        }
        else if (is_point() || is_spot() || is_area())
        {
            direction = normalize(fragment_position - light_position);
        }

        return direction;
    }

    float compare_depth(float3 uv, float compare)
    {
        uint array_index = (uint)uv.z;
    
        // compute atlas UV
        float2 atlas_uv = atlas_offset[array_index] + uv.xy * atlas_scale[array_index];
    
        // clamp to slice bounds
        float2 min_uv = atlas_offset[array_index];
        float2 max_uv = atlas_offset[array_index] + atlas_scale[array_index];
        atlas_uv      = clamp(atlas_uv, min_uv, max_uv);
    
        // comparison sampling
        return tex2.SampleCmpLevelZero(samplers_comparison[sampler_compare_depth], atlas_uv, compare).r;
    }
    
    float sample_depth(float3 uv)
    {
        uint array_index = (uint)uv.z;
    
        // compute atlas UV
        float2 atlas_uv = atlas_offset[array_index] + uv.xy * atlas_scale[array_index];
    
        // clamp to slice bounds
        float2 min_uv = atlas_offset[array_index];
        float2 max_uv = atlas_offset[array_index] + atlas_scale[array_index];
        atlas_uv      = clamp(atlas_uv, min_uv, max_uv);
    
        // normal sampling
        return tex2.SampleLevel(samplers[sampler_bilinear_clamp_border], atlas_uv, 0).r;
    }

    void Build(uint index, Surface surface)
    {
        LightParameters light            = light_parameters[index];
        flags                            = light.flags;
        color                            = light.color.rgb;
        position                         = light.position.xyz;
        intensity                        = light.intensity;
        near                             = 0.01f;
        far                              = light.range;
        angle                            = light.angle;
        area_width                       = light.area_width;
        area_height                      = light.area_height;
        forward                          = (is_point() && !is_area()) ? float3(0.0f, 0.0f, 1.0f) : light.direction.xyz;
        distance_to_pixel                = length(surface.position - position);
        
        // for area lights, use representative point for accurate specular reflections
        // this makes rectangular area lights produce elongated reflections instead of circular ones
        if (is_area())
        {
            float3 view_direction       = normalize(-surface.camera_to_pixel);
            float3 representative_point = compute_representative_point_on_area(surface.position, surface.normal, view_direction);
            to_pixel                    = normalize(surface.position - representative_point);
        }
        else
        {
            to_pixel = compute_direction(position, surface.position);
        }
        
        n_dot_l = saturate(dot(surface.normal, -to_pixel));
        
        // compute attenuation
        attenuation = compute_attenuation(surface.position);
        
        resolution                       = compute_resolution();
        screen_space_shadows_slice_index = light.screen_space_shadow_slice_index;
        transform                        = light.transform;
        atlas_offset                     = light.atlas_offsets;
        atlas_scale                      = light.atlas_scales;
        atlas_texel_size                 = light.atlas_texel_sizes;

        radiance = color * intensity * attenuation * n_dot_l;
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
