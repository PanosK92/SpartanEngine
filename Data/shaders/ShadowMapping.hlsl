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

//= INCLUDES =====================
#include "ScreenSpaceShadows.hlsl"
//================================

/*------------------------------------------------------------------------------
    SETTINGS
------------------------------------------------------------------------------*/
// technique
#define SampleShadowMap Technique_Vogel

// technique - all
static const uint g_shadow_samples      = 4;
static const uint g_penumbra_samples    = 16;

// technique - vogel
static const float g_shadow_vogel_filter_size = 3.0f;

// technique - poisson
static const float g_shadow_poisson_filter_size = 5.0f;

// technique - pre-calculated
static const float g_pcf_filter_size = (sqrt((float)g_shadow_samples) - 1.0f) / 2.0f;

/*------------------------------------------------------------------------------
    DEPTH SAMPLING
------------------------------------------------------------------------------*/
float compare_depth(float3 uv, float compare)
{
    #if DIRECTIONAL
    // float3 -> uv, slice
    return light_directional_depth.SampleCmpLevelZero(sampler_compare_depth, uv, compare).r;
    #elif POINT
    // float3 -> direction
    return light_point_depth.SampleCmpLevelZero(sampler_compare_depth, uv, compare).r;
    #elif SPOT
    // float3 -> uv, 0
    return light_spot_depth.SampleCmpLevelZero(sampler_compare_depth, uv.xy, compare).r;
    #endif

    return 0.0f;
}

float sample_depth(float3 uv)
{
    #if DIRECTIONAL
    // float3 -> uv, slice
    return light_directional_depth.SampleLevel(sampler_point_clamp, uv, 0).r;
    #elif POINT
    // float3 -> direction
    return light_point_depth.SampleLevel(sampler_point_clamp, uv, 0).r;
    #elif SPOT
    // float3 -> uv, 0
    return light_spot_depth.SampleLevel(sampler_point_clamp, uv.xy, 0).r;
    #endif

    return 0.0f;
}

float4 sample_color(float3 uv)
{
    #if DIRECTIONAL
    // float3 -> uv, slice
    return light_directional_color.SampleLevel(sampler_point_clamp, uv, 0);
    #elif POINT
    // float3 -> direction
    return light_point_color.SampleLevel(sampler_point_clamp, uv, 0);
    #elif SPOT
    // float3 -> uv, 0
    return light_spot_color.SampleLevel(sampler_point_clamp, uv.xy, 0);
    #endif

    return 0.0f;
}

/*------------------------------------------------------------------------------
    PENUMBRA
------------------------------------------------------------------------------*/
float2 vogel_disk_sample(uint sample_index, uint sample_count, float angle)
{
  float golden_angle    = 2.4f;
  float r               = sqrt(sample_index + 0.5f) / sqrt(sample_count);
  float theta           = sample_index * golden_angle + angle;
  float sine, cosine;
  sincos(theta, sine, cosine);
  
  return float2(r * cosine, r * sine);
}

float AvgBlockersDepthToPenumbra(float z_shadowMapView, float avg_blocker_depth)
{
    float penumbra = (z_shadowMapView - avg_blocker_depth) / avg_blocker_depth;
    penumbra *= penumbra;
    return saturate(80.0f * penumbra);
}

float Penumbra(float gradient_noise, float2 uv, float z_shadowMapView, int cascade)
{
    float avg_blocker_depth = 0.0f;
    float blockers_count    = 0.0f;
    
    [unroll]
    for(uint i = 0; i < g_penumbra_samples; i ++)
    {
        float2 penumbraFilterMaxSize    = 1.0f;
        float2 offset                   = vogel_disk_sample(i, g_penumbra_samples, gradient_noise);
        float depth_sample              = sample_depth(float3(uv + penumbraFilterMaxSize * offset * g_shadow_texel_size, cascade));

        //matrix projection = light_view_projection[0][cascade];
        //depth_sample = projection._43 / (depth_sample - projection._33);

        if(depth_sample > z_shadowMapView)
        {
            avg_blocker_depth += depth_sample;
            blockers_count += 1.0f;
        }
    }

    if(blockers_count > 0.0f)
    {
        avg_blocker_depth /= blockers_count;
        return AvgBlockersDepthToPenumbra(z_shadowMapView, avg_blocker_depth);
    }
    
    return 0.0f;
}

/*------------------------------------------------------------------------------
    TECHNIQUE - VOGEL
------------------------------------------------------------------------------*/
float Technique_Vogel(float3 uv, float compare)
{
    float shadow        = 0.0f;
    float vogel_angle   = interleaved_gradient_noise(g_shadow_resolution * uv.xy) * PI2;
    
    //float penumbra = Penumbra(vogel_angle, uv, compare, cascade);

    [unroll]
    for (uint i = 0; i < g_shadow_samples; i++)
    {
        float2 offset   = vogel_disk_sample(i, g_shadow_samples, vogel_angle) * g_shadow_texel_size * g_shadow_vogel_filter_size;
        shadow          += compare_depth(uv + float3(offset, 0.0f), compare);
    } 

    return shadow / (float)g_shadow_samples;
}

