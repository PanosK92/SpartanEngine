﻿/*
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

//= INCLUDES =========
#include "common.hlsl"
//====================

struct gbuffer
{
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
    float4 material : SV_Target2;
    float2 velocity : SV_Target3;
};

Texture2D<float4> displacement_map : register(t17);
Texture2D<float4> slope_map : register(t18);

// rotate UV around center (0.5, 0.5) by angle
float2 rotate_uv(float2 uv, float angle)
{
    float cos_a = cos(angle);
    float sin_a = sin(angle);
    float2 centered = uv - 0.5f;
    float2 rotated = float2(
        centered.x * cos_a - centered.y * sin_a,
        centered.x * sin_a + centered.y * cos_a
    );
    return rotated + 0.5f;
}

static float4 sample_reduce_tiling(uint texture_index, float2 uv, float3 world_pos)
{
    // get integer tile coordinates to hash from
    int2 tile_coords = int2(floor(world_pos.x), floor(world_pos.z));

    // get random rotation angle per tile in multiples of 90 degrees (0, 90, 180, 270)
    float rnd   = hash(tile_coords);
    float angle = floor(rnd * 4.0f) * PI_HALF;

    // rotate uv inside tile
    float2 tile_uv    = frac(uv);
    float2 rotated_uv = rotate_uv(tile_uv, angle);

    // recombine with integer tile coords to preserve tiling but rotated
    float2 final_uv = float2(tile_coords) + rotated_uv;

    // wrap final uv again to [0,1] so sample doesn't go out of bounds
    final_uv = frac(final_uv);

    // sample texture
    return GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), final_uv);
}

static float4 sample_texture(gbuffer_vertex vertex, uint texture_index, Surface surface)
{
    float4 color;

    if (surface.is_terrain())
    {
        // sample base color without tiling
        color           = sample_reduce_tiling(texture_index, vertex.uv, vertex.position);
        float4 tex_rock = sample_reduce_tiling(texture_index + 1, vertex.uv, vertex.position);
        float4 tex_sand = sample_reduce_tiling(texture_index + 2, vertex.uv, vertex.position);

        const float sand_offset    = 0.75f;
        const float rock_angle     = 50.0f * DEG_TO_RAD; // start blending here
        const float rock_sharpness = 0.2f;               // radians — smaller = sharper

        float surface_angle = acos(dot(vertex.normal, float3(0, 1, 0)));
        float slope         = saturate((surface_angle - rock_angle) / rock_sharpness);
        float sand_factor   = saturate((vertex.position.y - sea_level) / sand_offset);

        float4 terrain = lerp(tex_rock, color, 1.0f - slope);
        color          = lerp(terrain, tex_sand, 1.0f - sand_factor);
    }
    else
    {
        // sample base color with tiling for non-terrain
        color = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv);
    }

    return color;
}

static const float2 hexRatio = float2(1.0f, sqrt(3.0f));

float4 GetHexGridInfo(float2 uv)
{
    float4 hexIndex = round(float4(uv, uv - float2(0.5f, 1.0f)) / hexRatio.xyxy);
    float4 hexCenter = float4(hexIndex.xy * hexRatio, (hexIndex.zw + 0.5f) * hexRatio);
    float4 offset = uv.xyxy - hexCenter;
    return dot(offset.xy, offset.xy) < dot(offset.zw, offset.zw) ?
    float4(hexCenter.xy, hexIndex.xy) :
    float4(hexCenter.zw, hexIndex.zw);
}

float GetHexSDF(in float2 p)
{
    p = abs(p);
    return 0.5f - max(dot(p, hexRatio * 0.5f), p.x);
}

//xy: node pos, z: weight
float3 GetTriangleInterpNode(in float2 pos, in float freq, in int nodeIndex)
{
    float2 nodeOffsets[3] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 1.0f),
        float2(1.0f, -1.0f)
    };

    float2 uv = pos * freq + nodeOffsets[nodeIndex] / hexRatio.xy * 0.5f;
    float4 hexInfo = GetHexGridInfo(uv);
    float dist = GetHexSDF(uv - hexInfo.xy) * 2.0f;
    return float3(hexInfo.xy / freq, dist);
}

float3 hash33(float3 p)
{
    p = float3(dot(p, float3(127.1f, 311.7f, 74.7f)),
			  dot(p, float3(269.5f, 183.3f, 246.1f)),
			  dot(p, float3(113.5f, 271.9f, 124.6f)));

    return frac(sin(p) * 43758.5453123f);
}

float4 GetTextureSample(Texture2D texture, float2 pos, float freq, float2 nodePoint)
{
    const float3 hash = hash33(float3(nodePoint.xy, 0.0f));
    const float ang = hash.x * 2.0f * 3.14159265f;
    
    const float2x2 rotation = float2x2(
        cos(ang), sin(ang),
       -sin(ang), cos(ang)
    );

    const float2 uv = mul(rotation, pos * freq) + hash.yz;
    
    return texture.SampleLevel(samplers[sampler_point_clamp], uv, 0);
}

gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    MaterialParameters material = GetMaterial();
    
    //input.position.xyz += displacement_map.SampleLevel(samplers[sampler_point_clamp], input.uv, 0).rgb * material.ocean_parameters.displacementScale;
    float3 displacement;
    for (int i = 0; i < 3; i++)
    {
        float3 interpNode = GetTriangleInterpNode(input.uv, 20.0f, i);
        displacement += GetTextureSample(displacement_map, input.uv, 10.0f, interpNode.xy) * interpNode.z;
    }

    input.position.xyz += displacement;
    
    float4 slope = slope_map.SampleLevel(samplers[sampler_point_clamp], input.uv, 0) * material.ocean_parameters.slopeScale;
    input.normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    
    gbuffer_vertex vertex = transform_to_world_space(input, instance_id, buffer_pass.transform);

    // transform world space position to clip space
    Surface surface;
    surface.flags = material.flags;
    if (!surface.is_tessellated())
    {
        vertex = transform_to_clip_space(vertex);
    }

    return vertex;
}

[earlydepthstencil]
gbuffer main_ps(gbuffer_vertex vertex)
{
    // setup
    MaterialParameters material = GetMaterial();
    float4 albedo               = material.color;
    float3 normal               = vertex.normal.xyz;
    float roughness             = material.roughness;
    float metalness             = material.metallness;
    float emission              = 0.0f;
    float2 velocity             = 0.0f;
    float occlusion             = 1.0f;
    Surface surface;
    surface.flags = material.flags;

    // velocity
    {
        // convert to ndc
        float2 position_ndc_current  = (vertex.position_clip_current.xy / vertex.position_clip_current.w);
        float2 position_ndc_previous = (vertex.position_clip_previous.xy / vertex.position_clip_previous.w);

        // remove the ndc jitter
        position_ndc_current  -= buffer_frame.taa_jitter_current;
        position_ndc_previous -= buffer_frame.taa_jitter_previous;

        // compute the velocity
        velocity = position_ndc_current - position_ndc_previous;
    }

    // albedo
    {
        float4 albedo_sample = 1.0f;
        if (surface.has_texture_albedo())
        {
            albedo_sample      = sample_texture(vertex, material_texture_index_albedo, surface);
            albedo_sample.rgb  = srgb_to_linear(albedo_sample.rgb);
            albedo            *= albedo_sample;
        }

        // dynamic vegetation coloring (grass blades, trees, etc.)
        {
            // color vaiation based on instance id
            static const float3 vegetation_greener    = float3(0.05f, 0.4f, 0.03f);
            static const float3 vegetation_yellower   = float3(0.45f, 0.4f, 0.15f);
            static const float3 vegetation_browner    = float3(0.3f,  0.15f, 0.08f);
            const float vegetation_variation_strength = 0.15f;
            float variation                           = hash(vertex.instance_id);

            // --- grass-specific tint based on local blade height and instance variation ---
            if (surface.is_grass_blade())
            {
                const float3 grass_base = float3(0.0f, 0.05f, 0.005f);
                const float3 grass_tip  = float3(0.02f, 0.15f, 0.015f);
                float t = smoothstep(0, 1, vertex.height_percent);
                float3 grass_tint = lerp(grass_base, grass_tip, t);
        
                // blend between greener, yellower, browner based on variation
                float3 variation_color = vegetation_greener;
                variation_color        = lerp(variation_color, vegetation_yellower, step(0.33f, variation));
                variation_color        = lerp(variation_color, vegetation_browner, step(0.66f, variation));
        
                // blend base tint with variation color, weighted by global variation strength
                grass_tint = lerp(grass_tint, variation_color, vegetation_variation_strength);
        
                albedo.rgb = lerp(albedo.rgb, grass_tint, 1.0f);
            }
            else // trees and other vegetation variation
            {
                float3 variation_color = vegetation_greener;
                variation_color        = lerp(variation_color, vegetation_yellower, step(0.25f, variation));
                variation_color        = lerp(variation_color, vegetation_browner, step(0.5f, variation));
        
                albedo.rgb = lerp(albedo.rgb, variation_color, vegetation_variation_strength * (float)surface.color_variation_from_instance());
            }
        
            // --- snow blending based on world-space height and normal ---
            float snow_blend_factor = get_snow_blend_factor(vertex.position, vertex.normal);
            albedo.rgb = lerp(albedo.rgb, float3(0.95f, 0.95f, 0.95f), snow_blend_factor);
        }
        
        // alpha testing happens in the depth pre-pass, so here any opaque pixel has an alpha of 1
        albedo.a = lerp(albedo.a, 1.0f, step(albedo_sample.a, 1.0f) * pass_is_opaque());
    }

    // emission
    if (material.emissive_from_albedo())
    {
        emission += luminance(albedo.rgb) * (float)material.emissive_from_albedo();
    }
    else if (surface.has_texture_emissive())
    {
        float3 emissive_color  = GET_TEXTURE(material_texture_index_emission).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv).rgb;
        albedo.rgb            += emissive_color;            // overwrite the albedo color
        emission               = luminance(emissive_color); // use the luminance later to boost it (no need to carry a float3 around)
    }
    
    // normal mapping
    if (surface.has_texture_normal())
    {
        // get tangent space normal and apply the user defined intensity, then transform it to world space
        float3 normal_sample  = sample_texture(vertex, material_texture_index_normal, surface).xyz;
        float3 tangent_normal = normalize(unpack(normal_sample));
    
        // reconstruct z-component as this can be a bc5 two channel normal map
        tangent_normal.z = fast_sqrt(max(0.0, 1.0 - tangent_normal.x * tangent_normal.x - tangent_normal.y * tangent_normal.y));
    
        // rotate normals for water using Perlin noise, modulated by surface.is_water()
        {
            float2 direction = float2(1.0, 0.5);
            float speed      = 0.2;
            float time       = (float)buffer_frame.time;
            float2 uv_offset = direction * speed * time;
            float2 noise_uv  = (vertex.uv + uv_offset) * 5.0f; // scale UVs for wave size
            float noise      = noise_perlin(noise_uv + float2(time, time * 0.5)); // animate with time
            float is_water   = (float) surface.is_water();
            float angle      = noise * PI2 * is_water; // map noise [0,1] to angle [0, 2π] for water only
    
            // rotate tangent normal.xy around Z-axis (tangent space)
            float cos_a = cos(angle);
            float sin_a = sin(angle);
            float2 rotated_xy = float2(
                tangent_normal.x * cos_a - tangent_normal.y * sin_a,
                tangent_normal.x * sin_a + tangent_normal.y * cos_a
            );
    
            // blend between original and rotated normals based on is_water (0 = original, 1 = rotated)
            tangent_normal.xy = lerp(tangent_normal.xy, rotated_xy, is_water);
            tangent_normal.z  = fast_sqrt(max(0.0, 1.0 - tangent_normal.x * tangent_normal.x - tangent_normal.y * tangent_normal.y));
    
            // flip if normal points down
            tangent_normal *= lerp(1.0, sign(tangent_normal.z), is_water);
        }
    
        float normal_intensity     = saturate(max(0.012f, GetMaterial().normal));
        tangent_normal.xy         *= normal_intensity;
        float3x3 tangent_to_world  = make_tangent_to_world_matrix(vertex.normal, vertex.tangent);
        normal                     = normalize(mul(tangent_normal, tangent_to_world).xyz);
    }
    
    // occlusion, roughness, metalness, height sample
    {
        float4 packed_sample  = sample_texture(vertex, material_texture_index_packed, surface);
        occlusion             = lerp(occlusion, packed_sample.r, material.has_texture_occlusion() ? 1.0f : 0.0f);
        roughness            *= lerp(1.0f,      packed_sample.g, material.has_texture_roughness() ? 1.0f : 0.0f);
        metalness            *= lerp(1.0f,      packed_sample.b, material.has_texture_metalness() ? 1.0f : 0.0f);
    }
    
    // specular anti-aliasing - also increases cache hits for certain subsqeuent passes
    {
        static const float strength           = 1.0f;
        static const float max_roughness_gain = 0.02f;

        float roughness2         = roughness * roughness;
        float3 dndu              = ddx(normal), dndv = ddy(normal);
        float variance           = (dot(dndu, dndu) + dot(dndv, dndv));
        float kernelRoughness2   = min(variance * strength, max_roughness_gain);
        float filteredRoughness2 = saturate(roughness2 + kernelRoughness2);
        roughness                = fast_sqrt(filteredRoughness2);
    }

    // write to g-buffer
    gbuffer g_buffer;
    g_buffer.albedo   = albedo;
    g_buffer.normal   = float4(normal, pass_get_material_index());
    g_buffer.material = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity = velocity;

    return g_buffer;
}
