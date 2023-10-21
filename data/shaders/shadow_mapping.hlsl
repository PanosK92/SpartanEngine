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

//= INCLUDES =======================
#include "screen_space_shadows.hlsl"
//==================================

/*------------------------------------------------------------------------------
    SETTINGS
------------------------------------------------------------------------------*/
// technique
#define SampleShadowMap Technique_Vogel

// technique - all
static const uint   g_shadow_samples                 = 3;
static const float  g_shadow_filter_size             = 3.0f;
static const float  g_shadow_cascade_blend_threshold = 0.8f;
// technique - vogel
static const uint   g_penumbra_samples               = 8;
static const float  g_penumbra_filter_size           = 128.0f;
// technique - pre-calculated
static const float g_pcf_filter_size    = (sqrt((float)g_shadow_samples) - 1.0f) / 2.0f;
static const float g_shadow_samples_rpc = 1.0f / (float) g_shadow_samples;

/*------------------------------------------------------------------------------
    SHADER VARIABLES
------------------------------------------------------------------------------*/

float get_shadow_resolution()
{
    return pass_get_f3_value().y;
}

float get_shadow_texel_size()
{
    return (1.0f / get_shadow_resolution());
}

/*------------------------------------------------------------------------------
    LIGHT SHADOW MAP SAMPLING
------------------------------------------------------------------------------*/

float shadow_compare_depth(float3 uv, float compare)
{
    if (light_is_directional())
    {
        // float3 -> uv, slice
        return tex_light_directional_depth.SampleCmpLevelZero(samplers_comparison[sampler_compare_depth], uv, compare).r;
    }
    else if (light_is_point())
    {
        // float3 -> direction
        return tex_light_point_depth.SampleCmpLevelZero(samplers_comparison[sampler_compare_depth], uv, compare).r;
    }
    else if (light_is_spot())
    {
        // float3 -> uv, 0
        return tex_light_spot_depth.SampleCmpLevelZero(samplers_comparison[sampler_compare_depth], uv.xy, compare).r;
    }
    
    return 0.0f;
}

float shadow_sample_depth(float3 uv)
{
    if (light_is_directional())
    {
        // float3 -> uv, slice
        return tex_light_directional_depth.SampleLevel(samplers[sampler_point_clamp], uv, 0).r;
    }
    else if (light_is_point())
    {
        // float3 -> direction
        return tex_light_point_depth.SampleLevel(samplers[sampler_point_clamp], uv, 0).r;
    }
    else if (light_is_spot())
    {
        // float3 -> uv, 0
        return tex_light_spot_depth.SampleLevel(samplers[sampler_point_clamp], uv.xy, 0).r;
    }
    
    return 0.0f;
}

float3 shadow_sample_color(float3 uv)
{
    if (light_is_directional())
    {
        // float3 -> uv, slice
        return tex_light_directional_color.SampleLevel(samplers[sampler_point_clamp], uv, 0).rgb;
    }
    else if (light_is_point())
    {
        // float3 -> direction
        return tex_light_point_color.SampleLevel(samplers[sampler_point_clamp], uv, 0).rgb;
    }
    else if (light_is_spot())
    {
        // float3 -> uv, 0
        return tex_light_spot_color.SampleLevel(samplers[sampler_point_clamp], uv.xy, 0).rgb;
    }
    
    return 0.0f;
}

/*------------------------------------------------------------------------------
    PENUMBRA
------------------------------------------------------------------------------*/
float2 vogel_disk_sample(uint sample_index, uint sample_count, float angle)
{
    const float golden_angle = 2.399963f; // radians
    float r                  = sqrt(sample_index + 0.5f) / sqrt(sample_count);
    float theta              = sample_index * golden_angle + angle;
    float sine, cosine;
    sincos(theta, sine, cosine);
    
    return float2(cosine, sine) * r;
}