float4 Technique_Vogel_Color(float3 uv)
{
    float4 shadow       = 0.0f;
    float vogel_angle   = interleaved_gradient_noise(g_shadow_resolution * uv.xy) * PI2;
    
    //float penumbra = Penumbra(vogel_angle, uv, compare, cascade);

    [unroll]
    for (uint i = 0; i < g_shadow_samples; i++)
    {
        float2 offset   = vogel_disk_sample(i, g_shadow_samples, vogel_angle) * g_shadow_texel_size * g_shadow_vogel_filter_size;
        shadow          += sample_color(uv + float3(offset, 0.0f));
    } 

    return shadow / (float)g_shadow_samples;
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

float Technique_Poisson(float3 uv, float compare)
{
    float shadow    = 0.0f;
    float temporal  = ceil(frac(g_time)) * any(g_taa_jitter_offset); // helps with noise if TAA is active

    [unroll]
    for (uint i = 0; i < g_shadow_samples; i++)
    {
        uint index      = uint(g_shadow_samples * random(uv.xy * i) + temporal) % g_shadow_samples; // A pseudo-random number between 0 and 15, different for each pixel and each index
        float2 offset   = poisson_disk[index] * g_shadow_texel_size * g_shadow_poisson_filter_size;
        shadow          += compare_depth(uv + float3(offset, 0.0f), compare);
    }   

    return shadow / (float)g_shadow_samples;
}

/*------------------------------------------------------------------------------
    TECHNIQUE - PCF
------------------------------------------------------------------------------*/
float Technique_Pcf(float3 uv, float compare)
{
    float shadow = 0.0f;

    [unroll]
    for (float y = -g_pcf_filter_size; y <= g_pcf_filter_size; y++)
    {
        [unroll]
        for (float x = -g_pcf_filter_size; x <= g_pcf_filter_size; x++)
        {
            float2 offset    = float2(x, y) * g_shadow_texel_size;
            shadow           += compare_depth(uv + float3(offset, 0.0f), compare);   
        }
    }
    
    return shadow / (float)g_shadow_samples;
}

/*------------------------------------------------------------------------------
    BIAS
------------------------------------------------------------------------------*/
inline float bias_sloped_scaled(float z, float bias)
{
    const float dmax    = 0.001f;
    float zdx           = abs(ddx(z));
    float zdy           = abs(ddy(z));
    float scale         = clamp(max(zdx, zdy), 0.0f, dmax);
    
    return z + bias * scale;
}

inline float3 bias_normal_offset(Light light, float3 normal)
{
    float n_dot_l = 1.0f - dot(normal, normalize(-light.direction));
    return normal * n_dot_l * light.normal_bias * g_shadow_texel_size * 10;
}

/*------------------------------------------------------------------------------
    ENTRYPOINT
------------------------------------------------------------------------------*/
float4 Shadow_Map(Surface surface, Light light, bool transparent_pixel)
{ 
    float3 position_world   = surface.position + bias_normal_offset(light, surface.normal);
    float4 shadow           = 1.0f;

    #if DIRECTIONAL
    {
        for (uint cascade = 0; cascade < light.array_size; cascade++)
        {
            // Compute position in clip space for primary cascade
            float3 pos = project(position_world, light_view_projection[cascade]);
            
            // If the position exists within the cascade, sample it
            [branch]
            if (is_saturated(pos))
            {   
                // Sample primary cascade
                float compare_depth = bias_sloped_scaled(pos.z, light.bias * (cascade + 1));
                shadow.a            = SampleShadowMap(float3(pos.xy, cascade), compare_depth);

                #if SHADOWS_TRANSPARENT
                [branch]
                if (shadow.a > 0.0f && !transparent_pixel)
                {
                    shadow *= Technique_Vogel_Color(float3(pos.xy, cascade));
                }
                #endif

                // If we are close to the edge of the primary cascade and a secondary cascade exists, lerp with it.
                static const float blend_threshold = 0.1f;
                float distance_to_edge = 1.0f - max3(abs(pos * 2.0f - 1.0f));
                [branch]
                if (distance_to_edge < blend_threshold && cascade < light.array_size)
                {
                    int cacade_secondary = cascade + 1;

                    // Compute position in clip space for secondary cascade
                    pos = project(position_world, light_view_projection[cacade_secondary]);

                    // Sample secondary cascade
                    compare_depth           = bias_sloped_scaled(pos.z, light.bias * (cacade_secondary + 1));
                    float shadow_secondary  = SampleShadowMap(float3(pos.xy, cacade_secondary), compare_depth);

                    // Blend cascades
                    float alpha = smoothstep(0.0f, blend_threshold, distance_to_edge);
                    shadow.a = lerp(shadow_secondary, shadow.a, alpha);
                    
                    #if SHADOWS_TRANSPARENT
                    [branch]
                    if (shadow.a > 0.0f && !transparent_pixel)
                    {
                        shadow = min(shadow, Technique_Vogel_Color(float3(pos.xy, cacade_secondary)));
                    }
                    #endif
                }

                break;
            }
        }
    }
    #elif POINT
    {
        [branch]
        if (light.distance_to_pixel < light.range)
        {
            uint projection_index   = direction_to_cube_face_index(light.direction);
            float pos_z             = project_depth(position_world, light_view_projection[projection_index]);   
            float compare_depth     = bias_sloped_scaled(pos_z, light.bias);
            shadow.a                = SampleShadowMap(light.direction, compare_depth);
            
            #if SHADOWS_TRANSPARENT
            [branch]
            if (shadow.a > 0.0f && !transparent_pixel)
            {
                shadow *= sample_color(light.direction);
            }
            #endif
        }
    }
    #elif SPOT
    {
        [branch]
        if (light.distance_to_pixel < light.range)
        {
            float3 pos_clip     = project(position_world, light_view_projection[0]);
            float compare_depth = bias_sloped_scaled(pos_clip.z, light.bias);
            shadow.a            = SampleShadowMap(float3(pos_clip.xy, 0.0f), compare_depth);

            #if SHADOWS_TRANSPARENT
            [branch]
            if (shadow.a > 0.0f  && !transparent_pixel)
            {
                shadow *= sample_color(float3(pos_clip.xy, 0.0f));
            }
            #endif
        }
    }
    #endif
    
    return shadow;
}

