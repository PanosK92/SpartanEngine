/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Environment.h"
#include <algorithm>
using namespace std;
using namespace spartan::math;
namespace spartan
{
namespace
{
    namespace world_weather
    {
        float puddliness = 0.0f;
    }

    namespace world_clouds
    {
        // the cloud noise tiles every 60 km horizontally, so any offset inside that span picks a different cloudscape
        constexpr float seed_span = 60000.0f;
        Vector2 seed_offset       = Vector2::Zero;

        void reroll_seed()
        {
            seed_offset = Vector2(
                random<float>(0.0f, seed_span),
                random<float>(0.0f, seed_span)
            );
        }
    }

    namespace world_wind
    {
        Vector3 wind = Vector3::Zero;

        constexpr int frequency_curl_base = 4;
        constexpr int frequency_gust      = 2;
        constexpr int frequency_micro     = 32;
        constexpr int curl_octaves        = 4;
        constexpr float curl_drift        = 0.03f;
        constexpr float gust_speed        = 0.07f;
        constexpr float micro_speed       = 0.12f;
        constexpr float life_rate         = 0.55f;
        constexpr float world_period      = 80.0f;

        uint32_t hash(uint32_t value)
        {
            value ^= value >> 16;
            value *= 0x7feb352du;
            value ^= value >> 15;
            value *= 0x846ca68bu;
            value ^= value >> 16;
            return value;
        }

        Vector2 hash(int x, int y)
        {
            uint32_t hash_0 = hash(static_cast<uint32_t>(x) * 374761393u + static_cast<uint32_t>(y) * 668265263u);
            uint32_t hash_1 = hash(hash_0 ^ 0x9e3779b9u);
            constexpr float scale = 2.0f / 4294967295.0f;
            return Vector2(static_cast<float>(hash_0) * scale - 1.0f, static_cast<float>(hash_1) * scale - 1.0f);
        }

        int wrap(int value, int period)
        {
            return ((value % period) + period) % period;
        }

