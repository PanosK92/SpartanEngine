/*
Copyright(c) 2016-2021 Panos Karabelas
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

//= INCLUDES ===================
#include "common.hlsl"
#include "parallax_mapping.hlsl"
//==============================

struct PixelInputType
{
    float4 position             : SV_POSITION;
    float2 uv                   : TEXCOORD;
    float3 normal               : NORMAL;
    float3 tangent              : TANGENT;
    float4 position_ss_current  : SCREEN_POS;
    float4 position_ss_previous : SCREEN_POS_PREVIOUS;
};

struct PixelOutputType
{
    float4 albedo                : SV_Target0;
    float4 normal                : SV_Target1;
    float4 material              : SV_Target2;
    float2 velocity              : SV_Target3;
    float fsr2_transparency_mask : SV_Target4;
};

PixelInputType mainVS(Vertex_PosUvNorTan input)
{
    PixelInputType output;

    // position computation has to be an exact match to depth_prepass.hlsl
    input.position.w = 1.0f;
    output.position  = mul(input.position, buffer_uber.transform);
    output.position  = mul(output.position, buffer_frame.view_projection);
    
    output.position_ss_current  = output.position;
    output.position_ss_previous = mul(input.position, buffer_uber.transform_previous);
    output.position_ss_previous = mul(output.position_ss_previous, buffer_frame.view_projection_previous);
    output.normal               = normalize(mul(input.normal,  (float3x3)buffer_uber.transform)).xyz;
    output.tangent              = normalize(mul(input.tangent, (float3x3)buffer_uber.transform)).xyz;
    output.uv                   = input.uv;
    
    return output;
}

PixelOutputType mainPS(PixelInputType input)
{
    // UV
    float2 uv = input.uv;
    uv        = float2(uv.x * buffer_uber.mat_tiling.x + buffer_uber.mat_offset.x, uv.y * buffer_uber.mat_tiling.y + buffer_uber.mat_offset.y);

    // Velocity
    float2 position_uv_current  = ndc_to_uv((input.position_ss_current.xy / input.position_ss_current.w) - buffer_frame.taa_jitter_current);
    float2 position_uv_previous = ndc_to_uv((input.position_ss_previous.xy / input.position_ss_previous.w) - buffer_frame.taa_jitter_previous);
    float2 velocity_uv          = position_uv_current - position_uv_previous;

    // TBN
    float3x3 TBN = 0.0f;
    if (has_texture_height() || has_texture_normal())
    {
        TBN = makeTBN(input.normal, input.tangent);
    }

    // Parallax mapping
    if (has_texture_height())
    {
        float height_scale     = buffer_uber.mat_height * 0.04f;
        float3 camera_to_pixel = normalize(buffer_frame.camera_position - input.position.xyz);
        uv                     = ParallaxMapping(tex_material_height, sampler_anisotropic_wrap, uv, camera_to_pixel, TBN, height_scale);
    }

    // Alpha mask
    float alpha_mask = 1.0f;
    if (has_texture_alpha_mask())
    {
        alpha_mask = tex_material_mask.Sample(sampler_anisotropic_wrap, uv).r;
    }

    // Albedo
    float4 albedo = buffer_uber.mat_color;
    if (has_texture_albedo())
    {
        float4 albedo_sample = tex_material_albedo.Sample(sampler_anisotropic_wrap, uv);

        // Read albedo's alpha channel as an alpha mask as well.
        alpha_mask      = min(alpha_mask, albedo_sample.a);
        albedo_sample.a = 1.0f;

        albedo_sample.rgb = degamma(albedo_sample.rgb);
        albedo            *= albedo_sample;
    }

    // Discard masked pixels
    if (alpha_mask <= ALPHA_THRESHOLD)
        discard;

    // Roughness + Metalness
    float roughness = buffer_uber.mat_roughness;
    float metalness = buffer_uber.mat_metallness;
    {
        if (!has_single_texture_roughness_metalness())
        {
            if (has_texture_roughness())
            {
                roughness *= tex_material_roughness.Sample(sampler_anisotropic_wrap, uv).r;
            }

            if (has_texture_metalness())
            {
                metalness *= tex_material_metallness.Sample(sampler_anisotropic_wrap, uv).r;
            }
        }
        else
        {
            if (has_texture_roughness())
            {
                roughness *= tex_material_roughness.Sample(sampler_anisotropic_wrap, uv).g;
            }

            if (has_texture_metalness())
            {
                metalness *= tex_material_metallness.Sample(sampler_anisotropic_wrap, uv).b;
            }
        }
    }
    
    // Normal
    float3 normal = input.normal.xyz;
    if (has_texture_normal())
    {
        // Get tangent space normal and apply the user defined intensity. Then transform it to world space.
        float3 tangent_normal  = normalize(unpack(tex_material_normal.Sample(sampler_anisotropic_wrap, uv).rgb));
        float normal_intensity = clamp(buffer_uber.mat_normal, 0.012f, buffer_uber.mat_normal);
        tangent_normal.xy      *= saturate(normal_intensity);
        normal                 = normalize(mul(tangent_normal, TBN).xyz);
    }

    // Occlusion
    float occlusion = 1.0f;
    if (has_texture_occlusion())
    {
        occlusion = tex_material_occlusion.Sample(sampler_anisotropic_wrap, uv).r;
    }

    // Emission
    float emission = 0.0f;
    if (has_texture_emissive())
    {
        float3 emissive_color = tex_material_emission.Sample(sampler_anisotropic_wrap, uv).rgb;
        emission              = luminance(emissive_color);
        albedo.rgb            += emissive_color;

    }

    // Specular anti-aliasing
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

    // Write to G-Buffer
    PixelOutputType g_buffer;
    g_buffer.albedo                 = albedo;
    g_buffer.normal                 = float4(normal, pack_uint32_to_float16(buffer_uber.mat_id));
    g_buffer.material               = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity               = velocity_uv;
    g_buffer.fsr2_transparency_mask = albedo.a * buffer_uber.is_transparent_pass;

    return g_buffer;
}