float compute_penumbra(float vogel_angle, float3 uv, float compare)
{
    float penumbra          = 1.0f;
    float blocker_depth_avg = 0.0f;
    uint blocker_count      = 0;

    for(uint i = 0; i < g_penumbra_samples; i ++)
    {
        float2 offset = vogel_disk_sample(i, g_penumbra_samples, vogel_angle) * get_shadow_texel_size() * g_penumbra_filter_size;
        float depth   = shadow_sample_depth(uv + float3(offset, 0.0f));

        if(depth > compare)
        {
            blocker_depth_avg += depth;
            blocker_count++;
        }
    }

    if (blocker_count != 0)
    {
        blocker_depth_avg /= (float)blocker_count;

        // Compute penumbra
        penumbra = (compare - blocker_depth_avg) / (blocker_depth_avg + FLT_MIN);
        penumbra *= penumbra;
        penumbra *= 10.0f;
    }
    
    return clamp(penumbra, 1.0f, FLT_MAX_16);
}

/*------------------------------------------------------------------------------
    TECHNIQUE - VOGEL
------------------------------------------------------------------------------*/
float Technique_Vogel(Surface surface, float3 uv, float compare)
{
    float shadow          = 0.0f;
    float temporal_offset = get_noise_interleaved_gradient(surface.uv * pass_get_resolution_out(), true, false);
    float temporal_angle  = temporal_offset * PI2;
    float penumbra        = light_is_directional() ? 1.0f : compute_penumbra(temporal_angle, uv, compare);

    // todo: in the case of the point light, the uv is the direction, filtering works ok but I could improved it.

    for (uint i = 0; i < g_shadow_samples; i++)
    {
        float2 offset = vogel_disk_sample(i, g_shadow_samples, temporal_angle) * get_shadow_texel_size() * g_shadow_filter_size * penumbra;
        shadow        += shadow_compare_depth(uv + float3(offset, 0.0f), compare);
    } 

    return shadow * g_shadow_samples_rpc;
}

float3 Technique_Vogel_Color(Surface surface, float3 uv)
{
    float3 shadow     = 0.0f;
    float vogel_angle = get_noise_interleaved_gradient(surface.uv * pass_get_resolution_out(), true, false) * PI2;

    // todo: in the case of the point light, the uv is the direction, filtering works ok but I could improved it.
    
    for (uint i = 0; i < g_shadow_samples; i++)
    {
        float2 offset = vogel_disk_sample(i, g_shadow_samples, vogel_angle) * get_shadow_texel_size() * g_shadow_filter_size;
        shadow        += shadow_sample_color(uv + float3(offset, 0.0f));
    } 

    return shadow * g_shadow_samples_rpc;
}

/*------------------------------------------------------------------------------
    TECHNIQUE - POISSON
------------------------------------------------------------------------------*/
static const float2 poisson_disk[64] =
{
    float2(-0.5119625f, -0.4827938f),
    float2(-0.2171264f, -0.4768726f),
    float2(-0.7552931f, -0.2426507f),
    float2(-0.7136765f, -0.4496614f),
    float2(-0.5938849f, -0.6895654f),
    float2(-0.3148003f, -0.7047654f),
    float2(-0.42215f, -0.2024607f),
    float2(-0.9466816f, -0.2014508f),
    float2(-0.8409063f, -0.03465778f),
    float2(-0.6517572f, -0.07476326f),
    float2(-0.1041822f, -0.02521214f),
    float2(-0.3042712f, -0.02195431f),
    float2(-0.5082307f, 0.1079806f),
    float2(-0.08429877f, -0.2316298f),
    float2(-0.9879128f, 0.1113683f),
    float2(-0.3859636f, 0.3363545f),
    float2(-0.1925334f, 0.1787288f),
    float2(0.003256182f, 0.138135f),
    float2(-0.8706837f, 0.3010679f),
    float2(-0.6982038f, 0.1904326f),
    float2(0.1975043f, 0.2221317f),
    float2(0.1507788f, 0.4204168f),
    float2(0.3514056f, 0.09865579f),
    float2(0.1558783f, -0.08460935f),
    float2(-0.0684978f, 0.4461993f),
    float2(0.3780522f, 0.3478679f),
    float2(0.3956799f, -0.1469177f),
    float2(0.5838975f, 0.1054943f),
    float2(0.6155105f, 0.3245716f),
    float2(0.3928624f, -0.4417621f),
    float2(0.1749884f, -0.4202175f),
    float2(0.6813727f, -0.2424808f),
    float2(-0.6707711f, 0.4912741f),
    float2(0.0005130528f, -0.8058334f),
    float2(0.02703013f, -0.6010728f),
    float2(-0.1658188f, -0.9695674f),
    float2(0.4060591f, -0.7100726f),
    float2(0.7713396f, -0.4713659f),
    float2(0.573212f, -0.51544f),
    float2(-0.3448896f, -0.9046497f),
    float2(0.1268544f, -0.9874692f),
    float2(0.7418533f, -0.6667366f),
    float2(0.3492522f, 0.5924662f),
    float2(0.5679897f, 0.5343465f),
    float2(0.5663417f, 0.7708698f),
    float2(0.7375497f, 0.6691415f),
    float2(0.2271994f, -0.6163502f),
    float2(0.2312844f, 0.8725659f),
    float2(0.4216993f, 0.9002838f),
    float2(0.4262091f, -0.9013284f),
    float2(0.2001408f, -0.808381f),
    float2(0.149394f, 0.6650763f),
    float2(-0.09640376f, 0.9843736f),
    float2(0.7682328f, -0.07273844f),
    float2(0.04146584f, 0.8313184f),
    float2(0.9705266f, -0.1143304f),
    float2(0.9670017f, 0.1293385f),
    float2(0.9015037f, -0.3306949f),
    float2(-0.5085648f, 0.7534177f),
    float2(0.9055501f, 0.3758393f),
    float2(0.7599946f, 0.1809109f),
    float2(-0.2483695f, 0.7942952f),
    float2(-0.4241052f, 0.5581087f),
    float2(-0.1020106f, 0.6724468f),
};

