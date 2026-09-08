/*
Copyright(c) 2015-2026 Panos Karabelas

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

#include "common.hlsl"

// Normalized scene-linear lens scattering. No threshold or luminance-dependent
// weights: a subpixel emitter must not change bloom energy as its coverage moves
// between pixels. Input has already passed through temporal reconstruction.
// Pyramid filtering: Jimenez, "Next Generation Post Processing", SIGGRAPH 2014.
// Reconstruction: positive cubic B-spline, evaluated with four bilinear taps.

float3 bloom_sample(Texture2D<float4> source, float2 uv)
{
    float3 color = source.SampleLevel(samplers[sampler_bilinear_clamp], uv, 0).rgb;
#if defined(PREFILTER)
    // One invalid HDR value must not poison the pyramid. Finite input remains
    // linear up to the representable range of the FP16 target.
    color = float3(isfinite(color.r) ? color.r : 0.0f,
                   isfinite(color.g) ? color.g : 0.0f,
                   isfinite(color.b) ? color.b : 0.0f);
    color = max(color, 0.0f);
    float peak = max(color.r, max(color.g, color.b));
    color *= min(1.0f, 65504.0f / max(peak, 1.0f));
#endif
    return color;
}

float3 bloom_downsample(Texture2D<float4> source, float2 uv, float2 texel)
{
    // Five overlapping boxes, 13 bilinear taps, sum of weights = 1.
    // Axial taps fill the sparse diagonal footprint of the old nine-tap kernel.
    float3 center = bloom_sample(source, uv);
    float3 axial = bloom_sample(source, uv + texel * float2(-2, 0))
                 + bloom_sample(source, uv + texel * float2( 2, 0))
                 + bloom_sample(source, uv + texel * float2( 0,-2))
                 + bloom_sample(source, uv + texel * float2( 0, 2));
    float3 outer = bloom_sample(source, uv + texel * float2(-2,-2))
                 + bloom_sample(source, uv + texel * float2( 2,-2))
                 + bloom_sample(source, uv + texel * float2(-2, 2))
                 + bloom_sample(source, uv + texel * float2( 2, 2));
    float3 inner = bloom_sample(source, uv + texel * float2(-1,-1))
                 + bloom_sample(source, uv + texel * float2( 1,-1))
                 + bloom_sample(source, uv + texel * float2(-1, 1))
                 + bloom_sample(source, uv + texel * float2( 1, 1));
    return center * 0.125f + axial * 0.0625f + outer * 0.03125f + inner * 0.125f;
}

float3 bloom_upsample(Texture2D<float4> source, float2 uv)
{
    uint width, height;
    source.GetDimensions(width, height);
    float2 size = float2(width, height);
    float2 position = uv * size - 0.5f;
    float2 base = floor(position);
    float2 f = position - base;
    float2 f2 = f * f;
    float2 f3 = f2 * f;
    float2 w0 = (1.0f - 3.0f * f + 3.0f * f2 - f3) / 6.0f;
    float2 w1 = (4.0f - 6.0f * f2 + 3.0f * f3) / 6.0f;
    float2 w2 = (1.0f + 3.0f * f + 3.0f * f2 - 3.0f * f3) / 6.0f;
    float2 w3 = f3 / 6.0f;
    float2 g0 = w0 + w1;
    float2 g1 = w2 + w3;
    float2 p0 = (base - 0.5f + w1 / g0) / size;
    float2 p1 = (base + 1.5f + w3 / g1) / size;
    float3 a = bloom_sample(source, float2(p0.x, p0.y));
    float3 b = bloom_sample(source, float2(p1.x, p0.y));
    float3 c = bloom_sample(source, float2(p0.x, p1.y));
    float3 d = bloom_sample(source, float2(p1.x, p1.y));
    // Positive weights: no ringing, negative colors or bilinear cell boundaries.
    return lerp(lerp(a, b, g1.x), lerp(c, d, g1.x), g1.y);
}

#if defined(PREFILTER) || defined(DOWNSAMPLE)
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (any(thread_id.xy >= uint2(width, height)))
        return;
    uint source_width, source_height;
    tex.GetDimensions(source_width, source_height);
    float2 uv = (float2(thread_id.xy) + 0.5f) / float2(width, height);
    float3 color = bloom_downsample(tex, uv, 1.0f / float2(source_width, source_height));
    tex_uav[thread_id.xy] = float4(min(color, 65504.0f), 1.0f);
}
#endif

#if defined(UPSAMPLE_BLEND_MIP)
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (any(thread_id.xy >= uint2(width, height)))
        return;
    float2 uv = (float2(thread_id.xy) + 0.5f) / float2(width, height);
    float3 high = tex_uav[thread_id.xy].rgb;
    float3 low = bloom_upsample(tex, uv);
    float scatter = clamp(pass_get_f3_value().x, 0.05f, 0.95f);
    // A normalized scale mixture keeps DC gain independent of the mip count.
    tex_uav[thread_id.xy] = float4(lerp(high, low, scatter), 1.0f);
}
#endif

#if defined(BLEND_FRAME)
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (any(thread_id.xy >= uint2(width, height)))
        return;
    float2 uv = (float2(thread_id.xy) + 0.5f) / float2(width, height);
    float4 scene = tex[thread_id.xy];
    float3 glow = bloom_upsample(tex2, uv);
    // r.bloom = 1 redistributes about 4% of light into the halo. Exposure applies
    // later to both terms together; constant scenes retain their brightness.
    float amount = 1.0f - exp2(-0.06f * max(pass_get_f3_value().x, 0.0f));
    tex_uav[thread_id.xy] = float4(lerp(scene.rgb, glow, amount), scene.a);
}
#endif
