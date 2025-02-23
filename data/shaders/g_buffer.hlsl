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

static float4 sample_texture(float3 position, float3 normal, float2 uv, uint texture_index, bool is_water, bool is_terrain)
{
    // parameters
    const float sand_offset  = 0.75f;
    const float2 direction_1 = float2(1.0, 0.5);
    const float2 direction_2 = float2(-0.5, 1.0);
    const float speed_1      = 0.2;
    const float speed_2      = 0.15;

    // things which are shared among branches
    float snow_blend_factor = get_snow_blend_factor(position);
    float4 base_color       = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv);

    float4 color = 0.0f;
    if (is_water)
    {
        // sample with different UVs
        float2 uv_interleaved_1 = uv + (float)buffer_frame.time * speed_1 * direction_1;
        float2 uv_interleaved_2 = uv + (float)buffer_frame.time * speed_2 * direction_2;
        float3 sample_1         = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_interleaved_1).rgb;
        float3 sample_2         = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_interleaved_2).rgb;
        color                   = float4(normalize(sample_1 + sample_2), 0.0f);
    }
    else if (is_terrain)
    {
        float4 tex_rock = GET_TEXTURE(texture_index + 1).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv);
        float4 tex_sand = GET_TEXTURE(texture_index + 2).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv);

        // compute slope and blend factors
        float slope                = saturate(pow(saturate(dot(normal, float3(0.0f, 1.0f, 0.0f)) - -0.25f), 24.0f));
        float sand_blend_factor    = saturate(position.y / sand_offset);
        float sand_blend_threshold = sea_level + sand_offset;
        float sand_factor          = saturate((position.y - sea_level) / (sand_blend_threshold - sea_level));
        sand_blend_factor          = 1.0f - sand_factor;

        // compute the color
        float4 terrain    = lerp(tex_rock, base_color, slope);
        color             = lerp(terrain, tex_sand, sand_blend_factor);
        float4 snow_color = float4(0.95f, 0.95f, 0.95f, 1.0f);
        color             = lerp(color, snow_color, snow_blend_factor);
        
        // apply perlin noise color variation
        const float noise_scale         = 2.0f;
        const float variation_strength  = 0.25f;
        float2 noise_uv                 = position.xz * noise_scale;
        float noise_value               = get_noise_perlin(noise_uv);
        color.rgb                      *= (1.0f + noise_value * variation_strength);
    }
    else // default
    {
        color             = base_color;
        float4 snow_color = float4(0.95f, 0.95f, 0.95f, 1.0f);
        color             = lerp(color, snow_color, snow_blend_factor);
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
        velocity = ndc_to_uv(position_ndc_current) - ndc_to_uv(position_ndc_previous);
    }

    // albedo
    {
        float4 albedo_sample = 1.0f;
        if (surface.has_texture_albedo())
        {
            albedo_sample      = sample_texture(vertex.position, vertex.normal, vertex.uv, material_texture_index_albedo, surface.is_water(), surface.texture_slope_based());
            albedo_sample.rgb  = srgb_to_linear(albedo_sample.rgb);
            albedo            *= albedo_sample;
        }
        albedo.rgb *= vertex.color;
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
        float3 normal_sample  = sample_texture(vertex.position, vertex.normal, vertex.uv, material_texture_index_normal, surface.is_water(), surface.texture_slope_based()).xyz;
        float3 tangent_normal = normalize(unpack(normal_sample));
    
        // reconstruct z-component as this can be a BC5 two channel normal map
        tangent_normal.z = fast_sqrt(max(0.0, 1.0 - tangent_normal.x * tangent_normal.x - tangent_normal.y * tangent_normal.y));
    
        float normal_intensity     = saturate(max(0.012f, GetMaterial().normal));
        tangent_normal.xy         *= normal_intensity;
        float3x3 tangent_to_world  = make_tangent_to_world_matrix(vertex.normal, vertex.tangent);
        normal                     = normalize(mul(tangent_normal, tangent_to_world).xyz);
    }

    // occlusion, roughness, metalness, height sample
    {
        float4 packed_sample  = sample_texture(vertex.position, vertex.normal, vertex.uv, material_texture_index_packed, surface.is_water(), surface.texture_slope_based());
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

