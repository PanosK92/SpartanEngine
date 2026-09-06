/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the MIT license. See license.md in the repository root.
*/

#ifndef SPARTAN_VHS_SIGNAL
#define SPARTAN_VHS_SIGNAL

// A decoded, moderately worn NTSC VHS recording. Work in a virtual 720 x 480
// signal raster and a 59.94 Hz field clock, independent of output resolution/fps.
// This is a real-time baseband approximation, not an RF/head/tape simulation.
// References and validation: tools/vhs_tests/README.md.
static const float2 vhs_raster = float2(720.0f, 480.0f);
static const float vhs_field_rate = 60000.0f / 1001.0f;

// Supplied by the pass: samples the source in display-referred, nonlinear RGB.
float3 vhs_read_rgb(float2 uv);

uint vhs_hash(uint value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

float vhs_random(uint x, uint y, uint seed)
{
    return float(vhs_hash(x ^ vhs_hash(y + 0x9e3779b9u) ^ seed) >> 8) / 16777216.0f;
}

float vhs_signed(uint x, uint y, uint seed)
{
    return vhs_random(x, y, seed) * 2.0f - 1.0f;
}

float3 vhs_to_yiq(float3 rgb)
{
    return float3(dot(rgb, float3(0.299f, 0.587f, 0.114f)),
                  dot(rgb, float3(0.596f, -0.274f, -0.322f)),
                  dot(rgb, float3(0.211f, -0.523f, 0.312f)));
}

float3 vhs_from_yiq(float3 yiq)
{
    return float3(dot(yiq, float3(1.0f, 0.956f, 0.621f)),
                  dot(yiq, float3(1.0f, -0.272f, -0.647f)),
                  dot(yiq, float3(1.0f, -1.106f, 1.703f)));
}

float3 vhs_to_linear(float3 rgb)
{
    rgb = max(rgb, 0.0f);
    return lerp(rgb / 12.92f, pow((rgb + 0.055f) / 1.055f, 2.4f), step(0.04045f, rgb));
}

float3 vhs_to_gamma(float3 rgb)
{
    rgb = max(rgb, 0.0f);
    return lerp(rgb * 12.92f, 1.055f * pow(rgb, 1.0f / 2.4f) - 0.055f, step(0.0031308f, rgb));
}

float3 vhs_pq_to_nits(float3 rgb)
{
    float3 p = pow(saturate(rgb), 1.0f / 78.84375f);
    return 10000.0f * pow(max(p - 0.8359375f, 0.0f) / max(18.8515625f - 18.6875f * p, 0.00001f), 1.0f / 0.1593017578125f);
}

float3 vhs_nits_to_pq(float3 nits)
{
    float3 p = pow(max(nits, 0.0f) / 10000.0f, 0.1593017578125f);
    return pow((0.8359375f + 18.8515625f * p) / (1.0f + 18.6875f * p), 78.84375f);
}

// VHS is SDR. Decode HDR before filtering the video voltages, then display the
// resulting SDR recording at paper white instead of treating PQ/scRGB as RGB video.
float3 vhs_decode_display(float3 rgb, float hdr_mode, float white_nits)
{
    if (hdr_mode < 0.5f) return saturate(rgb);
    if (hdr_mode > 1.5f) return saturate(vhs_to_gamma(rgb * (80.0f / white_nits)));
    float3 c = vhs_pq_to_nits(rgb) / white_nits;
    c = mul(float3x3(1.660491f, -0.5876411f, -0.0728499f,
                    -0.1245505f, 1.1328999f, -0.0083494f,
                    -0.0181508f, -0.1005789f, 1.1187297f), c);
    return saturate(vhs_to_gamma(c));
}

float3 vhs_encode_display(float3 rgb, float hdr_mode, float white_nits)
{
    rgb = saturate(rgb);
    if (hdr_mode < 0.5f) return rgb;
    rgb = vhs_to_linear(rgb);
    if (hdr_mode > 1.5f) return rgb * (white_nits / 80.0f);
    rgb = mul(float3x3(0.627404f, 0.329282f, 0.0433136f,
                      0.069097f, 0.91954f, 0.0113612f,
                      0.0163916f, 0.0880132f, 0.895595f), rgb);
    return vhs_nits_to_pq(rgb * white_nits);
}

struct vhs_transport
{
    float offset;   // horizontal error, in 720-sample raster units
    float tracking;
    float switching;
};

vhs_transport vhs_transport_at(uint scan_line, uint field)
{
    vhs_transport tape;
    // Small field-wide servo error, correlated error across adjacent lines, and
    // fine line jitter. The held field clock gives ragged vertical edges, not jelly.
    float group = float(scan_line) / 12.0f;
    float wander = lerp(vhs_signed(uint(group), field, 17u), vhs_signed(uint(group) + 1u, field, 17u), frac(group));
    tape.offset = vhs_signed(0u, field, 3u) * 0.16f + wander * 0.38f + vhs_signed(scan_line, field, 9u) * 0.13f;

    // Brief mistracking events, separated by long stable sections. Edges are tied
    // to complete lines; the band can move between fields and release abruptly.
    uint event_id = field / 120u;
    uint event_start = 12u + uint(vhs_random(event_id, 0u, 23u) * 88.0f);
    uint age = field % 120u;
    float active = (age >= event_start && age < event_start + 5u
                    && vhs_random(event_id, 0u, 29u) < 0.38f) ? 1.0f : 0.0f;
    float center = 35.0f + vhs_random(event_id, 0u, 31u) * 380.0f + (float(age) - float(event_start)) * 9.0f;
    float distance = abs(float(scan_line) - center);
    tape.tracking = active * saturate((9.0f - distance) * 0.5f);
    tape.offset += tape.tracking * (vhs_signed(event_id, field, 37u) * 11.0f + vhs_signed(scan_line, field, 41u) * 4.0f);

    // A few exposed head-switch lines at the bottom of a full-raster capture.
    // There is no broad gradient of wobble across the lower fifth of the picture.
    uint switch_line = 474u + uint(vhs_random(0u, field / 2u, 43u) * 3.0f);
    tape.switching = scan_line >= switch_line ? 1.0f : 0.0f;
    tape.offset += tape.switching * (vhs_signed(0u, field, 47u) * 5.0f + vhs_signed(scan_line, field, 53u) * 2.0f);
    return tape;
}

float3 vhs_sample_video(float2 uv, float vertical_footprint)
{
    // A modest vertical aperture models reconstructed video lines. No CRT grille
    // or alternating black stripes: those belong to the display, not the recording.
    float2 dy = float2(0.0f, vertical_footprint * 0.5f);
    return (vhs_read_rgb(saturate(uv - dy)) + vhs_read_rgb(saturate(uv + dy))) * 0.5f;
}

float3 vhs_process(float2 uv, float2 output_resolution, float time)
{
    uint field = uint(floor(max(time, 0.0f) * vhs_field_rate));
    uint scan_line = min(uint(uv.y * vhs_raster.y), 479u);
    vhs_transport tape = vhs_transport_at(scan_line, field);
    float2 signal_uv = uv + float2(tape.offset / vhs_raster.x, 0.0f);
    // Small field registration difference; motion remains responsive. True combing
    // would require storing a previous field, which this deinterlaced path does not.
    signal_uv.y += ((field & 1u) != 0u ? 0.125f : -0.125f) / vhs_raster.y;
    float aperture = max(1.0f / vhs_raster.y, 1.0f / output_resolution.y);

    // Separate bandwidths: fine brightness structure, much coarser horizontal colour.
    // The chroma filter's delay creates colour bleed rather than splitting RGB channels.
    static const float luma_weights[5] = { 0.055f, 0.245f, 0.4f, 0.245f, 0.055f };
    float y = 0.0f;
    float center_y = 0.0f;
    [unroll]
    for (int i = -2; i <= 2; ++i)
    {
        float value = vhs_to_yiq(vhs_sample_video(signal_uv + float2(float(i) / vhs_raster.x, 0.0f), aperture)).x;
        y += value * luma_weights[i + 2];
        if (i == 0) center_y = value;
    }
    y += (center_y - y) * 0.12f; // restrained playback peaking around bright edges

    static const float chroma_weights[9] = { 0.028f, 0.066f, 0.124f, 0.18f, 0.204f, 0.18f, 0.124f, 0.066f, 0.028f };
    float2 iq = 0.0f;
    float chroma_delay = 1.6f + vhs_signed(scan_line / 4u, field, 59u) * 0.35f;
    [unroll]
    for (int j = -4; j <= 4; ++j)
    {
        float2 sample_uv = signal_uv + float2((float(j) * 3.0f - chroma_delay) / vhs_raster.x, 0.5f / vhs_raster.y);
        iq += vhs_to_yiq(vhs_sample_video(sample_uv, aperture)).yz * chroma_weights[j + 4];
    }

    uint sample_x = uint(saturate(uv.x) * vhs_raster.x);
    float grain = (vhs_signed(sample_x, scan_line, field * 61u) + vhs_signed(sample_x, scan_line, field * 67u + 1u)) * 0.5f;
    y += grain * (0.017f + (1.0f - y) * 0.007f) + vhs_signed(scan_line, field, 71u) * 0.002f;
    // Low-bandwidth colour noise, correlated along a line rather than RGB snow.
    float chroma_x = uv.x * 120.0f;
    uint cx = uint(chroma_x);
    float2 noise_a = float2(vhs_signed(cx, scan_line / 2u, field * 73u), vhs_signed(cx, scan_line / 2u, field * 79u));
    float2 noise_b = float2(vhs_signed(cx + 1u, scan_line / 2u, field * 73u), vhs_signed(cx + 1u, scan_line / 2u, field * 79u));
    iq += lerp(noise_a, noise_b, frac(chroma_x)) * 0.007f;
    float phase = vhs_signed(scan_line / 8u, field, 83u) * 0.018f;
    iq = float2(iq.x - iq.y * phase, iq.y + iq.x * phase) * 0.97f;

    // Loss of RF affects a short stretch of one line. Most of it is concealed with
    // a neighbouring line; failed compensation leaves a thin bright/dark streak.
    float dropout = vhs_random(scan_line, field, 89u) < 0.0022f ? 1.0f : 0.0f;
    float dropout_start = vhs_random(scan_line, field, 97u) * 700.0f;
    float dropout_length = 4.0f + vhs_random(scan_line, field, 101u) * 36.0f;
    float x = uv.x * vhs_raster.x;
    dropout *= step(dropout_start, x) * (1.0f - step(dropout_start + dropout_length, x));
    if (dropout > 0.0f)
    {
        float previous_line = vhs_to_yiq(vhs_read_rgb(saturate(signal_uv - float2(0.0f, 2.0f / vhs_raster.y)))).x;
        float lost_signal = vhs_random(sample_x, scan_line, field * 103u);
        y = lerp(previous_line, lost_signal, 0.65f);
        iq *= 0.15f;
    }

    float damage = max(tape.tracking, tape.switching * 0.7f);
    float rf_noise = vhs_random(sample_x, scan_line, field * 107u);
    y = lerp(y, y * 0.45f + rf_noise * 0.45f, damage);
    iq *= 1.0f - damage * 0.85f;
    // Displaced active picture exposes blanking at the edge instead of smearing
    // the last pixel outward. No vignette, sepia grade, RGB split, or screen curvature.
    float picture = step(0.0f, signal_uv.x) * step(signal_uv.x, 1.0f);
    return saturate(vhs_from_yiq(float3(y, iq))) * picture;
}
#endif
