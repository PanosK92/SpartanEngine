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

static const float g_quality_max_distance = 500.0f;

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

gbuffer main_ps(gbuffer_vertex vertex)
{
    float4 albedo   = GetMaterial().color;
    float3 normal   = vertex.normal.xyz;
    float roughness = GetMaterial().roughness;
    float metalness = GetMaterial().metallness;
    float occlusion = 1.0f;
    float emission  = 0.0f;
    float2 velocity = 0.0f;

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

    Material material = GetMaterial();
    Surface surface; surface.flags = material.flags;
 
    // alpha mask
    float alpha_mask = 1.0f;
    if (surface.has_texture_alpha_mask())
    {
        alpha_mask = GET_TEXTURE(material_mask).Sample(samplers[sampler_point_wrap], vertex.uv).r;
    }

    // albedo
    if (surface.has_texture_albedo())
    {
        float4 albedo_sample = sampling::smart(surface, vertex, material_albedo);

        // read albedo's alpha channel as an alpha mask as well
        alpha_mask = min(alpha_mask, albedo_sample.a);
        
        albedo_sample.rgb  = srgb_to_linear(albedo_sample.rgb);
        albedo            *= albedo_sample;
    }

    // discard masked pixels
    if (alpha_mask <= get_alpha_threshold(vertex.position))
        discard;

    // compute pixel distance
    float3 camera_to_pixel_world = buffer_frame.camera_position - vertex.position.xyz;
    float pixel_distance         = length(camera_to_pixel_world);

    if (pixel_distance < g_quality_max_distance)
    {
        // normal mapping
        if (surface.has_texture_normal())
        {
            // get tangent space normal and apply the user defined intensity, then transform it to world space
            float3 normal_sample  = sampling::smart(surface, vertex, material_normal).xyz;
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
                roughness_sample  = sampling::smart(surface, vertex, material_roughness);
                roughness        *= roughness_sample.g;
            }
            
            float is_single_texture_roughness_metalness = surface.has_single_texture_roughness_metalness() ? 1.0f : 0.0f;
            metalness *= (1.0 - is_single_texture_roughness_metalness) + (roughness_sample.b * is_single_texture_roughness_metalness);
            
            if (surface.has_texture_metalness() && !surface.has_single_texture_roughness_metalness())
            {
                metalness *= sampling::smart(surface, vertex, material_metalness).r;
            }
        }

        // occlusion
        if (surface.has_texture_occlusion())
        {
            occlusion = sampling::smart(surface, vertex, material_occlusion).r;
        }

        // emission
        if (surface.has_texture_emissive())
        {
            float3 emissive_color  = GET_TEXTURE(material_emission).Sample(GET_SAMPLER(sampler_anisotropic_wrap), vertex.uv).rgb;
            emission               = luminance(emissive_color);
            albedo.rgb            += emissive_color;
        }

        // specular anti-aliasing
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
    }

    // write to g-buffer
    gbuffer g_buffer;
    g_buffer.albedo   = albedo;
    g_buffer.normal   = float4(normal, pass_get_material_index());
    g_buffer.material = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity = velocity;

    return g_buffer;
}