        float interpolate(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        float gradient_noise(const Vector2& position, int period)
        {
            int cell_x = static_cast<int>(floorf(position.x));
            int cell_y = static_cast<int>(floorf(position.y));
            float x = position.x - static_cast<float>(cell_x);
            float y = position.y - static_cast<float>(cell_y);
            float smooth_x = x * x * x * (x * (x * 6.0f - 15.0f) + 10.0f);
            float smooth_y = y * y * y * (y * (y * 6.0f - 15.0f) + 10.0f);

            Vector2 gradient_a = hash(wrap(cell_x, period), wrap(cell_y, period));
            Vector2 gradient_b = hash(wrap(cell_x + 1, period), wrap(cell_y, period));
            Vector2 gradient_c = hash(wrap(cell_x, period), wrap(cell_y + 1, period));
            Vector2 gradient_d = hash(wrap(cell_x + 1, period), wrap(cell_y + 1, period));

            float a = gradient_a.x * x + gradient_a.y * y;
            float b = gradient_b.x * (x - 1.0f) + gradient_b.y * y;
            float c = gradient_c.x * x + gradient_c.y * (y - 1.0f);
            float d = gradient_d.x * (x - 1.0f) + gradient_d.y * (y - 1.0f);
            return interpolate(interpolate(a, b, smooth_x), interpolate(c, d, smooth_x), smooth_y);
        }

        float fbm_evolving(const Vector2& uv, float time, float phase)
        {
            static const Vector2 directions[curl_octaves] =
            {
                Vector2(0.80f, 0.60f),
                Vector2(-0.60f, 0.80f),
                Vector2(-0.80f, -0.60f),
                Vector2(0.60f, -0.80f)
            };

            float noise           = 0.0f;
            float amplitude       = 1.0f;
            float amplitude_total = 0.0f;
            int frequency         = frequency_curl_base;

            for (int i = 0; i < curl_octaves; i++)
            {
                Vector2 drift = directions[i] * (time * curl_drift * static_cast<float>(frequency));
                float life = 0.6f + 0.4f * sinf(time * life_rate + static_cast<float>(i) * 1.7f + phase);
                noise += amplitude * life * gradient_noise(uv * static_cast<float>(frequency) + drift, frequency);
                amplitude_total += amplitude;
                amplitude *= 0.55f;
                frequency *= 2;
            }

            return noise / amplitude_total;
        }

        Vector3 sample(const Vector3& position, float time)
        {
            float wind_magnitude = sqrtf(wind.x * wind.x + wind.z * wind.z);
            if (wind_magnitude <= 0.0001f)
            {
                return Vector3(0.0f, wind.y, 0.0f);
            }

            Vector2 wind_direction(wind.x / wind_magnitude, wind.z / wind_magnitude);
            Vector2 uv(position.x / world_period, position.z / world_period);
            Vector2 warp_drift_a = Vector2(0.06f, -0.09f) * time;
            Vector2 warp_drift_b = Vector2(-0.07f, 0.05f) * time;
            Vector2 warp(
                gradient_noise(uv * 2.0f + warp_drift_a, 2),
                gradient_noise(uv * 2.0f + warp_drift_b + Vector2(5.1f, 3.7f), 2)
            );
            warp *= 0.18f;

            float flow_x = fbm_evolving(uv + warp, time, 0.0f);
            float flow_y = fbm_evolving(uv - Vector2(warp.y, warp.x), time * 1.07f, 2.71f);
            Vector2 direction = wind_direction + Vector2(clamp(flow_x * 1.6f, -1.0f, 1.0f), clamp(flow_y * 1.6f, -1.0f, 1.0f)) * 0.55f;
            direction.Normalize();

            Vector2 gust_advection = wind_direction * (-time * gust_speed * static_cast<float>(frequency_gust));
            float gust_low = gradient_noise(uv * static_cast<float>(frequency_gust) + gust_advection, frequency_gust) * 0.5f + 0.5f;
            float gust_high = gradient_noise(uv * static_cast<float>(frequency_gust * 2) + gust_advection * 2.0f, frequency_gust * 2) * 0.5f + 0.5f;
            float gust = clamp(interpolate(gust_low, gust_high, 0.35f), 0.0f, 1.0f);
            float gust_t = clamp((gust - 0.42f) / (0.78f - 0.42f), 0.0f, 1.0f);
            gust = gust_t * gust_t * (3.0f - 2.0f * gust_t);

            Vector2 micro_advection = Vector2(0.31f, -0.27f) * (time * micro_speed * static_cast<float>(frequency_micro));
            float micro = gradient_noise(uv * static_cast<float>(frequency_micro) + micro_advection, frequency_micro) * 0.5f + 0.5f;
            float strength = (0.2f + 0.8f * gust) * wind_magnitude;
            return Vector3(direction.x * strength, wind.y * (0.2f + 0.8f * gust) + (micro - 0.5f) * wind_magnitude * 0.2f, direction.y * strength);
        }

        void initialize()
        {
            float rotation_y      = 120.0f * math::deg_to_rad;
            const float intensity = 8.0f;
            wind = Vector3(sin(rotation_y), 0.0f, cos(rotation_y)) * intensity;
        }
    }

}
    const Vector3& Environment::GetWind() { return world_wind::wind; }
    Vector3 Environment::SampleWind(const Vector3& position, float time) { return world_wind::sample(position, time); }
    void Environment::SetWind(const Vector3& wind)
    {
        if (isfinite(wind.x) && isfinite(wind.y) && isfinite(wind.z))
            world_wind::wind = Vector3(clamp(wind.x, -100.0f, 100.0f), clamp(wind.y, -100.0f, 100.0f), clamp(wind.z, -100.0f, 100.0f));
    }
    float Environment::GetPuddliness() { return world_weather::puddliness; }
    void Environment::SetPuddliness(float value) { if (isfinite(value)) world_weather::puddliness = clamp(value, 0.0f, 1.0f); }
    const Vector2& Environment::GetCloudSeedOffset() { return world_clouds::seed_offset; }
    void Environment::ResetSpatialWeather() { world_wind::initialize(); world_clouds::reroll_seed(); world_weather::puddliness = 0.0f; }
}
