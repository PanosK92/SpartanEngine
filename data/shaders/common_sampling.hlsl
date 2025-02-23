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

#ifndef SPARTAN_COMMON_SAMPLING
#define SPARTAN_COMMON_SAMPLING

struct sampling
{
    static float4 hash4(float2 p)
    {
        return frac(sin(float4(1.0 + dot(p, float2(37.0, 17.0)),
                           2.0 + dot(p, float2(11.0, 47.0)),
                           3.0 + dot(p, float2(41.0, 29.0)),
                           4.0 + dot(p, float2(23.0, 31.0)))) * 103.0);
    }

    // good but very expensive, could possibly be used at far distances which use lower mips
    static float4 sample_seamless_texture(float2 uv, uint texture_index, uint sampler_index, float v = 1.0f)
    {
        float2 p       = floor(uv);
        float2 f       = frac(uv);
        float2 ddx_val = ddx(uv);
        float2 ddy_val = ddy(uv);
        float3 va      = 0.0f;
        float w1       = 0.0f;
        float w2       = 0.0f;
    
        for (int j = -1; j <= 1; j++)
        {
            for (int i = -1; i <= 1; i++)
            {
                float2 g = float2(float(i), float(j));
                float4 o = hash4(p + g);
                float2 r = g - f + o.xy;
                float d  = dot(r, r);
                float w  = exp(-5.0 * d);
                float3 c = GET_TEXTURE(texture_index).SampleGrad(GET_SAMPLER(sampler_index), uv + v * o.zw, ddx_val, ddy_val).xyz;
            
                va += w * c;
                w1 += w;
                w2 += w * w;
            }
        }

        // contrast-preserving average
        float mean = 0.3;
        float3 res = mean + (va - w1 * mean) / sqrt(w2);
    
        return float4(lerp(va / w1, res, v), 1.0f);
    }

    static float4 smart(float3 position, float3 normal, float2 uv, uint texture_index, bool is_water, bool is_terrain)
    {
        // parameters
        const float sand_offset      = 0.75f;
        const float2 direction_1     = float2(1.0, 0.5);
        const float2 direction_2     = float2(-0.5, 1.0);
        const float speed_1          = 0.2;
        const float speed_2          = 0.15;

        float snow_blend_factor = get_snow_blend_factor(position);
        
        float4 color;
        if (is_water) // animate using interleaved UVs
        {
            float2 uv_interleaved_1 = uv + (float)buffer_frame.time * speed_1 * direction_1;
            float2 uv_interleaved_2 = uv + (float)buffer_frame.time * speed_2 * direction_2;
            float3 sample_1         = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_interleaved_1).rgb;
            float3 sample_2         = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv_interleaved_2).rgb;
            color                   = float4(normalize(sample_1 + sample_2), 0.0f);
        }
        else if (is_terrain) // slope based blending with sand, grass, rock and snow
        {
            float4 tex_grass = GET_TEXTURE(texture_index + 0).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv);
            float4 tex_rock  = GET_TEXTURE(texture_index + 1).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv);
            float4 tex_sand  = GET_TEXTURE(texture_index + 2).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv);
  
            // compute blend factors
            float sand_blend_factor    = saturate(position.y / sand_offset);
            float sand_blend_threshold = sea_level + sand_offset; // define a threshold above which no sand should appear
            float sand_factor          = saturate((position.y - sea_level) / (sand_blend_threshold - sea_level));
            sand_blend_factor          = 1.0f - sand_factor; // invert factor: 1 near sea level, 0 above the threshold

            // blend textures
            float slope    = saturate(pow(saturate(dot(normal, float3(0.0f, 1.0f, 0.0f)) - -0.25f), 24.0f));
            float4 terrain = lerp(tex_rock, tex_grass, slope);           // blend base terrain with slope
            color          = lerp(terrain, tex_sand, sand_blend_factor); // then blend in sand based on height
            color          = lerp(color, 0.95f, snow_blend_factor);      // blend in the snow
        }
        else // default texture sampling
        {
            color = GET_TEXTURE(texture_index).Sample(GET_SAMPLER(sampler_anisotropic_wrap), uv);
            
            // define snow color and blend with it (if needed)
            float4 snow_color = float4(0.95f, 0.95f, 0.95f, 1.0f);
            color             = lerp(color, snow_color, snow_blend_factor);
        }

        return color;
    }
};
#endif
