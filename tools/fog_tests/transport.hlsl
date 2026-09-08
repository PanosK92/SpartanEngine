#include "../../data/shaders/shared_fog.h"
RWStructuredBuffer<float4> results : register(u0);
[numthreads(64, 1, 1)]
void main_cs(uint3 id : SV_DispatchThreadID)
{
    uint i = id.x;
    if (i >= 12288u) return;
    if (i >= 8192u)
    {
        uint test = i - 8192u;
        float sigma = exp2(-18.0f + float(test % 257u) * 0.09f);
        float distance = exp2(float(test % 191u) * 0.08f - 4.0f);
        // Adjacent perspective columns must meet a horizontal interface at the
        // same height even when their radial distances differ substantially.
        float tap_y = -0.021f - float(test % 113u) * 0.007f;
        float adjusted = fog_reproject_height_distance(distance, -0.3f, tap_y);
        results[i] = float4(sigma, distance, fog_segment_centroid(sigma, distance), adjusted * tap_y);
        return;
    }
    if (i >= 4096u)
    {
        uint test = i - 4096u;
        float length = exp2(float(test % 257u) * 0.05f - 4.0f);
        float water_fraction = float(test % 31u) / 30.0f;
        float partial = length * float(test % 17u) / 16.0f;
        bool water_first = (test & 1u) != 0u;
        float weight = fog_layered_weight(0.0004f, 0.2025f, 0.00038f, 0.008f,
            length, water_fraction, water_first, partial);
        float transmission = fog_layered_transmittance(0.0004f, 0.2025f,
            length, water_fraction, water_first, partial);
        results[i] = float4(length, partial, weight, transmission);
        return;
    }
    float u = float(i) / 4095.0f;
    float d = fog_slice_to_distance(u);
    float sigma = i == 0u ? 0.0f : exp2(-24.0f + float(i % 257u) * 0.12f);
    float integral = 0.0f;
    float transmittance = 1.0f;
    // Production integration math over the actual nonlinear grid, including
    // optically thick distant slices and optically thin near-camera cells.
    for (uint z = 0u; z < fog_depth; z++)
    {
        float start = fog_slice_to_distance(float(z) / float(fog_depth));
        float end = min(d, fog_slice_to_distance(float(z + 1u) / float(fog_depth)));
        float dt = max(end - start, 0.0f);
        integral += transmittance * fog_segment_weight(sigma, dt);
        transmittance *= exp(-sigma * dt);
    }
    results[i] = float4(d, fog_distance_to_slice(d), integral, transmittance);
}