float Technique_Poisson(Surface surface, float3 uv, float compare)
{
    float shadow          = 0.0f;
    float temporal_offset = get_noise_interleaved_gradient(uv.xy * get_shadow_resolution(), true, false); // helps with noise if TAA is active

    for (uint i = 0; i < g_shadow_samples; i++)
    {
        uint index    = uint(g_shadow_samples * get_random(uv.xy * i)) % g_shadow_samples; // A pseudo-random number between 0 and 15, different for each pixel and each index
        float2 offset = (poisson_disk[index] + temporal_offset) * get_shadow_texel_size() * g_shadow_filter_size;
        shadow        += shadow_compare_depth(uv + float3(offset, 0.0f), compare);
    }   

    return shadow * g_shadow_samples_rpc;
}

/*------------------------------------------------------------------------------
    TECHNIQUE - PCF
------------------------------------------------------------------------------*/
float Technique_Pcf(Surface surface, float3 uv, float compare)
{
    float shadow = 0.0f;

    for (float y = -g_pcf_filter_size; y <= g_pcf_filter_size; y++)
    {
        for (float x = -g_pcf_filter_size; x <= g_pcf_filter_size; x++)
        {
            float2 offset = float2(x, y) * get_shadow_texel_size();
            shadow        += shadow_compare_depth(uv + float3(offset, 0.0f), compare);
        }
    }
    
    return shadow * g_shadow_samples_rpc;
}

/*------------------------------------------------------------------------------
    BIAS
------------------------------------------------------------------------------*/
inline void auto_bias(Surface surface, inout float3 position, Light light, float bias_mul = 1.0f)
{
    //// Receiver plane bias (slope scaled basically)
    //float3 du                   = ddx(position);
    //float3 dv                   = ddy(position);
    //float2 receiver_plane_bias  = mul(transpose(float2x2(du.xy, dv.xy)), float2(du.z, dv.z));
    
    //// Static depth biasing to make up for incorrect fractional sampling on the shadow map grid
    //float sampling_error = min(2.0f * dot(g_shadow_texel_size, abs(receiver_plane_bias)), 0.01f);

    // Scale down as the user is interacting with much bigger, non-fractional values (just a UX approach)
    float fixed_factor = 0.0001f;
    
    // Slope scaling
    float slope_factor = (1.0f - saturate(light.n_dot_l));

    // Apply bias
    position.z += fixed_factor * slope_factor * light.bias * (bias_mul + 1.0f);
}

