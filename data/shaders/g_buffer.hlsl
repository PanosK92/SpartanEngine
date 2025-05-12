/*
Copyright(c) 2016-2025 Panos Karabelas

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

float hash_instance_id(uint instance_id)
{
    uint seed = instance_id * 16777619u;
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return float(seed) / 4294967295.0; // normalize
}

static float4 sample_texture(gbuffer_vertex vertex, uint texture_index, Surface surface)
{
    // sample base texture
    float4 color = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv);

    if (surface.is_terrain())
    {
        // for the terrain, sample 2 more textures, rock and sand
        float4 tex_rock = GET_TEXTURE(texture_index + 1).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv);
        float4 tex_sand = GET_TEXTURE(texture_index + 2).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv);
        
        // compute slope and blend factors
        const float sand_offset = 0.75f;
        const float rock_angle  = 45.0f * DEG_TO_RAD;
        float cos_slope         = dot(vertex.normal, float3(0, 1, 0));
        float slope             = saturate((cos(rock_angle) - cos_slope) / cos(rock_angle));
        float sand_factor       = saturate((vertex.position.y - sea_level) / sand_offset);

        // compute the color
        float4 terrain = lerp(tex_rock, color, 1.0f - slope);
        color          = lerp(terrain, tex_sand, 1.0f - sand_factor);
    }
    
    // vegetation typically uses color variance
    {
        const float variation_strength = 0.2f;                       // kind of visible variation
        const float3 greener           = float3(0.05f, 0.4f, 0.03f); // richer green
        const float3 yellower          = float3(0.45f, 0.4f, 0.15f); // bolder yellow
        const float3 browner           = float3(0.3f, 0.15f, 0.08f); // deeper brown
        
        // blend based on variation value using lerps
        float variation        = hash_instance_id(vertex.instance_id);
        float3 variation_color = greener;                                                 // start with greener
        variation_color        = lerp(variation_color, yellower, step(0.25f, variation)); // transition to yellower at 0.25
        variation_color        = lerp(variation_color, browner, step(0.5f, variation));   // transition to browner at 0.5

        // apply
        color.rgb = lerp(color.rgb, variation_color, variation_strength * (float)surface.color_variation_from_instance());
    }

    return color;
}

gbuffer_vertex main_vs(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    gbuffer_vertex vertex = transform_to_world_space(input, instance_id, buffer_pass.transform);

    // transform world space position to screen space
    Surface surface;
    surface.flags = GetMaterial().flags;
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

    // compute animated uv for water
    {
        const float2 direction  = float2(1.0, 0.5);
        const float speed       = 0.2;
        float2 uv_offset        = direction * speed * (float)buffer_frame.time * surface.is_water();
        vertex.uv              += uv_offset; 
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

        // height based color
        {
            // local space height, for the grass blades
            const float3 grass_base  = float3(0.0f, 0.05f, 0.005f);  // darker base - emulate occlusion
            const float3 grass_tip   = float3(0.02f, 0.15f, 0.015f); // darker tip
            const float3 grass_tint  = lerp(grass_base, grass_tip, smoothstep(0, 1, vertex.height_percent));
            albedo.rgb               = lerp(albedo.rgb, grass_tint, (float)surface.is_grass_blade());

            // world space height, for everything
            float snow_blend_factor = get_snow_blend_factor(vertex.position, vertex.normal);
            albedo.rgb              = lerp(albedo.rgb, float3(0.95f, 0.95f, 0.95f), snow_blend_factor);
        }
        
        // alpha testing happens in the depth pre-pass, so here any opaque pixel has an alpha of 1
        albedo.a = lerp(albedo.a, 1.0f, step(albedo_sample.a, 1.0f) * pass_is_opaque());
    }

    // emission
    if (surface.has_texture_emissive())
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
    
        float normal_intensity     = saturate(max(0.012f, GetMaterial().normal));
        tangent_normal.xy         *= normal_intensity;
        float3x3 tangent_to_world  = make_tangent_to_world_matrix(vertex.normal, vertex.tangent);
        normal                     = normalize(mul(tangent_normal, tangent_to_world).xyz);
    }

    // occlusion, roughness, metalness, height sample
    {
        float4 packed_sample  = sample_texture(vertex, material_texture_index_packed, surface);
        occlusion             = packed_sample.r;
        roughness            *= packed_sample.g;
        metalness            *= packed_sample.b;
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
