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

static const float g_quality_distance_low = 200.0f;

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
    float4 albedo   : SV_Target0;
    float4 normal   : SV_Target1;
    float4 material : SV_Target2;
    float2 velocity : SV_Target3;
};

struct sampling
{
    static float4 interleave(Texture2D texture_1, Texture2D texture_2, float2 uv)
    {
        // constants for scale and direction of the normal map movement
        float2 direction_1 = float2(1.0, 0.5);
        float2 direction_2 = float2(-0.5, 1.0);
        float scale_1      = 0.5;
        float scale_2      = 0.5;
        float speed_1      = 0.2;
        float speed_2      = 0.15;

        // calculate unique UV offsets for the two normal maps
        float2 uv_1 = uv + buffer_frame.time * speed_1 * direction_1;
        float2 uv_2 = uv + buffer_frame.time * speed_2 * direction_2;

        // sample
        float4 sample_1 = texture_1.Sample(samplers[sampler_anisotropic_wrap], uv_1 * scale_1);
        float4 sample_2 = texture_2.Sample(samplers[sampler_anisotropic_wrap], uv_2 * scale_2);

        // blend
        return sample_1 + sample_2;
    }

    static float4 reduce_tiling(Texture2D _tex, float2 uv, float variation)
    {
        float random_value = tex_noise_blue.Sample(samplers[sampler_anisotropic_wrap], float3(uv * 0.005f, 0)).x; // low frequency lookup

        float2 duvdx = ddx(uv);
        float2 duvdy = ddy(uv);

        float l = random_value * 8.0f;
        float f = frac(l);

        float ia = floor(l);
        float ib = ia + 1.0f;

        // hash function for offsets
        float2 off_a = sin(float2(3.0f, 7.0f) * ia);
        float2 off_b = sin(float2(3.0f, 7.0f) * ib);

        // sample the texture with offsets and gradients
        float4 col_a = _tex.SampleGrad(samplers[sampler_anisotropic_wrap], uv + variation * off_a, duvdx, duvdy);
        float4 col_b = _tex.SampleGrad(samplers[sampler_anisotropic_wrap], uv + variation * off_b, duvdx, duvdy);

        // blend the samples
        float blend_factor   = smoothstep(0.2f, 0.8f, f - 0.1f * ((col_a.x - col_b.x) + (col_a.y - col_b.y) + (col_a.z - col_b.z)+ (col_a.w - col_b.w)));
        float4 blended_color = lerp(col_a, col_b, blend_factor);

        return blended_color;
    }

    static float4 smart(Texture2D texture_1, Texture2D texture_2, float2 uv, float slope)
    {
        // in case of water, we just interleave the normal
        if (material_vertex_animate_water())
        {
            float4 normal = interleave(texture_1, texture_1, uv);
            return float4(normalize(normal.xyz), 0.0f);
        }

        // in case of the terrain, we do slope based texturing with tiling removal
        if (material_texture_slope_based())
        {
            float variation  = 1.0f;
            float4 tex_flat  = reduce_tiling(texture_1, uv, variation);
            float4 tex_slope = reduce_tiling(texture_2, uv * 0.3f, variation);
            return lerp(tex_slope, tex_flat, slope);
        }

        // this is a regular sample
        return texture_1.Sample(samplers[sampler_anisotropic_wrap], uv);
    }
};

float compute_slope(float3 normal)
{
    float bias  = -0.1f; // increase the bias to favour the slope/rock texture
    float slope = saturate(dot(normal, float3(0.0f, 1.0f, 0.0f)) - bias);
    slope       = pow(slope, 16.0f); // increase the exponent to sharpen the transition

    return slope;
}

PixelInputType mainVS(Vertex_PosUvNorTan input, uint instance_id : SV_InstanceID)
{
    PixelInputType output;

    // position
    output.position             = compute_screen_space_position(input, instance_id, buffer_pass.transform, buffer_frame.view_projection, buffer_frame.time);
    output.position_ss_current  = output.position;
    output.position_ss_previous = compute_screen_space_position(input, instance_id, pass_get_transform_previous(), buffer_frame.view_projection_previous, buffer_frame.time - buffer_frame.delta_time, output.position_world);
    // normals
    output.normal_world  = normalize(mul(input.normal,  (float3x3)buffer_pass.transform)).xyz;
    output.tangent_world = normalize(mul(input.tangent, (float3x3)buffer_pass.transform)).xyz;
    // uv
    output.uv = input.uv;
    
    return output;
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
    
    // velocity
    {
        // convert to ndc
        float2 position_ndc_current  = (input.position_ss_current.xy / input.position_ss_current.w);
        float2 position_ndc_previous = (input.position_ss_previous.xy / input.position_ss_previous.w);
    
        // remove the ndc jitter
        position_ndc_current  -= buffer_frame.taa_jitter_current;
        position_ndc_previous -= buffer_frame.taa_jitter_previous;
    
        // compute the velocity
        velocity = ndc_to_uv(position_ndc_current) - ndc_to_uv(position_ndc_previous);
    }

    // uv
    float2 uv = input.uv;
    uv        = float2(uv.x * buffer_material.tiling.x + buffer_material.offset.x, uv.y * buffer_material.tiling.y + buffer_material.offset.y);
    
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
        float4 albedo_sample = sampling::smart(tex_material_albedo, tex_material_albedo_2, uv, slope);

        // read albedo's alpha channel as an alpha mask as well
        alpha_mask      = min(alpha_mask, albedo_sample.a);
        albedo_sample.a = 1.0f;
        
        albedo_sample.rgb  = degamma(albedo_sample.rgb);
        albedo            *= albedo_sample;
    }

    // discard masked pixels
    if (alpha_mask <= get_alpha_threshold(input.position_world))
        discard;

    // compute pixel distance
    float3 camera_to_pixel_world = buffer_frame.camera_position - input.position_world.xyz;
    float pixel_distance         = length(camera_to_pixel_world);

    if (pixel_distance < g_quality_distance_low)
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
            float3 normal_sample       = sampling::smart(tex_material_normal, tex_material_normal2, uv, slope).xyz;
            float3 tangent_normal      = normalize(unpack(normal_sample));
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
    g_buffer.albedo   = albedo;
    g_buffer.normal   = float4(normal, buffer_material.id % 1024);
    g_buffer.material = float4(roughness, metalness, emission, occlusion);
    g_buffer.velocity = velocity;

    return g_buffer;
}
