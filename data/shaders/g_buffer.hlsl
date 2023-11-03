/*
Copyright(c) 2016-2023 Panos Karabelas
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

static const float g_quality_distance_low = 180.0f;

struct PixelInputType
{
    float4 position             : SV_POSITION;
    float2 uv                   : TEXCOORD;
    float3 normal_world         : WORLD_NORMAL;
    float3 tangent_world        : WORLD_TANGENT;
    float3 position_world       : WORLD_POS;
    float4 position_ss_current  : SCREEN_POS;
    float4 position_ss_previous : SCREEN_POS_PREVIOUS;
};

struct PixelOutputType
{
    float4 albedo                : SV_Target0;
    float4 normal                : SV_Target1;
    float4 material              : SV_Target2;
    float4 material_2            : SV_Target3;
    float2 velocity              : SV_Target4;
    float fsr2_transparency_mask : SV_Target5;
};

PixelInputType mainVS(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    PixelInputType output;

    // position
    output.position             = compute_screen_space_position(input, instance_id, buffer_pass.transform, buffer_frame.view_projection);
    output.position_ss_current  = output.position;
    output.position_ss_previous = compute_screen_space_position(input, instance_id, pass_get_transform_previous(), buffer_frame.view_projection_previous, output.position_world);
    // normals
    output.normal_world  = normalize(mul(input.normal,  (float3x3)buffer_pass.transform)).xyz;
    output.tangent_world = normalize(mul(input.tangent, (float3x3)buffer_pass.transform)).xyz;
    // uv
    output.uv = input.uv;
    
    return output;
}

float compute_slope(float3 normal)
{
    float bias  = -0.1f; // increase the bias to favour the slope/rock texture
    float slope = saturate(dot(normal, float3(0.0f, 1.0f, 0.0f)) - bias);
    slope       = pow(slope, 16.0f); // increase the exponent to sharpen the transition

    return slope;
}

float4 sample_albedo(float2 uv, float slope)
{
    float4 albedo = tex_material_albedo.Sample(samplers[sampler_anisotropic_wrap], uv);
    
    if (material_texture_slope_based())
    {
        float4 tex_flat  = albedo;
        float4 tex_slope = tex_material_albedo_2.Sample(samplers[sampler_anisotropic_wrap], uv * 0.3f);
        albedo           = lerp(tex_slope, tex_flat, slope);
    }

    return albedo;
}

float3 smaple_normal(float2 uv, float slope)
{
    float3 normal = tex_material_normal.Sample(samplers[sampler_anisotropic_wrap], uv).xyz;

    if (material_texture_slope_based())
    {
        float3 tex_flat  = normal;
        float3 tex_slope = tex_material_normal2.Sample(samplers[sampler_anisotropic_wrap], uv * 0.3f).rgb;
        normal           = lerp(tex_slope, tex_flat, slope);
    }

    return normal;
}

PixelOutputType mainPS(PixelInputType input)
{
    // initial g-buffer values
    float4 albedo   = buffer_material.color;
    float3 normal   = input.normal_world.xyz;
    float roughness = buffer_material.roughness;
    float metalness = buffer_material.metallness;
    float occlusion = 1.0f;
    float emission  = 0.0f;
    float2 velocity = 0.0f;
    
    // velocity ndc
    {
        // compute previous and current positions
        float2 position_ndc_current  = (input.position_ss_current.xy / input.position_ss_current.w);
        float2 position_ndc_previous = (input.position_ss_previous.xy / input.position_ss_previous.w);

        // remove the ndc jitter
        position_ndc_current  -= buffer_frame.taa_jitter_current;
        position_ndc_previous -= buffer_frame.taa_jitter_previous;

        // compute the velocity
        velocity = position_ndc_current - position_ndc_previous;
    }

    // uv
    float2 uv  = input.uv;
    uv         = float2(uv.x * buffer_material.tiling.x + buffer_material.offset.x, uv.y * buffer_material.tiling.y + buffer_material.offset.y);
    uv        += float(buffer_frame.time * 0.1f) * material_texture_animate();
    
    // alpha mask
    float alpha_mask = 1.0f;
    if (has_texture_alpha_mask())
    {
        alpha_mask = tex_material_mask.Sample(samplers[sampler_anisotropic_wrap], uv).r;
    }
    
    // albedo
    float slope = compute_slope(normal);
    if (has_texture_albedo())
    {
        float4 albedo_sample = sample_albedo(uv, slope);

        // read albedo's alpha channel as an alpha mask as well
        alpha_mask      = min(alpha_mask, albedo_sample.a);
        albedo_sample.a = 1.0f;
        
        albedo_sample.rgb  = degamma(albedo_sample.rgb);
        albedo            *= albedo_sample;
    }

    // discard masked pixels
    if (alpha_mask <= ALPHA_THRESHOLD)
        discard;

    // determine shading quality
    float3 camera_to_pixel_world = buffer_frame.camera_position - input.position_world.xyz;
    float pixel_distance         = length(camera_to_pixel_world);
    bool high_quality            = pixel_distance < g_quality_distance_low;
    
    if (high_quality)
    {
        // parallax mapping
        if (has_texture_height())
        {
            float scale = buffer_material.height * 0.01f;

            float3x3 world_to_tangent       = make_world_to_tangent_matrix(input.normal_world, input.tangent_world);
            float3 camera_to_pixel_tangent  = normalize(mul(normalize(camera_to_pixel_world), world_to_tangent));
            float height                    = tex_material_height.Sample(samplers[sampler_anisotropic_wrap], uv).r - 0.5f;
            uv                             += (camera_to_pixel_tangent.xy / camera_to_pixel_tangent.z) * height * scale;
        }
        
        // normal mapping
        if (has_texture_normal())
        {
            // get tangent space normal and apply the user defined intensity, then transform it to world space
            float3 tangent_normal      = normalize(unpack(smaple_normal(uv, slope)));
            float normal_intensity     = clamp(buffer_material.normal, 0.012f, buffer_material.normal);
            tangent_normal.xy         *= saturate(normal_intensity);
            float3x3 tangent_to_world  = make_tangent_to_world_matrix(input.normal_world, input.tangent_world);
            normal                     = normalize(mul(tangent_normal, tangent_to_world).xyz);
        }
        
        // roughness + metalness
        {
            if (!has_single_texture_roughness_metalness())
            {
                if (has_texture_roughness())
                {
                    roughness *= tex_material_roughness.Sample(samplers[sampler_anisotropic_wrap], uv).r;
                }

                if (has_texture_metalness())
                {
                    metalness *= tex_material_metallness.Sample(samplers[sampler_anisotropic_wrap], uv).r;
                }
            }
            else
            {
                if (has_texture_roughness())
                {
                    roughness *= tex_material_roughness.Sample(samplers[sampler_anisotropic_wrap], uv).g;
                }

                if (has_texture_metalness())
                {
                    metalness *= tex_material_metallness.Sample(samplers[sampler_anisotropic_wrap], uv).b;
                }
            }
        }
        
        // occlusion
        if (has_texture_occlusion())
        {
            occlusion = tex_material_occlusion.Sample(samplers[sampler_anisotropic_wrap], uv).r;
        }

        // emission
        if (has_texture_emissive())
        {
            float3 emissive_color  = tex_material_emission.Sample(samplers[sampler_anisotropic_wrap], uv).rgb;
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
    PixelOutputType g_buffer;
    g_buffer.albedo                 = albedo;
    g_buffer.normal                 = float4(normal, buffer_material.sheen);
    g_buffer.material               = float4(roughness, metalness, emission, occlusion);
    g_buffer.material_2             = float4(buffer_material.anisotropic, buffer_material.anisotropic_rotation, buffer_material.clearcoat, buffer_material.clearcoat_roughness);
    g_buffer.velocity               = velocity;
    g_buffer.fsr2_transparency_mask = (1.0f - albedo.a) * (pass_is_transparent() ? 1.0f : 0.0f);

    return g_buffer;
}
