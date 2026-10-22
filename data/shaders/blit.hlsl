/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =========
#include "common.hlsl"
//====================

// box filter a source footprint of taps x taps texels around uv, a single bilinear tap aliases hard
// once the source is several times larger than the destination
float4 sample_footprint(Texture2D source, float2 uv, float2 texel_footprint, int2 taps)
{
    float4 sum = 0.0f;
    [loop] for (int y = 0; y < taps.y; y++)
    {
        [loop] for (int x = 0; x < taps.x; x++)
        {
            const float2 offset = (float2(x, y) + 0.5f) / (float2)taps - 0.5f;
            sum += source.SampleLevel(GET_SAMPLER(sampler_bilinear_clamp), uv + offset * texel_footprint, 0);
        }
    }

    return sum / (float)(taps.x * taps.y);
}

#if defined(XR_MIRROR)

// desktop mirror of the hmd, left eye in tex and right eye in tex2, side by side and letterboxed
// the output texture is stretched over display_size, so the layout is done in display pixels
[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 size_out;
    tex_uav.GetDimensions(size_out.x, size_out.y);
    if (any(thread_id.xy >= size_out))
        return;

    uint2 size_eye;
    tex.GetDimensions(size_eye.x, size_eye.y);

    const float2 display    = max(pass_float2(pass_xr_mirror::display_size), float2(1.0f, 1.0f));
    const float2 to_display = display / (float2)size_out;
    const float2 position   = ((float2)thread_id.xy + 0.5f) * to_display;

    // fit each eye into half the display without changing its aspect
    const float half_width = display.x * 0.5f;
    const float eye_aspect = (float)size_eye.x / (float)size_eye.y;
    float2 eye_size        = float2(half_width, half_width / eye_aspect);
    if (eye_size.y > display.y)
    {
        eye_size = float2(display.y * eye_aspect, display.y);
    }

    const uint eye       = position.x >= half_width ? 1 : 0;
    const float2 origin  = float2(eye * half_width + (half_width - eye_size.x) * 0.5f, (display.y - eye_size.y) * 0.5f);
    const float2 uv      = (position - origin) / eye_size;
    if (any(uv < 0.0f) || any(uv > 1.0f))
    {
        tex_uav[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
        return;
    }

    // one output texel covers this much of the eye, in uv and in eye texels
    const float2 uv_footprint = to_display / eye_size;
    const int2 taps           = clamp((int2)ceil(uv_footprint * (float2)size_eye), int2(1, 1), int2(4, 4));

    // the eyes are linear sdr, frame_output holds whatever the desktop swapchain expects (srgb, hdr10 or scrgb)
    const float4 color       = eye == 0 ? sample_footprint(tex, uv, uv_footprint, taps) : sample_footprint(tex2, uv, uv_footprint, taps);
    const float3 color_srgb  = linear_to_srgb(saturate(color.rgb));
    const float  white_nits  = buffer_frame.hdr_sdr_white_nits > 0.0f ? buffer_frame.hdr_sdr_white_nits : 203.0f;
    const float  hdr_mode    = pass_bool(pass_xr_mirror::encode_srgb) ? 0.0f : buffer_frame.hdr_enabled;
    tex_uav[thread_id.xy]    = float4(ui_to_display(color_srgb, hdr_mode, white_nits), 1.0f);
}

#else

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint2 size_out;
    tex_uav.GetDimensions(size_out.x, size_out.y);
    if (any(thread_id.xy >= size_out))
        return;

    uint2 size_in;
    tex.GetDimensions(size_in.x, size_in.y);

    // a same size blit stays an exact texel copy, depth to float relies on it
    if (all(size_in == size_out))
    {
        tex_uav[thread_id.xy] = tex.Load(int3(thread_id.xy, 0));
        return;
    }

    // differing sizes are a rescale, loading by texel would crop to the top left
    const float2 uv    = (thread_id.xy + 0.5f) / (float2)size_out;
    const float2 ratio = (float2)size_in / (float2)size_out;
    const int2 taps    = clamp((int2)ceil(ratio), int2(1, 1), int2(4, 4));

    tex_uav[thread_id.xy] = sample_footprint(tex, uv, 1.0f / (float2)size_out, taps);
}

#endif
