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

//= INCLUDES =======
#include "SSCS.hlsl"
//==================

/*------------------------------------------------------------------------------
    SETTINGS
------------------------------------------------------------------------------*/
// technique
#define SampleShadowMap Technique_Vogel

// technique - all
static const uint g_shadow_samples = 16;

// technique - vogel
static const float g_shadow_vogel_filter_size = 2.5f;

// technique - poisson
static const float g_shadow_poisson_filter_size = 5.0f;

// technique - pre-calculated
static const float g_pcf_filter_size = (sqrt((float)g_shadow_samples) - 1.0f) / 2.0f;

/*------------------------------------------------------------------------------
    TECHNIQUE REQUIREMENTS
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

float2 vogel_disk_sample(int sampleIndex, int samplesCount, float phi)
{
  float GoldenAngle = 2.4f;

  float r = sqrt(sampleIndex + 0.5f) / sqrt(samplesCount);
  float theta = sampleIndex * GoldenAngle + phi;

  float sine, cosine;
  sincos(theta, sine, cosine);
  
  return float2(r * cosine, r * sine);
}

/*------------------------------------------------------------------------------
    DEPTH COMPARISON
------------------------------------------------------------------------------*/
float compare_depth(float3 uv, float compare)
{
    #if DIRECTIONAL
    // float3 -> uv, slice
    return light_depth_directional.SampleCmpLevelZero(sampler_compare_depth, uv, compare).r;
    #elif POINT
    // float3 -> direction
    return light_depth_point.SampleCmp(sampler_compare_depth, uv, compare).r;
    #elif SPOT
    // float2 -> uv, 0
    return light_depth_spot.SampleCmp(sampler_compare_depth, uv.xy, compare).r;
    #endif

    return 0.0f;
}

/*------------------------------------------------------------------------------
    TECHNIQUES
------------------------------------------------------------------------------*/
float Technique_Vogel(int cascade, float2 uv, float compare)
{
    float shadow    = 0.0f;
    float vogel_phi = interleaved_gradient_noise(g_shadow_resolution * uv);

    [unroll]
    for (uint i = 0; i < g_shadow_samples; i++)
    {
        float2 offset   = vogel_disk_sample(i, g_shadow_samples, vogel_phi) * g_shadow_texel_size * g_shadow_vogel_filter_size;
        shadow          += compare_depth(float3(uv + offset, cascade), compare);
    }   

    return shadow / (float)g_shadow_samples;
}

float Technique_Poisson(int cascade, float2 uv, float compare)
{
    float shadow    = 0.0f;
	float temporal  = ceil(frac(g_time)) * any(g_taa_jitter_offset); // helps with noise if TAA is active

    [unroll]
    for (uint i = 0; i < g_shadow_samples; i++)
    {
        uint index      = uint(g_shadow_samples * random(uv * i) + temporal) % g_shadow_samples; // A pseudo-random number between 0 and 15, different for each pixel and each index
        float2 offset   = poisson_disk[index] * g_shadow_texel_size * g_shadow_poisson_filter_size;
        shadow          += compare_depth(float3(uv + offset, cascade), compare);
    }   

    return shadow / (float)g_shadow_samples;
}

float Technique_Pcf(int cascade, float2 uv, float compare)
{
    float shadow = 0.0f;

    [unroll]
    for (float y = -g_pcf_filter_size; y <= g_pcf_filter_size; y++)
    {
        [unroll]
        for (float x = -g_pcf_filter_size; x <= g_pcf_filter_size; x++)
        {
            float2 offset    = float2(x, y) * g_shadow_texel_size;
            shadow           += compare_depth(float3(uv + offset, cascade), compare);   
        }
    }
    
    return shadow / (float)g_shadow_samples;
}

/*------------------------------------------------------------------------------
    ENTRYPOINT
------------------------------------------------------------------------------*/
float Shadow_Map(float2 uv, float3 normal, float depth, float3 world_pos, Light light)
{
    float n_dot_l                   = dot(normal, normalize(-light.direction));
    float cos_angle                 = saturate(1.0f - n_dot_l);
    float3 scaled_normal_offset     = normal * light.normal_bias * cos_angle * g_shadow_texel_size  * 10;
    float4 position_world           = float4(world_pos + scaled_normal_offset, 1.0f);
    float shadow                    = 1.0f;

    #if DIRECTIONAL
    {
        for (int cascade = 0; cascade < cascade_count; cascade++)
        {
            // Compute clip space position and uv for primary cascade
            float3 pos  = mul(position_world, light_view_projection[light.index][cascade]).xyz;
            float3 uv   = pos * float3(0.5f, -0.5f, 0.5f) + 0.5f;
            
            // If the position exists within the cascade, sample it
            [branch]
            if (is_saturated(uv))
            {   
                // Sample primary cascade
                float compare_depth     = pos.z + (light.bias * (cascade + 1));
                float shadow_primary    = SampleShadowMap(cascade, uv.xy, compare_depth);
                float cascade_lerp      = (max2(abs(pos.xy)) - 0.9f) * 10.0f;

                // If we are close to the edge of the primary cascade and a secondary cascade exists, lerp with it.
                [branch]
                if (cascade_lerp > 0.0f && cascade < cascade_count - 1)
                {
                    int cacade_secondary = cascade + 1;

                    // Coute clip space position and uv for secondary cascade
                    pos = mul(position_world, light_view_projection[light.index][cacade_secondary]).xyz;
                    uv  = pos * float3(0.5f, -0.5f, 0.5f) + 0.5f;

                    // Sample secondary cascade
                    compare_depth           = pos.z + (light.bias * (cacade_secondary + 1));
                    float shadow_secondary  = SampleShadowMap(cacade_secondary, uv.xy, compare_depth);

                    // Blend cascades   
                    shadow = lerp(shadow_primary, shadow_secondary, cascade_lerp);
                }
                else
                {
                    shadow = shadow_primary;
                }

                break;
            }
        }
    }
    #elif POINT
    {
        float3 light_to_pixel_direction = position_world.xyz - light.position;
        float light_to_pixel_distance   = length(light_to_pixel_direction);
        light_to_pixel_direction        = normalize(light_to_pixel_direction);
        
        [branch]
        if (light_to_pixel_distance < light.range)
        {
            ///float depth = light_depth_point.Sample(samplerLinear_clamp, light_to_pixel_direction).r;
            //shadow = depth < (light_to_pixel_distance / light.range) ? 1.0f : 0.0f;

            float compare = (light_to_pixel_distance / light.range) + light.bias;
            return light_depth_point.SampleCmpLevelZero(sampler_compare_depth, light_to_pixel_direction, compare).r;
        }
    }
    #elif SPOT
    {
        float light_to_pixel_distance   = length(position_world.xyz - light.position);
        float2 uv                       = project(position_world.xyz, light_view_projection[light.index][0]);   
        float compare                   = (light_to_pixel_distance / light.range) + light.bias;
        return light_depth_spot.SampleCmpLevelZero(sampler_compare_depth, uv, compare).r;
    }
    #endif

    // Screen space contact shadow
    if (normalBias_shadow_volumetric_contact[light.index].w)
    {
        float sscs = ScreenSpaceContactShadows(tex_depth, uv, light.direction);
        shadow = min(shadow, sscs); 
    }
    
    return shadow;
}
