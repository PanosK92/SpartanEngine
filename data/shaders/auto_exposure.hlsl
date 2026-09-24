/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= includes =========
#include "common.hlsl"
//====================

// histogram based metering as described in lagarde and de rousiers, moving frostbite to pbr
static const uint  thread_count            = 256;
static const uint  histogram_bin_count     = 256;
static const float histogram_log2_min      = -8.0f;  // log2 nits, histogram floor
static const float histogram_log2_max      = 18.0f;  // log2 nits, histogram ceiling
static const float percentile_low          = 0.5f;   // reject the darkest half so shadows don't overbrighten the frame
static const float percentile_high         = 0.9f;   // reject the brightest tail so the sun doesn't darken the frame
static const float avg_nits_min            = 0.01f;  // adaptation floor, keeps night scenes dark
static const float avg_nits_max            = 20000.0f; // adaptation ceiling, sun drenched scene
static const float metering_edge_weight    = 0.15f;  // center weighted metering falloff at the screen edges
static const uint  metering_max_dimension  = 128;    // mip size used for metering
static const float weight_fixed_point      = 1024.0f;

groupshared uint g_histogram[histogram_bin_count];

[numthreads(16, 16, 1)]
void main_cs(uint group_index : SV_GroupIndex)
{
    g_histogram[group_index] = 0;
    GroupMemoryBarrierWithGroupSync();

    // pick a mip small enough to be cheap but large enough to preserve the luminance distribution
    uint w, h, mip_count;
    tex.GetDimensions(0, w, h, mip_count);
    uint mip = 0;
    while ((mip + 1) < mip_count && (w > metering_max_dimension || h > metering_max_dimension))
    {
        mip++;
        tex.GetDimensions(mip, w, h, mip_count);
    }

    // build a center weighted histogram of log2 luminance in nits
    uint texel_count = w * h;
    for (uint i = group_index; i < texel_count; i += thread_count)
    {
        uint x = i % w;
        uint y = i / w;

        float3 color   = tex.Load(int3(x, y, mip)).rgb;
        float lum_nits = dot(radiometric_to_photometric(color), float3(0.2126f, 0.7152f, 0.0722f));

        // photographic center weighted metering mask
        float2 ndc   = (float2(x + 0.5f, y + 0.5f) / float2(w, h)) * 2.0f - 1.0f;
        float weight = lerp(metering_edge_weight, 1.0f, exp(-dot(ndc, ndc) * 1.5f));

        // nan resolves to the histogram floor through the max below
        float log2_lum   = log2(max(lum_nits, exp2(histogram_log2_min)));
        float normalized = saturate((log2_lum - histogram_log2_min) / (histogram_log2_max - histogram_log2_min));
        uint bin         = (uint)(normalized * (histogram_bin_count - 1) + 0.5f);

        InterlockedAdd(g_histogram[bin], (uint)(weight * weight_fixed_point));
    }
    GroupMemoryBarrierWithGroupSync();

    if (group_index != 0)
    {
        return;
    }

    float total_weight = 0.0f;
    for (uint bin = 0; bin < histogram_bin_count; bin++)
    {
        total_weight += g_histogram[bin];
    }

    // average log2 luminance over the percentile band, tails are excluded from the meter
    float weight_low  = total_weight * percentile_low;
    float weight_high = total_weight * percentile_high;
    float log2_sum    = 0.0f;
    float weight_sum  = 0.0f;
    float cumulative  = 0.0f;
    for (uint bin = 0; bin < histogram_bin_count; bin++)
    {
        float bin_weight  = g_histogram[bin];
        float band_weight = max(min(cumulative + bin_weight, weight_high) - max(cumulative, weight_low), 0.0f);
        float bin_log2    = histogram_log2_min + (bin / (float)(histogram_bin_count - 1)) * (histogram_log2_max - histogram_log2_min);
        log2_sum         += bin_log2 * band_weight;
        weight_sum       += band_weight;
        cumulative       += bin_weight;
    }
    float avg_nits = exp2(log2_sum / max(weight_sum, 0.000001f));
    avg_nits       = clamp(avg_nits, avg_nits_min, avg_nits_max);

    // Meter to middle grey in scene-linear display units. A luminance-dependent
    // daytime key approached 0.7 (near display white after the output transfer),
    // so entering a shaded forest raised its midtones and bleached sunlit leaves.
    // Keep headroom for highlights; artistic bias belongs in compensation.
    float key = 0.18f;
    // Retain the low-light adaptation curve so night is not lifted to middle grey.
    float dark_t = saturate((log2(avg_nits + 1e-4f) + 2.0f) / 8.0f);
    key = lerp(0.018f, key, dark_t * dark_t);

    // exposure compensation is an artist controlled bias in stops, positive brightens
    float exposure_compensation = pass_get_f3_value().y;

    // map the metered average to the key in display units where 1 is paper white
    // Limit the meter before compensation, so +1 stop always doubles exposure,
    // including at night. 0.30 is an artistic adaptation limit (about EV 1.47).
    float target_exposure = min(key / avg_nits, 0.30f) * exp2(exposure_compensation);

    float prev_exposure = tex2.Load(int3(0, 0, 0)).r;
    if (!isfinite(prev_exposure) || prev_exposure <= 0.0f)
    {
        // start from the target to avoid a first frame flash
        prev_exposure = target_exposure;
    }

    // adapt in ev space, the eye adjusts to bright scenes faster than to dark ones
    float adaptation_speed = pass_get_f3_value().x;
    float speed            = target_exposure < prev_exposure ? adaptation_speed * 6.0f : adaptation_speed * 2.0f;
    float alpha            =
        adaptation_speed <= 0.0f ?
        1.0f :
        1.0f - exp(-speed * buffer_frame.delta_time);
    float exposure         = exp2(lerp(log2(prev_exposure), log2(target_exposure), alpha));

    tex_uav[uint2(0, 0)] = float4(exposure, exposure, exposure, 1.0f);
}