inline float3 bias_normal_offset(Surface surface, Light light, float3 normal)
{
    return normal * (1.0f - saturate(light.n_dot_l)) * light.normal_bias * get_shadow_texel_size() * 10;
}

/*------------------------------------------------------------------------------
    ENTRYPOINT
------------------------------------------------------------------------------*/
float4 Shadow_Map(Surface surface, Light light)
{ 
    float3 position_world = surface.position + bias_normal_offset(surface, light, surface.normal);
    float4 shadow         = 1.0f;

    if (light_is_directional())
    {
        // TODO send from CPU constbuffer Light instead
        uint cascade_count = 3;
        for (uint cascade_index = 0; cascade_index < cascade_count; cascade_index++)
        {
            // project into light space
            float3 pos_ndc = world_to_ndc(position_world, buffer_light.view_projection[cascade_index]);
            float2 pos_uv  = ndc_to_uv(pos_ndc);

            // ensure not out of bound
            if (is_saturated(pos_uv))
            {
                // sample primary cascade
                auto_bias(surface, pos_ndc, light, cascade_index);
                shadow.a = SampleShadowMap(surface, float3(pos_uv, cascade_index), pos_ndc.z);

                if (light_has_shadows_transparent())
                {
                    if (shadow.a > 0.0f && surface.is_opaque())
                    {
                        shadow.rgb *= Technique_Vogel_Color(surface, float3(pos_uv, cascade_index));
                    }
                }

                // if we are close to the edge a secondary cascade exists, lerp with it.
                float cascade_fade = (max2(abs(pos_ndc.xy)) - g_shadow_cascade_blend_threshold) * 4.0f;
                uint cascade_index_next = cascade_index + 1;

                if (cascade_fade > 0.0f && cascade_index_next < cascade_count - 1)
                {
                    // project into light space
                    pos_ndc = world_to_ndc(position_world, buffer_light.view_projection[cascade_index_next]);
                    pos_uv  = ndc_to_uv(pos_ndc);

                    // sample secondary cascade
                    auto_bias(surface, pos_ndc, light, cascade_index_next);
                    float shadow_secondary = SampleShadowMap(surface, float3(pos_uv, cascade_index_next), pos_ndc.z);

                    // blend cascades
                    shadow.a = lerp(shadow.a, shadow_secondary, cascade_fade);
                    
                    if (light_has_shadows_transparent())
                    {
                        if (shadow.a > 0.0f && surface.is_opaque())
                        {
                            shadow.rgb = min(shadow.rgb, Technique_Vogel_Color(surface, float3(pos_uv, cascade_index_next)));
                        }
                    }
                }

                break;
            }
        }
    }
    else if (light_is_point())
    {
        if (light.distance_to_pixel < light.far)
        {
            // project into light space
            uint slice_index  = direction_to_cube_face_index(light.to_pixel);
            float3 pos_ndc    = world_to_ndc(position_world, buffer_light.view_projection[slice_index]);

            auto_bias(surface, pos_ndc, light);
            shadow.a = SampleShadowMap(surface, light.to_pixel, pos_ndc.z);
            
            if (light_has_shadows_transparent())
            {
                if (shadow.a > 0.0f && surface.is_opaque())
                {
                    shadow.rgb *= Technique_Vogel_Color(surface, light.to_pixel);
                }
            }
        }
    }
    else if (light_is_spot())
    {
        if (light.distance_to_pixel < light.far)
        {
            // project into light space
            float3 pos_ndc  = world_to_ndc(position_world, buffer_light.view_projection[0]);
            float3 pos_uv   = float3(ndc_to_uv(pos_ndc), 0);

            // ensure not out of bound
            if (is_saturated(pos_uv.xy))
            {
                auto_bias(surface, pos_ndc, light);
                shadow.a = SampleShadowMap(surface, pos_uv, pos_ndc.z);

                if (light_has_shadows_transparent())
                {
                    if (shadow.a > 0.0f && surface.is_opaque())
                    {
                        shadow.rgb *= Technique_Vogel_Color(surface, pos_uv);
                    }
                }
            }
        }
    }

    return shadow;
}
