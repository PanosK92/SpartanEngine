/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==================
#include "Common.hlsl"
#include "ParallaxMapping.hlsl"
//=============================

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
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
    float4 material : SV_Target2;
    float2 velocity : SV_Target3;
};

PixelInputType mainVS(Vertex_PosUvNorTan input)
{
    PixelInputType output;
    
    input.position.w            = 1.0f;     
    output.position_ss_previous = mul(input.position, g_object_wvp_previous);
    output.position             = mul(input.position, g_object_transform);
    output.position             = mul(output.position, g_view_projection);
    output.position_ss_current  = output.position;
    output.normal               = normalize(mul(input.normal, (float3x3)g_object_transform)).xyz;
    output.tangent              = normalize(mul(input.tangent, (float3x3)g_object_transform)).xyz;
    output.uv                   = input.uv;
    
    return output;
}

PixelOutputType mainPS(PixelInputType input)
{
    PixelOutputType g_buffer;

    float2 texCoords    = float2(input.uv.x * g_mat_tiling.x + g_mat_offset.x, input.uv.y * g_mat_tiling.y + g_mat_offset.y);
    float4 albedo       = g_mat_color;
    float roughness     = g_mat_roughness;
    float metallic      = g_mat_metallic;
    float3 normal       = input.normal.xyz;
    float emission      = 0.0f;
    float occlusion     = 1.0f;
    float material_id   = g_mat_id / float(FLT_MAX_16);
    
    //= VELOCITY ================================================================================
    float2 position_current     = (input.position_ss_current.xy / input.position_ss_current.w);
    float2 position_previous    = (input.position_ss_previous.xy / input.position_ss_previous.w);
    float2 position_delta       = position_current - position_previous;
    float2 velocity             = (position_delta - g_taa_jitter_offset) * float2(0.5f, -0.5f);
    //===========================================================================================

    // Make TBN
    #if HEIGHT_MAP || NORMAL_MAP
    float3x3 TBN = makeTBN(input.normal, input.tangent);
    #endif

    #if HEIGHT_MAP
        // Parallax Mapping
        float height_scale      = g_mat_height * 0.04f;
        float3 camera_to_pixel  = normalize(g_camera_position - input.position.xyz);
        texCoords               = ParallaxMapping(tex_material_height, sampler_anisotropic_wrap, texCoords, camera_to_pixel, TBN, height_scale);
    #endif
    
    float mask_threshold = 0.6f;
    
    #if MASK_MAP
        float3 maskSample = tex_material_mask.Sample(sampler_anisotropic_wrap, texCoords).rgb;
        if (maskSample.r <= mask_threshold && maskSample.g <= mask_threshold && maskSample.b <= mask_threshold)
            discard;
    #endif

    #if ALBEDO_MAP
        float4 albedo_sample = tex_material_albedo.Sample(sampler_anisotropic_wrap, texCoords);
        if (albedo_sample.a <= mask_threshold)
            discard;

        albedo_sample.rgb = degamma(albedo_sample.rgb);
        albedo *= albedo_sample;
    #endif
    
    #if ROUGHNESS_MAP
        roughness *= tex_material_roughness.Sample(sampler_anisotropic_wrap, texCoords).r;
    #endif
    
    #if METALLIC_MAP
        metallic *= tex_material_metallic.Sample(sampler_anisotropic_wrap, texCoords).r;
    #endif
    
    #if NORMAL_MAP
        // Get tangent space normal and apply intensity
        float3 tangent_normal   = normalize(unpack(tex_material_normal.Sample(sampler_anisotropic_wrap, texCoords).rgb));
        float normal_intensity  = clamp(g_mat_normal, 0.012f, g_mat_normal);
        tangent_normal.xy       *= saturate(normal_intensity);
        normal                  = normalize(mul(tangent_normal, TBN).xyz); // Transform to world space
    #endif

    #if OCCLUSION_MAP
        occlusion = tex_material_occlusion.Sample(sampler_anisotropic_wrap, texCoords).r;
    #endif
    
    #if EMISSION_MAP
        emission = luminance(tex_material_emission.Sample(sampler_anisotropic_wrap, texCoords).rgb);
    #endif

    // Write to G-Buffer
    g_buffer.albedo     = albedo;
    g_buffer.normal     = float4(normal_encode(normal), material_id);
    g_buffer.material   = float4(roughness, metallic, emission, occlusion);
    g_buffer.velocity   = velocity;

    return g_buffer;
}
