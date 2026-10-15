/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

// sequencer fades and letterbox bars, the last thing drawn over the display encoded frame

#include "common.hlsl"
#include "vhs_signal.hlsl"

[numthreads(THREAD_GROUP_COUNT_X, THREAD_GROUP_COUNT_Y, 1)]
void main_cs(uint3 thread_id : SV_DispatchThreadID)
{
    uint width, height;
    tex_uav.GetDimensions(width, height);
    if (thread_id.x >= width || thread_id.y >= height)
        return;

    float4 color = tex[thread_id.xy];

    // bars keep the full width and crop the height down to the target aspect
    float aspect = pass_float(pass_cinematic::letterbox);
    if (aspect > 0.0f)
    {
        float visible_height = float(width) / aspect;
        float bar            = max(0.0f, (float(height) - visible_height) * 0.5f);
        if (float(thread_id.y) < bar || float(thread_id.y) >= float(height) - bar)
        {
            tex_uav[thread_id.xy] = float4(0.0f, 0.0f, 0.0f, 1.0f);
            return;
        }
    }

    float fade = saturate(pass_float(pass_cinematic::fade));
    if (fade > 0.0f)
    {
        // blend in display gamma space so a fade looks the same in sdr, hdr10 and scrgb
        float hdr_mode   = pass_bool(pass_cinematic::force_sdr) ? 0.0f : buffer_frame.hdr_enabled;
        float white_nits = buffer_frame.hdr_sdr_white_nits > 0.0f ? buffer_frame.hdr_sdr_white_nits : 203.0f;
        float3 target    = saturate(pass_float3(pass_cinematic::fade_color));
        float3 decoded   = vhs_decode_display(color.rgb, hdr_mode, white_nits);
        float3 faded     = vhs_encode_display(lerp(decoded, target, fade), hdr_mode, white_nits);
        // keep hdr highlights untouched at the very start of a fade instead of clipping them on the first frame
        color.rgb        = lerp(color.rgb, faded, saturate(fade * 8.0f));
    }

    tex_uav[thread_id.xy] = float4(color.rgb, 1.0f);
}
