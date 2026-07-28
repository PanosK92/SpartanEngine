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

//= includes =========
#include "common.hlsl"
//====================

// asset preview backdrop and wireframe recolour, runs on the display ready frame of a
// secondary view only, the sky stays the lighting source while the visible background
// becomes a flat studio tone so thin wires cannot blend into it

// a soft radial falloff, a dead flat fill reads like a broken render, this reads like a backdrop
static const float VIGNETTE_CENTER = 1.18f;
static const float VIGNETTE_EDGE   = 0.62f;
static const float VIGNETTE_REACH  = 1.35f;

// wire and point colours, picked for contrast against the backdrop rather than for realism
static const float3 WIRE_ON_DARK  = float3(0.62f, 0.86f, 1.00f);
static const float3 WIRE_ON_LIGHT = float3(0.04f, 0.12f, 0.24f);

// how much wire colour bleeds into a pixel next to a wire, thickens a one pixel line to read
static const float WIRE_SPILL = 0.55f;

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    float2 resolution;
    tex_uav.GetDimensions(resolution.x, resolution.y);
    if (any(thread_id.xy >= uint2(resolution)))
        return;

    const float3 backdrop_tint = pass_get_f3_value();
    const float  replace_sky   = pass_get_f3_value2().x;
    // 0 leaves the shaded wires alone, 1 picks the bright wire, 2 picks the dark wire
    const float  wire_mode     = pass_get_f3_value2().y;

    float2 uv        = (thread_id.xy + 0.5f) / resolution;
    float2 uv_depth  = uv * get_render_uv_scale();
    float4 source    = tex_uav[thread_id.xy];

    // a wire keeps whatever the material shaded it to, which is why glass against a bright sky
    // disappears, replace it with a fixed colour chosen against the backdrop brightness instead
    const float3 wire = wire_mode > 1.5f ? WIRE_ON_LIGHT : WIRE_ON_DARK;

    // reverse z, the far value is zero, so anything the rasterizer never touched is background,
    // in wireframe and vertex modes only the edges and points write depth which makes this an
    // exact mask for the geometry with no extra pass
    const bool is_background = get_depth(uv_depth) <= 0.0f;

    if (is_background)
    {
        if (replace_sky < 0.5f)
            return;

        float  falloff  = saturate(length(uv - 0.5f) * VIGNETTE_REACH);
        float  gradient = lerp(VIGNETTE_CENTER, VIGNETTE_EDGE, falloff * falloff);
        float3 backdrop = saturate(backdrop_tint * gradient);

        // a one pixel wire is technically visible and practically not, spill a partial wire into
        // the four neighbours of a hit so edges read as a line and gain cheap antialiasing
        if (wire_mode >= 0.5f)
        {
            float2 texel   = 1.0f / max(buffer_frame.resolution_render, float2(1.0f, 1.0f));
            float  neighbour =
                step(0.0f, -get_depth(uv_depth + float2( texel.x, 0.0f))) +
                step(0.0f, -get_depth(uv_depth + float2(-texel.x, 0.0f))) +
                step(0.0f, -get_depth(uv_depth + float2(0.0f,  texel.y))) +
                step(0.0f, -get_depth(uv_depth + float2(0.0f, -texel.y)));

            // the step above counts misses, so four means no neighbour was geometry
            float coverage = saturate((4.0f - neighbour) * 0.5f);
            backdrop       = lerp(backdrop, wire, coverage * WIRE_SPILL);
        }

        tex_uav[thread_id.xy] = float4(backdrop, source.a);
        return;
    }

    if (wire_mode < 0.5f)
        return;

    // the shaded value only survives as a faint tint so material identity is not lost entirely
    float3 result = lerp(wire, saturate(source.rgb), 0.15f);

    tex_uav[thread_id.xy] = float4(result, source.a);
}
