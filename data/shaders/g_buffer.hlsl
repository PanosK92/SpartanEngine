/*
Copyright(c) 2016-2024 Panos Karabelas

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
    float4 albedo     = GetMaterial().color;
    float3 normal     = vertex.normal.xyz;
    float roughness   = GetMaterial().roughness;
    float metalness   = GetMaterial().metallness;
    float occlusion   = 1.0f;
    float emission    = 0.0f;
    float2 velocity   = 0.0f;
    Material material = GetMaterial();
    Surface surface; surface.flags = material.flags;
    
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
    float4 albedo_sample = 1.0f;
    if (surface.has_texture_albedo())
    {
        albedo_sample      = sampling::smart(vertex.position, vertex.normal, vertex.uv, material_texture_index_albedo, surface.is_water(), surface.texture_slope_based(), surface.vertex_animate_wind());
        albedo_sample.rgb  = srgb_to_linear(albedo_sample.rgb);
        albedo            *= albedo_sample;
    }

    // alpah testing happens in the depth pre-pass, so here any opaque pixel has an alpha of 1
    albedo.a = lerp(albedo.a, 1.0f, step(albedo_sample.a, 1.0f) * pass_is_opaque());

    // normal mapping
    if (surface.has_texture_normal())
    {
        // get tangent space normal and apply the user defined intensity, then transform it to world space
        float3 normal_sample  = sampling::smart(vertex.position, vertex.normal, vertex.uv, material_texture_index_normal, surface.is_water(), surface.texture_slope_based(), surface.vertex_animate_wind()).xyz;
        float3 tangent_normal = normalize(unpack(normal_sample));
    
        // reconstruct z-component as this can be a BC5 two channel normal map
        tangent_normal.z = sqrt(max(0.0, 1.0 - tangent_normal.x * tangent_normal.x - tangent_normal.y * tangent_normal.y));
    
        float normal_intensity     = max(0.012f, GetMaterial().normal);
        tangent_normal.xy         *= saturate(normal_intensity);
        float3x3 tangent_to_world  = make_tangent_to_world_matrix(vertex.normal, vertex.tangent);
        normal                     = normalize(mul(tangent_normal, tangent_to_world).xyz);
    }
    
    // roughness + metalness
    {
        float4 roughness_sample = 1.0f;
        if (surface.has_texture_roughness())
        {
            roughness_sample  = sampling::smart(vertex.position, vertex.normal, vertex.uv, material_texture_index_roughness, surface.is_water(), surface.texture_slope_based(), surface.vertex_animate_wind());
            roughness        *= roughness_sample.g;
        }
        
        float is_single_texture_roughness_metalness = surface.has_single_texture_roughness_metalness() ? 1.0f : 0.0f;
        metalness *= (1.0 - is_single_texture_roughness_metalness) + (roughness_sample.b * is_single_texture_roughness_metalness);
        
        if (surface.has_texture_metalness() && !surface.has_single_texture_roughness_metalness())
        {
            metalness *= sampling::smart(vertex.position, vertex.normal, vertex.uv, material_texture_index_metalness, surface.is_water(), surface.texture_slope_based(), surface.vertex_animate_wind()).r;
        }
    }
    
    // occlusion
    if (surface.has_texture_occlusion())
    {
        occlusion = sampling::smart(vertex.position, vertex.normal, vertex.uv, material_texture_index_occlusion, surface.is_water(), surface.texture_slope_based(), surface.vertex_animate_wind()).r;
    }
    
    // emission
    if (surface.has_texture_emissive())
    {
        float3 emissive_color  = GET_TEXTURE(material_texture_index_emission).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv).rgb;
        emission               = luminance(emissive_color);
        albedo.rgb            += emissive_color;
    }

    // write to g-buffer
    gbuffer g_buffer;
    g_buffer.albedo   = albedo;
    g_buffer.normal   = float4(normal, pass_get_material_index());
    g_buffer.material = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity = velocity;

    return g_buffer;
}
