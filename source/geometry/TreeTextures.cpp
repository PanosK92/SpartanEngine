/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =============================
#include "pch.h"
#include "TreeTextures.h"
#include "TreeGenerator.h"
#include "../core/ThreadPool.h"
#include "../file_system/FileSystem.h"
#include "../resource/import/ImageImporter.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
//========================================

using namespace std;
using namespace spartan::math;

namespace spartan::trees
{
    namespace
    {
        constexpr float pi = 3.14159265358979f;
        constexpr const char* texture_version_tag = "v3";

        // integer hash noise, periodic so every map tiles
        uint32_t hash3(int32_t x, int32_t y, uint32_t seed)
        {
            uint32_t h = static_cast<uint32_t>(x) * 0x8da6b343u ^ static_cast<uint32_t>(y) * 0xd8163841u ^ seed * 0xcb1ab31fu;
            h ^= h >> 13;
            h *= 0x5bd1e995u;
            h ^= h >> 15;
            return h;
        }

        float hash_unit(int32_t x, int32_t y, uint32_t seed)
        {
            return static_cast<float>(hash3(x, y, seed) & 0xffffff) / 16777215.0f;
        }

        int32_t wrap(const int32_t value, const int32_t period)
        {
            const int32_t r = value % period;
            return r < 0 ? r + period : r;
        }

        float smooth(const float t)
        {
            return t * t * (3.0f - 2.0f * t);
        }

        // value noise on a lattice that repeats every period cells
        float value_noise(const float x, const float y, const int32_t px, const int32_t py, const uint32_t seed)
        {
            const int32_t x0 = static_cast<int32_t>(floor(x));
            const int32_t y0 = static_cast<int32_t>(floor(y));
            const float fx   = smooth(x - static_cast<float>(x0));
            const float fy   = smooth(y - static_cast<float>(y0));
            const float a    = hash_unit(wrap(x0, px), wrap(y0, py), seed);
            const float b    = hash_unit(wrap(x0 + 1, px), wrap(y0, py), seed);
            const float c    = hash_unit(wrap(x0, px), wrap(y0 + 1, py), seed);
            const float d    = hash_unit(wrap(x0 + 1, px), wrap(y0 + 1, py), seed);
            return (a + (b - a) * fx) + ((c + (d - c) * fx) - (a + (b - a) * fx)) * fy;
        }

        // fbm over u, v in [0, 1), the base frequencies are whole numbers so the octaves tile too
        float fbm(const float u, const float v, const int32_t fu, const int32_t fv, const uint32_t octaves, const uint32_t seed)
        {
            float sum       = 0.0f;
            float amplitude = 0.5f;
            float norm      = 0.0f;
            int32_t mu      = fu;
            int32_t mv      = fv;
            for (uint32_t i = 0; i < octaves; i++)
            {
                sum  += amplitude * value_noise(u * mu, v * mv, mu, mv, seed + i * 101u);
                norm += amplitude;
                amplitude *= 0.5f;
                mu *= 2;
                mv *= 2;
            }
            return sum / norm;
        }

        struct cell
        {
            float f1    = 1e9f;
            float f2    = 1e9f;
            uint32_t id = 0;
        };

        // worley on a periodic grid, f2 - f1 is the crack between plates
        cell worley(const float u, const float v, const int32_t fu, const int32_t fv, const uint32_t seed, const float aspect = 1.0f)
        {
            const float x    = u * fu;
            const float y    = v * fv;
            const int32_t cx = static_cast<int32_t>(floor(x));
            const int32_t cy = static_cast<int32_t>(floor(y));
            cell result;
            for (int32_t oy = -1; oy <= 1; oy++)
            {
                for (int32_t ox = -1; ox <= 1; ox++)
                {
                    const int32_t gx = cx + ox;
                    const int32_t gy = cy + oy;
                    const int32_t wx = wrap(gx, fu);
                    const int32_t wy = wrap(gy, fv);
                    const float jx   = hash_unit(wx, wy, seed);
                    const float jy   = hash_unit(wx, wy, seed + 7u);
                    const float dx   = (static_cast<float>(gx) + jx - x);
                    const float dy   = (static_cast<float>(gy) + jy - y) * aspect;
                    const float d    = sqrt(dx * dx + dy * dy);
                    if (d < result.f1)
                    {
                        result.f2 = result.f1;
                        result.f1 = d;
                        result.id = hash3(wx, wy, seed + 13u);
                    }
                    else if (d < result.f2)
                    {
                        result.f2 = d;
                    }
                }
            }
            return result;
        }

        float saturate(const float v)
        {
            return clamp(v, 0.0f, 1.0f);
        }

        float smoothstep(const float a, const float b, const float x)
        {
            const float t = saturate((x - a) / max(b - a, 1e-6f));
            return t * t * (3.0f - 2.0f * t);
        }

        uint8_t to_srgb8(const float linear)
        {
            const float c    = saturate(linear);
            const float srgb = c <= 0.0031308f ? c * 12.92f : 1.055f * pow(c, 1.0f / 2.4f) - 0.055f;
            return static_cast<uint8_t>(round(srgb * 255.0f));
        }

        uint8_t to_unorm8(const float value)
        {
            return static_cast<uint8_t>(round(saturate(value) * 255.0f));
        }

        Vector3 mix(const Vector3& a, const Vector3& b, const float t)
        {
            return a + (b - a) * t;
        }

        // normals from a height field with wrap around, strength is height units per texel width
        void height_to_normal(const vector<float>& height, const uint32_t size, const float strength, vector<uint8_t>& output)
        {
            output.resize(static_cast<size_t>(size) * size * 4);
            ThreadPool::ParallelLoop([&](const uint32_t y_begin, const uint32_t y_end)
            {
                auto h = [&](const int32_t ix, const int32_t iy)
                {
                    return height[static_cast<size_t>(wrap(iy, static_cast<int32_t>(size))) * size + wrap(ix, static_cast<int32_t>(size))];
                };
                for (uint32_t y = y_begin; y < y_end; y++)
                {
                    for (uint32_t x = 0; x < size; x++)
                    {
                        const int32_t ix = static_cast<int32_t>(x);
                        const int32_t iy = static_cast<int32_t>(y);
                        // Sobel filter for crisp, artifact-free multi-scale bark relief
                        const float tl = h(ix - 1, iy - 1);
                        const float t  = h(ix,     iy - 1);
                        const float tr = h(ix + 1, iy - 1);
                        const float l  = h(ix - 1, iy);
                        const float r  = h(ix + 1, iy);
                        const float bl = h(ix - 1, iy + 1);
                        const float b  = h(ix,     iy + 1);
                        const float br = h(ix + 1, iy + 1);
                        const float dx = ((tr + 2.0f * r + br) - (tl + 2.0f * l + bl)) * 0.25f * strength;
                        const float dy = ((bl + 2.0f * b + br) - (tl + 2.0f * t + tr)) * 0.25f * strength;
                        const Vector3 n = Vector3(-dx, dy, 1.0f).Normalized();
                        uint8_t* p = &output[(static_cast<size_t>(y) * size + x) * 4];
                        p[0] = to_unorm8(n.x * 0.5f + 0.5f);
                        p[1] = to_unorm8(n.y * 0.5f + 0.5f);
                        p[2] = to_unorm8(n.z * 0.5f + 0.5f);
                        p[3] = 255;
                    }
                }
            }, size);
        }

        void paint_bark(const species& s, const uint32_t size, vector<uint8_t>& albedo, vector<uint8_t>& normal, vector<uint8_t>& roughness, vector<uint8_t>& occlusion)
        {
            const uint32_t seed = static_cast<uint32_t>(texture_hash(s, season::summer) & 0xffffffffu);
            vector<float> height(static_cast<size_t>(size) * size, 0.0f);
            albedo.resize(static_cast<size_t>(size) * size * 4);
            roughness.resize(albedo.size());
            occlusion.resize(albedo.size());
            const Vector3 light = s.bark_color;
            const Vector3 dark  = s.bark_color_dark;
            float strength      = 5.0f;
            switch (s.bark)
            {
                case bark_style::furrowed: strength = 8.0f; break;
                case bark_style::plated:   strength = 8.5f; break;
                case bark_style::smooth:   strength = 2.0f; break;
                case bark_style::birch:    strength = 3.0f; break;
                case bark_style::peeling:  strength = 3.2f; break;
                case bark_style::fibrous:  strength = 7.5f; break;
                case bark_style::twisted:  strength = 9.5f; break;
                case bark_style::ringed:   strength = 4.5f; break;
            }

            ThreadPool::ParallelLoop([&](const uint32_t y_begin, const uint32_t y_end)
            {
                for (uint32_t y = y_begin; y < y_end; y++)
                {
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                    for (uint32_t x = 0; x < size; x++)
                    {
                        const float u    = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                        const float fine = fbm(u, v, 48, 48, 3, seed + 900u);
                        const float micro = fbm(u, v, 160, 120, 2, seed + 941u);
                        float h          = 0.5f;
                        float ao         = 0.85f;
                        float rough_bias = 0.0f;
                        Vector3 color    = light;

                        switch (s.bark)
                        {
                            case bark_style::furrowed:
                            {
                                // Deep gnarled dark-umber Mediterranean carob/oak bark: heavily warped vertical ridges broken by ragged horizontal plate checks (zero polygonal tiles)
                                const float warp_u  = (fbm(u, v, 5, 2, 4, seed + 5u) - 0.5f) * 0.085f + (fbm(u, v, 14, 5, 3, seed + 13u) - 0.5f) * 0.025f;
                                const float warp_v  = (fbm(u, v, 4, 4, 3, seed + 19u) - 0.5f) * 0.035f;
                                const float gu      = u + warp_u;
                                const float gv      = v + warp_v;
                                const float n1      = fbm(gu, gv, 18, 3, 5, seed);
                                const float n2      = fbm(gu, gv, 34, 7, 4, seed + 31u);
                                const float ridge1  = 1.0f - abs(n1 * 2.0f - 1.0f);
                                const float ridge2  = 1.0f - abs(n2 * 2.0f - 1.0f);
                                const float furrow  = smoothstep(0.18f, 0.78f, pow(saturate(ridge1 * 0.68f + ridge2 * 0.32f), 1.45f));
                                // Ragged natural horizontal bark scale checks (driven purely by multi-octave noise, no geometric grids)
                                const float checks  = 1.0f - smoothstep(0.08f, 0.24f, abs(fbm(gu, gv, 16, 26, 3, seed + 59u) - 0.5f));
                                const float fibers  = fbm(gu, v, 96, 16, 3, seed + 73u);
                                const float chunks  = fbm(gu, gv, 24, 14, 3, seed + 109u);
                                h     = saturate(furrow * 0.64f * (1.0f - checks * 0.38f) + fibers * 0.20f + micro * 0.12f + chunks * 0.08f);
                                ao    = clamp(0.20f + 0.80f * smoothstep(0.10f, 0.68f, h * (1.0f - checks * 0.28f)), 0.18f, 1.0f);
                                const Vector3 weather_cap = mix(light, Vector3(0.20f, 0.185f, 0.165f), 0.38f);
                                color = mix(dark * 0.68f, mix(light, weather_cap, chunks * 0.65f), smoothstep(0.14f, 0.74f, h));
                                color = color * (0.68f + 0.38f * fibers + 0.26f * micro);
                                color = mix(color, dark * 0.55f, checks * 0.42f * furrow);
                                const float lichen = smoothstep(0.70f, 0.88f, fbm(u, v, 9, 9, 3, seed + 211u)) * smoothstep(0.45f, 0.75f, h);
                                color = mix(color, Vector3(0.16f, 0.17f, 0.125f), lichen * 0.24f);
                                rough_bias = (1.0f - h) * 0.14f - lichen * 0.04f;
                                break;
                            }
                            case bark_style::plated:
                            {
                                // Aleppo pine (Pinus halepensis): crisp multi-scale vertical silver-ash bark plates over deep warm cinnamon-terracotta fissures
                                const float warp_u   = (fbm(u, v, 6, 3, 4, seed + 3u) - 0.5f) * 0.065f + (fbm(u, v, 18, 6, 3, seed + 29u) - 0.5f) * 0.018f;
                                const float warp_v   = (fbm(u, v, 5, 5, 2, seed + 41u) - 0.5f) * 0.025f;
                                const float grain_u  = u + warp_u;
                                const float grain_v  = v + warp_v;
                                // Tight vertical fissured ridges (high horizontal frequency so bark reads as authentic scaly pine plates, not large camo blobs)
                                const float r_coarse = fbm(grain_u, grain_v, 22, 4, 5, seed + 17u);
                                const float r_detail = fbm(grain_u, grain_v, 42, 9, 4, seed + 53u);
                                const float crag     = r_coarse * 0.65f + r_detail * 0.35f;
                                const float deep_fis = 1.0f - smoothstep(0.28f, 0.46f, crag);
                                const float mid_fis  = 1.0f - smoothstep(0.40f, 0.58f, crag);
                                // Overlapping scaly plate flakes & natural ragged horizontal plate fractures (noise-driven, zero wavy sine hairlines)
                                const float plates   = fbm(grain_u, grain_v, 36, 18, 4, seed + 67u);
                                const float flakes   = smoothstep(0.44f, 0.68f, plates);
                                const float cracks   = (1.0f - smoothstep(0.06f, 0.19f, abs(fbm(grain_u, grain_v, 18, 22, 3, seed + 83u) - 0.5f))) * (1.0f - mid_fis);
                                const float fibers   = fbm(grain_u, v, 112, 16, 3, seed + 91u);
                                h  = saturate((1.0f - deep_fis) * 0.52f + flakes * 0.22f + fibers * 0.16f + micro * 0.12f - cracks * 0.22f);
                                ao = clamp(0.18f + 0.82f * smoothstep(0.10f, 0.64f, h) * (1.0f - cracks * 0.32f), 0.16f, 1.0f);

                                // Warm reddish-cinnamon inner bark in deep fissure walls, weathered silver-ash flaky outer plates
                                const Vector3 cinnamon = mix(dark * 1.45f, Vector3(0.155f, 0.072f, 0.038f), 0.62f);
                                const Vector3 silver   = mix(light, Vector3(0.245f, 0.232f, 0.212f), 0.48f);
                                Vector3 base = mix(dark * 0.60f, cinnamon, smoothstep(0.08f, 0.38f, 1.0f - deep_fis));
                                base = mix(base, light, smoothstep(0.32f, 0.60f, 1.0f - mid_fis));
                                base = mix(base, silver, flakes * (1.0f - mid_fis) * 0.58f);
                                color = base * (0.64f + fibers * 0.42f + micro * 0.28f);
                                color = mix(color, dark * 0.52f, cracks * 0.50f);
                                const float lichen = smoothstep(0.74f, 0.90f, fbm(u, v, 11, 11, 3, seed + 307u)) * (1.0f - mid_fis);
                                color = mix(color, Vector3(0.20f, 0.21f, 0.16f), lichen * 0.24f);
                                rough_bias = deep_fis * 0.14f - flakes * 0.06f;
                                break;
                            }
                            case bark_style::smooth:
                            {
                                const float mottle   = fbm(u, v, 3, 3, 5, seed);
                                const float lenticel = value_noise(u * 6.0f, v * 64.0f, 6, 64, seed + 11u);
                                const float dash     = smoothstep(0.82f, 0.9f, lenticel) * smoothstep(0.4f, 0.6f, fbm(u, v, 24, 4, 2, seed + 12u));
                                h     = 0.52f + (mottle - 0.5f) * 0.24f - dash * 0.26f + (micro - 0.5f) * 0.05f;
                                ao    = clamp(0.65f + 0.35f * smoothstep(0.25f, 0.70f, h), 0.55f, 1.0f);
                                color = mix(light, dark, saturate((mottle - 0.45f) * 0.9f)) * (0.90f + 0.18f * fine);
                                color = mix(color, dark * 0.8f, dash * 0.72f);
                                break;
                            }
                            case bark_style::birch:
                            {
                                const float lenticel = value_noise(u * 5.0f, v * 48.0f, 5, 48, seed + 11u);
                                const float dash     = smoothstep(0.78f, 0.86f, lenticel) * smoothstep(0.35f, 0.65f, fbm(u, v, 20, 3, 2, seed + 12u));
                                const float scar     = smoothstep(0.7f, 0.78f, fbm(u, v, 3, 2, 5, seed + 40u));
                                const float peel     = fbm(u, v, 6, 10, 4, seed + 50u);
                                h     = 0.6f - dash * 0.4f - scar * 0.5f + (peel - 0.5f) * 0.12f;
                                ao    = clamp(0.35f + 0.65f * smoothstep(0.15f, 0.65f, h), 0.30f, 1.0f);
                                color = light * (0.9f + 0.15f * peel);
                                color = mix(color, dark, saturate(dash + scar));
                                break;
                            }
                            case bark_style::peeling:
                            {
                                const cell patch   = worley(u, v, 4, 3, seed, 0.9f);
                                const float tone   = static_cast<float>(patch.id & 1023) / 1023.0f;
                                const float edge   = smoothstep(0.0f, 0.06f, patch.f2 - patch.f1);
                                const Vector3 pale = mix(light, Vector3(0.58f, 0.56f, 0.45f), 0.5f);
                                const Vector3 tint = tone < 0.33f ? dark * 1.3f : (tone < 0.66f ? light : pale);
                                h     = 0.45f + 0.18f * edge + (fine - 0.5f) * 0.1f;
                                ao    = clamp(0.50f + 0.50f * edge, 0.45f, 1.0f);
                                color = mix(dark, tint, 0.35f + 0.65f * edge) * (0.90f + 0.18f * fine);
                                break;
                            }
                            case bark_style::fibrous:
                            {
                                // Mediterranean cypress: tightly interlaced vertical grey-brown fibrous strips with dark recessed seams
                                const float warp   = (fbm(u, v, 4, 2, 3, seed + 9u) - 0.5f) * 0.085f;
                                const float strand = fbm(u + warp, v, 28, 2, 5, seed);
                                const float ridge  = 1.0f - abs(strand * 2.0f - 1.0f);
                                const float groove = smoothstep(0.16f, 0.68f, pow(saturate(ridge), 1.35f));
                                const float shred  = fbm(u + warp * 0.5f, v, 84, 8, 3, seed + 47u);
                                h     = saturate(groove * 0.60f + shred * 0.28f + micro * 0.12f);
                                ao    = clamp(0.28f + 0.72f * smoothstep(0.15f, 0.72f, h), 0.25f, 1.0f);
                                const Vector3 inner_warm = mix(dark, Vector3(0.13f, 0.072f, 0.044f), 0.45f);
                                const Vector3 ash_crest  = mix(light, Vector3(0.21f, 0.19f, 0.165f), 0.42f);
                                color = mix(inner_warm, light, smoothstep(0.18f, 0.62f, h));
                                color = mix(color, ash_crest, smoothstep(0.62f, 0.90f, h) * 0.55f);
                                color = color * (0.76f + 0.34f * shred + 0.18f * micro);
                                rough_bias = (1.0f - groove) * 0.12f;
                                break;
                            }
                            case bark_style::twisted:
                            {
                                // Ancient Zakynthos olive: deeply fluted sinewy silver-ash ridges with dark charcoal crevices and knots
                                const float warp    = (fbm(u, v, 4, 2, 4, seed + 7u) - 0.5f) * 0.07f;
                                const float sinew   = fbm(u + warp, v, 26, 3, 5, seed);
                                const float ridge   = smoothstep(0.20f, 0.68f, sinew);
                                const float crack_n = fbm(u + warp * 0.6f, v, 52, 6, 4, seed + 43u);
                                const float crack   = 1.0f - smoothstep(0.20f, 0.34f, crack_n);
                                const float weather = fbm(u, v, 10, 6, 4, seed + 60u);
                                const float fibers  = fbm(u + warp * 0.3f, v, 110, 11, 3, seed + 85u);
                                h  = saturate(ridge * 0.56f + fibers * 0.20f + weather * 0.14f + micro * 0.10f - crack * 0.28f);
                                ao = clamp(0.20f + 0.80f * smoothstep(0.10f, 0.65f, h) * (1.0f - crack * 0.42f), 0.18f, 1.0f);

                                const Vector3 bleached = Vector3(0.30f, 0.29f, 0.265f);
                                color = mix(dark * 0.72f, light, smoothstep(0.14f, 0.68f, h));
                                color = mix(color, bleached, smoothstep(0.50f, 0.84f, weather * ridge) * 0.45f);
                                color = color * (0.64f + fibers * 0.46f + micro * 0.26f);
                                color = mix(color, dark * 0.50f, crack * 0.62f);
                                const float lichen = smoothstep(0.70f, 0.88f, fbm(u, v, 12, 12, 3, seed + 251u)) * ridge;
                                color = mix(color, Vector3(0.20f, 0.21f, 0.14f), lichen * 0.24f);
                                rough_bias = crack * 0.16f - ridge * 0.06f;
                                break;
                            }
                            case bark_style::ringed:
                            {
                                const float ring_pos = fmod(v * 3.0f + (fbm(u, v, 2, 1, 2, seed + 4u) - 0.5f) * 0.08f, 1.0f);
                                const float ring     = smoothstep(0.0f, 0.03f, ring_pos) * (1.0f - smoothstep(0.93f, 1.0f, ring_pos));
                                const float streak   = fbm(u, v, 18, 2, 3, seed);
                                h     = 0.4f + 0.4f * ring + (streak - 0.5f) * 0.15f;
                                ao    = clamp(0.45f + 0.55f * ring, 0.40f, 1.0f);
                                color = mix(dark, light, 0.35f + 0.65f * ring) * (0.9f + 0.2f * streak);
                                break;
                            }
                        }

                        const size_t idx = static_cast<size_t>(y) * size + x;
                        height[idx] = h;
                        uint8_t* p  = &albedo[idx * 4];
                        p[0] = to_srgb8(color.x);
                        p[1] = to_srgb8(color.y);
                        p[2] = to_srgb8(color.z);
                        p[3] = 255;

                        const uint8_t r = to_unorm8(clamp(s.bark_roughness + (0.5f - h) * 0.16f + (fine - 0.5f) * 0.10f + rough_bias, 0.58f, 0.98f));
                        uint8_t* rp = &roughness[idx * 4];
                        rp[0] = rp[1] = rp[2] = r;
                        rp[3] = 255;

                        const uint8_t o = to_unorm8(ao);
                        uint8_t* op = &occlusion[idx * 4];
                        op[0] = op[1] = op[2] = o;
                        op[3] = 255;
                    }
                }
            }, size);

            height_to_normal(height, size, strength * static_cast<float>(size) / 512.0f, normal);
        }

        // the twig atlas, painted in physical units so leaves keep their proportions on a card that is not square
        struct atlas
        {
            uint32_t size      = 2048;
            uint32_t tile_size = 1024;
            vector<float> color;     // linear rgb
            vector<float> alpha;
            vector<float> normal;    // xy, z is derived
            vector<float> roughness; // linear [0, 1]
            vector<float> occlusion; // linear [0, 1]
        };

        struct tile_frame
        {
            uint32_t ox    = 0;
            uint32_t oy    = 0;
            float width_m  = 0.4f; // physical width of the tile, x runs from -width/2 to width/2
            float height_m = 0.5f; // physical height, y runs from 0 at the twig base to height at the top
        };

        struct texel_sample
        {
            Vector3 color;
            Vector2 normal;
            float roughness = 0.65f;
            float occlusion = 0.9f;
        };

        // runs fn over the pixels whose centres fall in the physical box, fn returns coverage and fills pbr properties
        void raster(atlas& a, const tile_frame& frame, Vector2 lo, Vector2 hi, const function<float(const Vector2&, float, texel_sample&)>& fn)
        {
            const float ts      = static_cast<float>(a.tile_size);
            const float px_x    = frame.width_m / ts;
            const float px_y    = frame.height_m / ts;
            const float pixel_m = min(px_x, px_y);
            int32_t x0 = static_cast<int32_t>(floor((lo.x / frame.width_m + 0.5f) * ts)) - 1;
            int32_t x1 = static_cast<int32_t>(ceil((hi.x / frame.width_m + 0.5f) * ts)) + 1;
            int32_t y0 = static_cast<int32_t>(floor((1.0f - hi.y / frame.height_m) * ts)) - 1;
            int32_t y1 = static_cast<int32_t>(ceil((1.0f - lo.y / frame.height_m) * ts)) + 1;
            // a four texel gutter at 2k keeps mips from pulling the neighbour tile in
            const int32_t gutter = max(2, static_cast<int32_t>(a.tile_size / 256u));
            x0 = max(x0, gutter);
            y0 = max(y0, gutter);
            x1 = min(x1, static_cast<int32_t>(a.tile_size) - gutter);
            y1 = min(y1, static_cast<int32_t>(a.tile_size) - gutter);
            for (int32_t y = y0; y < y1; y++)
            {
                for (int32_t x = x0; x < x1; x++)
                {
                    const Vector2 p(((static_cast<float>(x) + 0.5f) / ts - 0.5f) * frame.width_m, (1.0f - (static_cast<float>(y) + 0.5f) / ts) * frame.height_m);
                    texel_sample sample;
                    const float coverage = fn(p, pixel_m, sample);
                    if (coverage <= 0.0f)
                    {
                        continue;
                    }
                    const size_t index = static_cast<size_t>(frame.oy + y) * a.size + frame.ox + x;
                    const float c      = saturate(coverage);
                    const float prev_a = a.alpha[index];
                    // Proper Porter-Duff over-blending for color/normal/pbr when painting back-to-front
                    const float out_a  = c + prev_a * (1.0f - c);
                    const float w_new  = out_a > 1e-6f ? c / out_a : 1.0f;
                    a.color[index * 3 + 0]  = a.color[index * 3 + 0] * (1.0f - w_new) + sample.color.x * w_new;
                    a.color[index * 3 + 1]  = a.color[index * 3 + 1] * (1.0f - w_new) + sample.color.y * w_new;
                    a.color[index * 3 + 2]  = a.color[index * 3 + 2] * (1.0f - w_new) + sample.color.z * w_new;
                    a.normal[index * 2 + 0] = a.normal[index * 2 + 0] * (1.0f - w_new) + sample.normal.x * w_new;
                    a.normal[index * 2 + 1] = a.normal[index * 2 + 1] * (1.0f - w_new) + sample.normal.y * w_new;
                    a.roughness[index]      = a.roughness[index] * (1.0f - w_new) + sample.roughness * w_new;
                    a.occlusion[index]      = a.occlusion[index] * (1.0f - w_new) + sample.occlusion * w_new;
                    a.alpha[index]          = out_a;
                }
            }
        }

        float segment_distance(const Vector2& p, const Vector2& a, const Vector2& b, float& t)
        {
            const Vector2 ab = b - a;
            t = clamp(((p.x - a.x) * ab.x + (p.y - a.y) * ab.y) / max(ab.x * ab.x + ab.y * ab.y, 1e-10f), 0.0f, 1.0f);
            const Vector2 closest = a + ab * t;
            return sqrt((p.x - closest.x) * (p.x - closest.x) + (p.y - closest.y) * (p.y - closest.y));
        }

        void draw_stick(atlas& a, const tile_frame& frame, const Vector2& from, const Vector2& to, const float r0, const float r1, const Vector3& color, const float roughness = 0.88f, const float occlusion = 0.72f)
        {
            const float r = max(r0, r1);
            const Vector2 lo(min(from.x, to.x) - r, min(from.y, to.y) - r);
            const Vector2 hi(max(from.x, to.x) + r, max(from.y, to.y) + r);
            Vector2 dir = to - from;
            const float len = max(sqrt(dir.x * dir.x + dir.y * dir.y), 1e-6f);
            dir = Vector2(dir.x / len, dir.y / len);
            const Vector2 across(-dir.y, dir.x);
            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, texel_sample& out)
            {
                float t;
                const float d      = segment_distance(p, from, to, t);
                const float radius = max(r0 + (r1 - r0) * t, pixel * 0.75f);
                const float cover  = saturate((radius - d) / pixel + 0.5f);
                if (cover <= 0.0f)
                {
                    return 0.0f;
                }
                const float side   = clamp(((p.x - from.x) * across.x + (p.y - from.y) * across.y) / max(radius, 1e-6f), -1.0f, 1.0f);
                const float bark_g = 0.86f + 0.24f * sin(t * 28.0f + side * 4.0f) * cos(t * 11.0f);
                out.color     = color * (0.68f + 0.34f * sqrt(saturate(1.0f - side * side))) * bark_g;
                out.normal    = Vector2(across.x * side * 0.65f, across.y * side * 0.65f);
                out.roughness = roughness;
                out.occlusion = occlusion * (0.78f + 0.22f * sqrt(saturate(1.0f - side * side)));
                return cover;
            });
        }

        // Gently arching pine needle with cylindrical normal, basal sheath darkening and sunlit tip gradient
        void draw_curved_needle(atlas& a, const tile_frame& frame, const Vector2& base, const float angle, const float len, const float arch, const float r0, const float r1, const Vector3& col_base, const Vector3& col_tip, const float roughness, const float occlusion)
        {
            const Vector2 axis(cos(angle), sin(angle));
            const Vector2 across(-axis.y, axis.x);
            const float pad = abs(arch) + max(r0, r1) + len * 0.05f;
            const Vector2 tip(base.x + axis.x * len, base.y + axis.y * len);
            const Vector2 lo(min(base.x, tip.x) - pad, min(base.y, tip.y) - pad);
            const Vector2 hi(max(base.x, tip.x) + pad, max(base.y, tip.y) + pad);

            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, texel_sample& out)
            {
                const Vector2 d(p.x - base.x, p.y - base.y);
                const float s_raw = (d.x * axis.x + d.y * axis.y) / max(len, 1e-6f);
                if (s_raw < -0.02f || s_raw > 1.02f)
                {
                    return 0.0f;
                }
                const float s       = saturate(s_raw);
                const float arch_y  = arch * sin(pi * s);
                const float perp    = (d.x * across.x + d.y * across.y) - arch_y;
                const float end_cap = (s_raw < 0.0f ? -s_raw : (s_raw > 1.0f ? s_raw - 1.0f : 0.0f)) * len;
                const float dist    = sqrt(perp * perp + end_cap * end_cap);
                const float radius  = max(r0 + (r1 - r0) * pow(s, 0.85f), pixel * 0.85f);
                const float cover   = saturate((radius - dist) / pixel + 0.5f);
                if (cover <= 0.0f)
                {
                    return 0.0f;
                }
                const float side   = clamp(perp / max(radius, 1e-6f), -1.0f, 1.0f);
                const float round_h = sqrt(saturate(1.0f - side * side));
                // Tiny brownish fascicle sheath at the very base, lush bright green toward the sunlit tip
                Vector3 c = mix(col_base, col_tip, smoothstep(0.08f, 0.92f, s));
                c = c * (0.76f + 0.28f * round_h);
                if (s < 0.08f)
                {
                    c = mix(Vector3(0.14f, 0.095f, 0.055f), c, smoothstep(0.0f, 0.08f, s));
                }
                out.color     = c;
                out.normal    = Vector2(across.x * side * 0.65f + axis.x * (s - 0.5f) * 0.25f, across.y * side * 0.65f + axis.y * (s - 0.5f) * 0.25f);
                out.roughness = roughness - 0.06f * round_h;
                out.occlusion = occlusion * (0.72f + 0.28f * smoothstep(0.0f, 0.55f, s));
                return cover;
            });
        }

        // Shingled braided cypress scale-cord branchlet (authentic Cupressus sempervirens feathery scale foliage)
        void draw_cypress_cord(atlas& a, const tile_frame& frame, const Vector2& from, const Vector2& to, const float arch, const float r0, const float r1, const Vector3& col_dark, const Vector3& col_light, const float roughness, const float occlusion, const uint32_t seed)
        {
            Vector2 dir     = to - from;
            const float len = max(sqrt(dir.x * dir.x + dir.y * dir.y), 1e-6f);
            dir             = Vector2(dir.x / len, dir.y / len);
            const Vector2 across(-dir.y, dir.x);
            const float pad = max(r0, r1) * 1.6f + abs(arch) + len * 0.04f;
            const Vector2 lo(min(from.x, to.x) - pad, min(from.y, to.y) - pad);
            const Vector2 hi(max(from.x, to.x) + pad, max(from.y, to.y) + pad);
            const float scale_freq = max(6.0f, len / max(r0 * 1.45f, 0.002f));
            const float phase_seed = static_cast<float>(seed & 255u) * 0.0245f;

            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, texel_sample& out)
            {
                const Vector2 d(p.x - from.x, p.y - from.y);
                const float s_raw = (d.x * dir.x + d.y * dir.y) / len;
                if (s_raw < -0.04f || s_raw > 1.04f)
                {
                    return 0.0f;
                }
                const float s      = saturate(s_raw);
                const float bend   = arch * sin(pi * s);
                const float perp   = (d.x * across.x + d.y * across.y) - bend;
                // Overlapping diamond scale-leaf bumps along the margin
                const float wave_l = 0.82f + 0.22f * sin(s * scale_freq * 6.283185f + phase_seed);
                const float wave_r = 0.82f + 0.22f * sin(s * scale_freq * 6.283185f + phase_seed + pi);
                const float wave   = perp >= 0.0f ? wave_l : wave_r;
                const float radius = max((r0 + (r1 - r0) * pow(s, 0.75f)) * wave, pixel * 0.9f);
                const float cap    = (s_raw < 0.0f ? -s_raw : (s_raw > 1.0f ? s_raw - 1.0f : 0.0f)) * len;
                const float dist   = sqrt(perp * perp + cap * cap);
                const float cover  = saturate((radius - dist) / pixel + 0.5f);
                if (cover <= 0.0f)
                {
                    return 0.0f;
                }
                const float side = clamp(perp / max(radius, 1e-6f), -1.0f, 1.0f);
                // Shingled overlapping scale diamond cells
                const float sc_u    = s * scale_freq;
                const float sc_cell = sc_u - floor(sc_u);
                const float diamond = saturate(1.0f - abs(sc_cell - 0.55f) * 1.4f - abs(side) * 0.45f);
                const float seam    = smoothstep(0.0f, 0.22f, sc_cell);
                const float round_h = sqrt(saturate(1.0f - side * side));
                const float tone    = saturate(0.28f + 0.52f * diamond * round_h + 0.25f * s);
                out.color     = mix(col_dark, col_light, tone) * (0.76f + 0.28f * seam);
                const float scale_slope = (sc_cell - 0.5f) * 0.45f;
                out.normal    = Vector2(across.x * side * 0.7f - dir.x * scale_slope, across.y * side * 0.7f - dir.y * scale_slope);
                out.roughness = roughness - 0.08f * diamond;
                out.occlusion = occlusion * (0.68f + 0.32f * seam * (0.45f + 0.55f * round_h));
                return cover;
            });
        }

        // half width of the blade at s along the midrib, as a share of the leaf length
        float leaf_half_width(const leaf_shape shape, const float s)
        {
            if (s <= 0.0f || s >= 1.0f)
            {
                return 0.0f;
            }
            switch (shape)
            {
                case leaf_shape::elliptic:   return 0.31f * pow(sin(pi * s), 0.72f);
                case leaf_shape::ovate:      return 0.36f * pow(sin(pi * pow(s, 0.75f)), 0.75f);
                case leaf_shape::lanceolate: return 0.125f * pow(sin(pi * s), 0.72f);
                case leaf_shape::round:      return 0.92f * sqrt(s * (1.0f - s));
                case leaf_shape::heart:      return 0.95f * sqrt(s * (1.0f - s)) * (1.15f - 0.45f * s);
                case leaf_shape::lobed:      return 0.33f * pow(sin(pi * s), 0.7f) * (0.68f + 0.32f * cos(2.0f * pi * 4.5f * s));
                case leaf_shape::blade:      return 0.08f * pow(sin(pi * s), 0.6f);
                case leaf_shape::pinnate:    return 0.38f * pow(sin(pi * pow(s, 0.85f)), 0.58f) * (0.80f + 0.32f * s); // broad rounded obovate carob leaflet
                default:                     return 0.25f * sin(pi * s);
            }
        }

        struct leaf_paint
        {
            Vector3 color;
            Vector3 vein;
            float curl      = 0.38f;
            float arch      = 0.0f;   // gentle organic sideways curve of midrib (fraction of length)
            float roughness = 0.52f;
            float occlusion = 0.92f;
            uint32_t seed   = 0;
        };

        // a leaf with its petiole at base, pointing along angle, len long, width scaled for foreshortening
        void draw_leaf(atlas& a, const tile_frame& frame, const leaf_shape shape, const Vector2& base, const float angle, const float len, const float width_scale, const leaf_paint& paint)
        {
            const Vector2 axis(cos(angle), sin(angle));
            const Vector2 across(-axis.y, axis.x);
            const float reach = len * (1.12f + abs(paint.arch));
            const Vector2 lo(base.x - reach, base.y - reach);
            const Vector2 hi(base.x + reach, base.y + reach);
            const bool palmate = shape == leaf_shape::palmate;
            const bool obovate = shape == leaf_shape::pinnate;
            const float phase  = static_cast<float>(paint.seed & 1023u) * 0.00614f;

            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, texel_sample& out)
            {
                const Vector2 d(p.x - base.x, p.y - base.y);
                const float x     = (d.x * axis.x + d.y * axis.y) / len;
                const float bend  = paint.arch * sin(pi * saturate(x));
                const float y     = ((d.x * across.x + d.y * across.y) - bend * len) / (len * max(width_scale, 0.22f));

                float edge;
                float s_along;
                float y_rel;
                if (palmate)
                {
                    const float cx   = x - 0.42f;
                    const float r    = sqrt(cx * cx + y * y);
                    const float phi  = atan2(y, cx);
                    const float lobe = 0.56f * (0.5f + 0.5f * pow(abs(cos(2.5f * phi)), 0.7f)) * (1.0f + 0.035f * sin(phi * 28.0f));
                    const float cut  = abs(phi) > 2.6f ? 0.15f : 1.0f;
                    edge    = lobe * cut - r;
                    s_along = saturate(x);
                    y_rel   = r > 1e-4f ? y / max(lobe, 1e-3f) : 0.0f;
                }
                else
                {
                    float w = leaf_half_width(shape, x);
                    if (obovate && x > 0.88f && x < 1.0f)
                    {
                        // Authentic subtle emarginate blunt/notched carob leaflet tip
                        const float notch = smoothstep(0.88f, 1.0f, x) * (1.0f - smoothstep(0.0f, 0.25f, abs(y) / max(w, 1e-3f)));
                        w *= (1.0f - 0.14f * notch);
                    }
                    edge    = w > 0.0f ? w - abs(y) : -1.0f;
                    s_along = saturate(x);
                    y_rel   = w > 1e-4f ? y / w : 0.0f;
                }

                const float edge_px    = edge * len * max(width_scale, 0.22f) / pixel;
                const float petiole_px = (0.015f * len - abs(d.x * across.x + d.y * across.y)) / pixel;
                const bool on_petiole  = x > -0.14f && x < 0.07f && petiole_px > -0.5f;
                float cover = saturate(edge_px + 0.5f);
                if (on_petiole)
                {
                    cover = max(cover, saturate(petiole_px + 0.5f));
                }
                if (cover <= 0.0f)
                {
                    return 0.0f;
                }

                // Midrib, branching lateral veins, natural mottled botanical texture, gently shaded margin & base
                const float midrib     = (1.0f - smoothstep(0.0f, 0.075f, abs(y_rel))) * (1.0f - smoothstep(0.82f, 0.98f, s_along));
                const float vein_freq  = shape == leaf_shape::lanceolate ? 11.0f : 7.5f;
                const float vein_phase = s_along * vein_freq - abs(y_rel) * 1.45f;
                const float vein_line  = 1.0f - smoothstep(0.0f, 0.11f, abs(vein_phase - round(vein_phase)));
                const float vein       = vein_line * smoothstep(0.07f, 0.22f, abs(y_rel)) * (1.0f - smoothstep(0.76f, 0.94f, abs(y_rel))) * (1.0f - smoothstep(0.88f, 1.0f, s_along));
                const float margin     = smoothstep(0.72f, 1.0f, abs(y_rel));
                const float mottle     = 0.93f + 0.14f * sin(s_along * 17.0f + phase) * cos(y_rel * 9.0f - phase * 0.7f);
                const float base_ao    = 0.80f + 0.22f * smoothstep(0.0f, 0.45f, s_along);
                const float tip_glow   = 1.0f + 0.12f * smoothstep(0.65f, 1.0f, s_along);

                Vector3 c = paint.color * base_ao * tip_glow * mottle;
                c = mix(c, paint.vein, saturate(midrib * 0.58f + vein * 0.28f));
                c = c * (1.0f - margin * 0.15f);
                if (on_petiole && x < 0.02f)
                {
                    c = paint.vein * 0.65f;
                }
                out.color = c;

                // Subtle organic blade curvature + soft midrib crease (gentle tilt avoids harsh grazing Fresnel rim glare)
                const float vein_bump = (sin(vein_phase * 6.283185f) * 0.045f) * smoothstep(0.10f, 0.30f, abs(y_rel));
                const float tilt      = y_rel * paint.curl - midrib * 0.14f * (y_rel >= 0.0f ? 1.0f : -1.0f);
                const float along_t   = (s_along - 0.45f) * 0.16f + vein_bump;
                out.normal    = Vector2(across.x * tilt + axis.x * along_t, across.y * tilt + axis.y * along_t);
                out.roughness = clamp(paint.roughness + midrib * 0.05f + margin * 0.04f - 0.02f * (mottle - 0.93f), 0.62f, 0.96f);
                out.occlusion = paint.occlusion * (0.82f + 0.18f * smoothstep(0.0f, 0.35f, s_along));
                return cover;
            });
        }

        void draw_blossom(atlas& a, const tile_frame& frame, const Vector2& center, const float radius, const float rotation, const Vector3& color)
        {
            const Vector2 lo(center.x - radius, center.y - radius);
            const Vector2 hi(center.x + radius, center.y + radius);
            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, texel_sample& out)
            {
                const Vector2 d(p.x - center.x, p.y - center.y);
                const float r     = sqrt(d.x * d.x + d.y * d.y) / radius;
                const float phi   = atan2(d.y, d.x) + rotation;
                const float petal = 0.62f + 0.38f * pow(abs(cos(2.5f * phi)), 0.5f) - 0.08f * pow(abs(cos(5.0f * phi)), 8.0f);
                const float cover = saturate((petal - r) * radius / pixel + 0.5f);
                if (cover <= 0.0f)
                {
                    return 0.0f;
                }
                const float heart = 1.0f - smoothstep(0.12f, 0.3f, r);
                out.color     = mix(color * (0.85f + 0.2f * r), Vector3(0.75f, 0.62f, 0.18f), heart * 0.65f);
                out.normal    = Vector2(d.x / radius * 0.35f, d.y / radius * 0.35f);
                out.roughness = 0.70f;
                out.occlusion = 0.92f;
                return cover;
            });
        }

        void draw_cone_bud(atlas& a, const tile_frame& frame, const Vector2& center, const float rx, const float ry, const float angle, const Vector3& color, const float occlusion)
        {
            const float reach = max(rx, ry) * 1.25f;
            const Vector2 axis(cos(angle), sin(angle));
            const Vector2 across(-axis.y, axis.x);
            const Vector2 lo(center.x - reach, center.y - reach);
            const Vector2 hi(center.x + reach, center.y + reach);
            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, texel_sample& out)
            {
                const Vector2 d(p.x - center.x, p.y - center.y);
                const float lx    = (d.x * axis.x + d.y * axis.y) / max(ry, 1e-5f);
                const float ly    = (d.x * across.x + d.y * across.y) / max(rx, 1e-5f);
                const float r     = sqrt(lx * lx + ly * ly);
                const float cover = saturate((1.0f - r) * min(rx, ry) / pixel + 0.5f);
                if (cover <= 0.0f)
                {
                    return 0.0f;
                }
                const float dome  = sqrt(saturate(1.0f - r * r));
                const float scale = 0.85f + 0.25f * sin(lx * 14.0f) * cos(ly * 12.0f);
                out.color     = color * (0.62f + 0.42f * dome) * scale;
                out.normal    = Vector2((across.x * ly + axis.x * lx) * 0.7f, (across.y * ly + axis.y * lx) * 0.7f);
                out.roughness = 0.84f;
                out.occlusion = occlusion * (0.75f + 0.25f * dome);
                return cover;
            });
        }

        struct paint_rng
        {
            uint32_t state;
            float unit()
            {
                state = state * 1664525u + 1013904223u;
                return static_cast<float>(state >> 8) / 16777216.0f;
            }
            float signed_unit() { return unit() * 2.0f - 1.0f; }
        };

        // Feathery recursive Mediterranean cypress scale spray (Cupressus sempervirens)
        void draw_scale_spray(atlas& a, const tile_frame& frame, const Vector2& from, const float angle, const float len, const uint32_t depth, const Vector3& col_dark, const Vector3& col_light, const float roughness, const float occlusion, paint_rng& rng)
        {
            const Vector2 dir(cos(angle), sin(angle));
            const Vector2 to(from.x + dir.x * len, from.y + dir.y * len);
            const float arch = len * 0.07f * rng.signed_unit();
            const float r0   = max(len * (depth == 0 ? 0.058f : 0.040f), 0.0020f);
            const float r1   = r0 * 0.42f;
            draw_cypress_cord(a, frame, from, to, arch, r0, r1, col_dark, col_light, roughness, occlusion, rng.state);

            if (depth == 0)
            {
                return;
            }

            const uint32_t pairs = depth >= 2 ? 6u : 4u;
            for (uint32_t i = 0; i < pairs; i++)
            {
                const float t      = 0.12f + 0.78f * (static_cast<float>(i) + 0.35f * rng.unit()) / static_cast<float>(pairs);
                const Vector2 p(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
                const float taper  = 1.0f - 0.58f * t;
                for (int32_t side = -1; side <= 1; side += 2)
                {
                    const float spread = (0.38f + 0.24f * rng.unit()) * static_cast<float>(side);
                    const float c_len  = len * (depth >= 2 ? 0.48f : 0.52f) * taper * (0.82f + 0.32f * rng.unit());
                    const float tint   = 0.88f + 0.24f * rng.unit();
                    draw_scale_spray(a, frame, p, angle + spread, c_len, depth - 1, col_dark * tint, col_light * tint, roughness, clamp(occlusion + 0.05f, 0.0f, 1.0f), rng);
                }
            }
        }

        Vector3 leaf_color_for(const species& s, const season time, paint_rng& rng)
        {
            const float var = s.leaf_color_var;
            Vector3 base    = s.leaf_color;
            if (time == season::spring)
            {
                base = mix(s.leaf_color, s.spring_color, s.evergreen ? 0.3f : 0.75f);
            }
            else if (time == season::autumn && !s.evergreen)
            {
                const float pick = rng.unit();
                base = pick < 0.12f ? mix(s.leaf_color, s.autumn_color, 0.4f) : mix(s.autumn_color, s.autumn_color_b, rng.unit());
            }
            const float value = 1.0f + var * rng.signed_unit();
            const Vector3 hue = Vector3(1.0f + var * 0.55f * rng.signed_unit(), 1.0f, 1.0f + var * 0.45f * rng.signed_unit());
            return Vector3(base.x * hue.x, base.y * hue.y, base.z * hue.z) * value;
        }

        // one twig variant, the twig grows from the bottom centre of its tile up to near the top
        void paint_twig(atlas& a, const tile_frame& frame, const species& s, const season time, const uint32_t variant)
        {
            paint_rng rng = { static_cast<uint32_t>(texture_hash(s, time)) ^ (variant * 0x9e3779b9u) ^ 0x51a7c3d1u };
            const bool bare        = time == season::bare && !s.evergreen;
            const bool blossoms    = s.flowers && time == season::spring;
            const Vector3 twig     = s.bark_color * 0.80f;
            const float w          = frame.width_m;
            const float h          = frame.height_m;
            const float leaf_len   = s.leaf_size;

            // the main twig, a gentle organic curve
            const uint32_t knots = 6;
            vector<Vector2> path;
            float x_offset = 0.0f;
            for (uint32_t i = 0; i <= knots; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(knots);
                x_offset += rng.signed_unit() * w * 0.032f;
                const float top_frac = (s.leaf == leaf_shape::needle || s.leaf == leaf_shape::scale) ? 0.81f : (s.leaf == leaf_shape::pinnate ? 0.72f : 0.80f);
                path.push_back(Vector2(x_offset * t, t * h * top_frac));
            }
            auto along = [&path](const float t, Vector2& position, Vector2& direction)
            {
                const float f    = clamp(t, 0.0f, 1.0f) * static_cast<float>(path.size() - 1);
                const uint32_t i = min(static_cast<uint32_t>(f), static_cast<uint32_t>(path.size() - 2));
                const float l    = f - static_cast<float>(i);
                position  = path[i] + (path[i + 1] - path[i]) * l;
                direction = path[i + 1] - path[i];
                const float len = max(sqrt(direction.x * direction.x + direction.y * direction.y), 1e-6f);
                direction = Vector2(direction.x / len, direction.y / len);
            };
            const float twig_radius = max(0.0028f, s.card_size * 0.0085f);

            if (s.fronds > 0)
            {
                const float rachis_top = h * 0.98f;
                draw_stick(a, frame, Vector2(0.0f, 0.0f), Vector2(0.0f, rachis_top), w * 0.022f, w * 0.006f, mix(twig, s.leaf_color, 0.5f), 0.72f, 0.75f);
                const uint32_t pairs = 34;
                for (uint32_t i = 0; i < pairs; i++)
                {
                    const float t   = 0.06f + 0.92f * static_cast<float>(i) / static_cast<float>(pairs);
                    const float y   = t * rachis_top;
                    const float len = w * 0.5f * pow(sin(pi * min(1.0f, 0.15f + t * 0.95f)), 0.5f) * (0.85f + 0.3f * rng.unit());
                    for (int32_t side = -1; side <= 1; side += 2)
                    {
                        const float rise  = 0.55f - 0.25f * t + 0.1f * rng.signed_unit();
                        const float angle = side < 0 ? pi - rise : rise;
                        leaf_paint paint;
                        paint.color     = leaf_color_for(s, time, rng);
                        paint.vein      = mix(paint.color, s.leaf_color_back, 0.55f);
                        paint.curl      = 0.28f;
                        paint.roughness = 0.52f;
                        paint.occlusion = 0.85f + 0.15f * t;
                        paint.seed      = rng.state;
                        draw_leaf(a, frame, leaf_shape::lanceolate, Vector2(0.0f, y), angle, len, 0.85f, paint);
                    }
                }
                return;
            }

            if (s.leaf == leaf_shape::needle)
            {
                // Mediterranean Aleppo pine bough: gnarled warm reddish-brown woody shoots tipped with dense
                // 3-layer rosette fascicle tufts of gently curved pine needles + occasional warm dry inner needles
                struct shoot
                {
                    Vector2 from;
                    Vector2 to;
                    float r0 = 0.004f;
                    float r1 = 0.002f;
                    bool terminal = true;
                };
                vector<shoot> shoots;
                for (uint32_t i = 0; i + 1 < path.size(); i++)
                {
                    const float t0 = static_cast<float>(i) / static_cast<float>(path.size() - 1);
                    const float t1 = static_cast<float>(i + 1) / static_cast<float>(path.size() - 1);
                    shoots.push_back({ path[i], path[i + 1], twig_radius * (1.25f - 0.55f * t0), twig_radius * (1.25f - 0.55f * t1), i + 2 == path.size() });
                }

                const uint32_t side_count = 7u + (variant % 2u);
                for (uint32_t i = 0; i < side_count; i++)
                {
                    const float frac = static_cast<float>(i) / static_cast<float>(max(1u, side_count - 1u));
                    Vector2 position, direction;
                    along(0.14f + 0.74f * frac, position, direction);
                    const float side  = (i % 2 ? 1.0f : -1.0f);
                    const float angle = atan2(direction.y, direction.x) + side * (0.62f + 0.30f * rng.unit());
                    float length      = h * (0.38f - 0.16f * frac) * (0.85f + 0.30f * rng.unit());
                    const float room  = w * 0.45f - leaf_len * 0.78f - abs(position.x);
                    if (abs(cos(angle)) * length > room)
                    {
                        length = max(room, 0.0f) / max(abs(cos(angle)), 1e-3f);
                    }
                    if (length < leaf_len * 0.35f)
                    {
                        continue;
                    }
                    // Slight organic elbow mid-shoot
                    const Vector2 mid(position.x + cos(angle) * length * 0.52f + rng.signed_unit() * length * 0.08f, position.y + sin(angle) * length * 0.52f);
                    const float angle2 = angle + side * (-0.18f + 0.15f * rng.signed_unit());
                    const Vector2 tip(mid.x + cos(angle2) * length * 0.50f, mid.y + sin(angle2) * length * 0.50f);
                    const float sr = twig_radius * (0.78f - 0.25f * frac);
                    shoots.push_back({ position, mid, sr, sr * 0.72f, false });
                    shoots.push_back({ mid, tip, sr * 0.72f, sr * 0.45f, true });
                }

                const Vector3 pine_wood = mix(s.bark_color * 0.78f, Vector3(0.135f, 0.082f, 0.048f), 0.55f);
                const Vector3 dry_needle = Vector3(0.24f, 0.135f, 0.042f);
                const float pixel_m      = frame.width_m / static_cast<float>(a.tile_size);
                const float needle_r0    = max(max(leaf_len * 0.017f, 0.0014f), pixel_m * 1.15f);
                const float needle_r1    = needle_r0 * 0.38f;

                // Helper to paint one depth pass of lush pine fascicles and terminal pom-pom needle rosettes
                auto paint_needle_layer = [&](const float layer_bright, const float layer_ao, const float layer_rough, const float density_scale, const bool include_dry)
                {
                    for (const shoot& sh : shoots)
                    {
                        const Vector2 d        = sh.to - sh.from;
                        const float shoot_len  = sqrt(d.x * d.x + d.y * d.y);
                        if (shoot_len < 1e-4f)
                        {
                            continue;
                        }
                        const float base_angle = atan2(d.y, d.x);
                        const float step       = max(leaf_len * 0.095f / density_scale, pixel_m * 2.2f);
                        const uint32_t nodes   = clamp(static_cast<uint32_t>(shoot_len / step), 4u, 72u);

                        // Lateral paired needle fascicles along the shoot
                        for (uint32_t i = 0; i < nodes; i++)
                        {
                            const float t = 0.08f + 0.90f * (static_cast<float>(i) + 0.4f * rng.unit()) / static_cast<float>(nodes);
                            const Vector2 p(sh.from.x + d.x * t, sh.from.y + d.y * t);
                            for (int32_t side = -1; side <= 1; side += 2)
                            {
                                // Two diverging needles per Aleppo pine fascicle
                                for (uint32_t pair = 0; pair < 2; pair++)
                                {
                                    const float spread = static_cast<float>(side) * (0.42f + 0.48f * rng.unit()) + (pair == 0 ? -0.10f : 0.10f);
                                    const float n_len  = leaf_len * (0.76f + 0.36f * rng.unit()) * (0.85f + 0.22f * t);
                                    const float arch   = n_len * 0.11f * static_cast<float>(side) * (0.5f + 0.8f * rng.unit());
                                    const bool is_dry  = include_dry && t < 0.45f && rng.unit() < 0.075f;
                                    Vector3 c_base     = is_dry ? dry_needle * 0.72f : leaf_color_for(s, time, rng) * layer_bright * (0.76f + 0.26f * t);
                                    Vector3 c_tip      = is_dry ? dry_needle * 1.08f : mix(c_base, s.leaf_color_back * layer_bright, 0.45f);
                                    draw_curved_needle(a, frame, p, base_angle + spread, n_len, arch, needle_r0, needle_r1, c_base, c_tip, is_dry ? 0.78f : layer_rough, layer_ao);
                                }
                            }
                        }

                        // Lush terminal rosette tuft at every shoot tip (signature Aleppo pine needle cloud silhouette)
                        if (sh.terminal)
                        {
                            const uint32_t tuft_needles = static_cast<uint32_t>(round(22.0f * density_scale));
                            for (uint32_t k = 0; k < tuft_needles; k++)
                            {
                                const float frac   = (static_cast<float>(k) + 0.5f) / static_cast<float>(tuft_needles) * 2.0f - 1.0f;
                                const float fan    = frac * 1.48f + 0.14f * rng.signed_unit();
                                const float n_len  = leaf_len * (0.84f + 0.34f * rng.unit()) * (1.0f - 0.16f * abs(frac));
                                const float arch   = n_len * 0.10f * (frac >= 0.0f ? 1.0f : -1.0f) * (0.4f + 0.8f * rng.unit());
                                const Vector2 origin(sh.to.x - d.x * (0.04f + 0.10f * rng.unit()), sh.to.y - d.y * (0.04f + 0.10f * rng.unit()));
                                Vector3 c_base     = leaf_color_for(s, time, rng) * layer_bright * 0.88f;
                                Vector3 c_tip      = mix(c_base, s.leaf_color_back * layer_bright * 1.08f, 0.55f);
                                draw_curved_needle(a, frame, origin, base_angle + fan, n_len, arch, needle_r0 * 1.05f, needle_r1, c_base, c_tip, layer_rough, clamp(layer_ao + 0.06f, 0.0f, 1.0f));
                            }
                        }
                    }
                };

                // 3-layer back-to-front depth composition: deep shadowed back -> woody skeleton -> mid lush body -> sunlit front tips + terminal buds
                paint_needle_layer(0.58f, 0.44f, 0.88f, 0.90f, true);
                for (const shoot& sh : shoots)
                {
                    draw_stick(a, frame, sh.from, sh.to, sh.r0, sh.r1, pine_wood, 0.92f, 0.62f);
                }
                paint_needle_layer(0.88f, 0.78f, 0.84f, 0.95f, false);
                paint_needle_layer(1.12f, 0.98f, 0.78f, 0.55f, false);

                // Delicate scaly reddish-brown terminal candle buds
                for (const shoot& sh : shoots)
                {
                    if (sh.terminal)
                    {
                        const Vector2 d = sh.to - sh.from;
                        draw_cone_bud(a, frame, sh.to, twig_radius * 0.82f, twig_radius * 1.45f, atan2(d.y, d.x), Vector3(0.19f, 0.11f, 0.055f), 0.88f);
                    }
                }
                return;
            }

            if (s.leaf == leaf_shape::scale)
            {
                // Mediterranean cypress (Cupressus sempervirens): 3-layer feathery shingled emerald scale sprays
                const Vector3 cypress_wood = mix(s.bark_color * 0.75f, Vector3(0.11f, 0.08f, 0.055f), 0.5f);

                auto paint_cypress_layer = [&](const float bright, const float ao, const float rough, const float scale_mult, const uint32_t branch_count)
                {
                    for (uint32_t i = 0; i < branch_count; i++)
                    {
                        const float frac = static_cast<float>(i) / static_cast<float>(max(1u, branch_count - 1u));
                        Vector2 position, direction;
                        along(0.08f + 0.80f * frac, position, direction);
                        const float side     = (i % 2 ? 1.0f : -1.0f);
                        const float base_ang = atan2(direction.y, direction.x) + side * (0.28f + 0.24f * rng.unit());
                        const float len      = h * (0.42f - 0.16f * frac) * scale_mult * (0.86f + 0.26f * rng.unit());
                        const Vector3 c_dark  = leaf_color_for(s, time, rng) * bright * 0.75f;
                        const Vector3 c_light = mix(c_dark, s.leaf_color_back * bright * 1.15f, 0.65f);
                        draw_scale_spray(a, frame, position, base_ang, len, 2, c_dark, c_light, rough, ao, rng);
                    }
                    // Ascending crown tip spray
                    const Vector3 t_dark  = leaf_color_for(s, time, rng) * bright * 0.82f;
                    const Vector3 t_light = mix(t_dark, s.leaf_color_back * bright * 1.2f, 0.7f);
                    draw_scale_spray(a, frame, path.back(), pi * 0.5f + 0.12f * rng.signed_unit(), h * 0.17f * scale_mult, 1, t_dark, t_light, rough, clamp(ao + 0.06f, 0.0f, 1.0f), rng);
                };

                paint_cypress_layer(0.62f, 0.44f, 0.88f, 1.02f, 7u);
                for (uint32_t i = 0; i + 1 < path.size(); i++)
                {
                    const float t0 = static_cast<float>(i) / static_cast<float>(path.size() - 1);
                    const float t1 = static_cast<float>(i + 1) / static_cast<float>(path.size() - 1);
                    draw_stick(a, frame, path[i], path[i + 1], twig_radius * (1.05f - 0.58f * t0), twig_radius * (1.05f - 0.58f * t1), cypress_wood, 0.92f, 0.58f);
                }
                paint_cypress_layer(0.92f, 0.78f, 0.84f, 0.94f, 6u);
                paint_cypress_layer(1.16f, 0.98f, 0.78f, 0.80f, 4u);

                // Occasional small dusty grey-brown spherical cypress cone (galbulus)
                if (variant % 2u == 0u)
                {
                    Vector2 cp, cd;
                    along(0.32f + 0.25f * rng.unit(), cp, cd);
                    draw_cone_bud(a, frame, Vector2(cp.x + rng.signed_unit() * w * 0.06f, cp.y), h * 0.026f, h * 0.030f, rng.unit() * pi, Vector3(0.15f, 0.125f, 0.095f), 0.85f);
                }
                return;
            }

            // Broadleaves (Zakynthos olive, carob, etc.): organic forking shoots + 3-layer depth leaf painting
            struct shoot
            {
                Vector2 from;
                Vector2 to;
                float length = 0.0f;
            };
            vector<shoot> shoots;
            shoots.push_back({ path.front(), path.back(), h * 0.82f });
            const bool sprays    = s.leaf != leaf_shape::blade;
            const uint32_t sides = sprays ? 5u + (variant % 2u) : 0u;
            for (uint32_t i = 0; i < sides; i++)
            {
                Vector2 position, direction;
                along(0.12f + 0.66f * (static_cast<float>(i) + 0.3f * rng.unit()) / static_cast<float>(sides), position, direction);
                const float side  = i % 2 ? 1.0f : -1.0f;
                const float angle = atan2(direction.y, direction.x) + side * (0.50f + 0.32f * rng.unit());
                float length      = h * (0.46f - 0.20f * static_cast<float>(i) / static_cast<float>(sides)) * (0.85f + 0.30f * rng.unit());
                const float room  = w * 0.46f - leaf_len * 0.75f - abs(position.x);
                if (abs(cos(angle)) * length > room)
                {
                    length = max(room, 0.0f) / max(abs(cos(angle)), 1e-3f);
                }
                if (length < leaf_len * 0.55f)
                {
                    continue;
                }
                const Vector2 end(position.x + cos(angle) * length, position.y + sin(angle) * length);
                shoots.push_back({ position, end, length });
            }

            auto draw_woody_skeleton = [&]()
            {
                const Vector3 shoot_wood = s.leaf == leaf_shape::lanceolate ? mix(twig, Vector3(0.24f, 0.23f, 0.20f), 0.45f) : twig;
                for (uint32_t i = 0; i + 1 < path.size(); i++)
                {
                    const float t0 = static_cast<float>(i) / static_cast<float>(path.size() - 1);
                    const float t1 = static_cast<float>(i + 1) / static_cast<float>(path.size() - 1);
                    draw_stick(a, frame, path[i], path[i + 1], twig_radius * (1.05f - 0.62f * t0), twig_radius * (1.05f - 0.62f * t1), shoot_wood, 0.86f, 0.64f);
                }
                for (size_t si = 1; si < shoots.size(); si++)
                {
                    draw_stick(a, frame, shoots[si].from, shoots[si].to, twig_radius * 0.58f, twig_radius * 0.28f, shoot_wood, 0.86f, 0.68f);
                }
            };

            if (bare)
            {
                draw_woody_skeleton();
                function<void(Vector2, float, float, uint32_t)> fork = [&](const Vector2 from, const float angle, const float len, const uint32_t depth)
                {
                    const Vector2 to(from.x + cos(angle) * len, from.y + sin(angle) * len);
                    draw_stick(a, frame, from, to, twig_radius * (0.45f + 0.2f * depth), twig_radius * 0.3f, twig, 0.88f, 0.72f);
                    if (depth == 0)
                    {
                        draw_blossom(a, frame, to, twig_radius * 1.4f, 0.0f, twig * 0.7f);
                        return;
                    }
                    fork(to, angle + 0.45f + 0.2f * rng.signed_unit(), len * 0.62f, depth - 1);
                    fork(to, angle - 0.45f + 0.2f * rng.signed_unit(), len * 0.62f, depth - 1);
                };
                for (uint32_t i = 0; i < 4; i++)
                {
                    Vector2 position, direction;
                    along(0.2f + 0.2f * static_cast<float>(i), position, direction);
                    const float side = i % 2 ? 1.0f : -1.0f;
                    fork(position, atan2(direction.y, direction.x) + side * 0.6f, h * 0.22f, 2);
                }
                Vector2 tip, tip_dir;
                along(1.0f, tip, tip_dir);
                fork(tip, atan2(tip_dir.y, tip_dir.x), h * 0.15f, 1);
                return;
            }

            // Special botanical painter for Zakynthos Olive (opposite pairs of silvery-sage + dark waxy olive lanceolate leaves)
            if (s.leaf == leaf_shape::lanceolate && !blossoms)
            {
                auto paint_olive_pass = [&](const float bright, const float ao, const float density_mult)
                {
                    for (size_t si = 0; si < shoots.size(); si++)
                    {
                        const shoot& sh      = shoots[si];
                        const uint32_t pairs = clamp(static_cast<uint32_t>(round((sh.length / max(leaf_len * 0.28f, 0.01f)) * density_mult)), 4u, 24u);
                        for (uint32_t i = 0; i <= pairs; i++)
                        {
                            const bool tip_leaf = (i == pairs);
                            const float t       = tip_leaf ? 1.0f : (0.08f + 0.88f * (static_cast<float>(i) + 0.35f * rng.unit()) / static_cast<float>(pairs));
                            Vector2 position, direction;
                            if (si == 0)
                            {
                                along(t, position, direction);
                            }
                            else
                            {
                                const Vector2 d = sh.to - sh.from;
                                position  = sh.from + d * t;
                                direction = Vector2(d.x / max(sh.length, 1e-6f), d.y / max(sh.length, 1e-6f));
                            }
                            const float twig_angle = atan2(direction.y, direction.x);
                            const int32_t s_start  = tip_leaf ? 0 : -1;
                            const int32_t s_end    = tip_leaf ? 0 : 1;
                            for (int32_t side = s_start; side <= s_end; side += 2)
                            {
                                const float angle   = tip_leaf ? (twig_angle + 0.15f * rng.signed_unit()) : (twig_angle + static_cast<float>(side) * (0.52f + 0.58f * rng.unit()));
                                const float l_len   = leaf_len * (0.78f + 0.38f * rng.unit()) * (0.85f + 0.22f * sin(pi * min(t, 0.95f)));
                                const float w_scale = 0.68f + 0.38f * rng.unit();
                                // Authentic olive shimmer: ~46% silvery-sage velvety undersides vs ~54% deep waxy dark-olive upper faces
                                const bool underside = rng.unit() < 0.46f;
                                leaf_paint paint;
                                if (underside)
                                {
                                    paint.color     = mix(s.leaf_color_back, leaf_color_for(s, time, rng), 0.18f) * bright * (0.90f + 0.20f * rng.unit());
                                    paint.vein      = paint.color * 1.12f;
                                    paint.roughness = 0.92f;
                                }
                                else
                                {
                                    paint.color     = leaf_color_for(s, time, rng) * bright;
                                    paint.vein      = mix(paint.color, s.leaf_color_back, 0.62f);
                                    paint.roughness = 0.74f;
                                }
                                paint.curl      = 0.15f;
                                paint.arch      = 0.08f * rng.signed_unit();
                                paint.occlusion = ao;
                                paint.seed      = rng.state;
                                const Vector2 pet(position.x + cos(angle) * l_len * 0.06f, position.y + sin(angle) * l_len * 0.06f);
                                draw_leaf(a, frame, leaf_shape::lanceolate, pet, angle, l_len, w_scale, paint);
                            }
                        }
                    }
                };

                paint_olive_pass(0.66f, 0.46f, 0.85f);
                draw_woody_skeleton();
                paint_olive_pass(0.94f, 0.78f, 0.90f);
                paint_olive_pass(1.12f, 0.98f, 0.48f);
                return;
            }

            // Special botanical painter for Carob / Pinnate compound leaves (thick leathery obovate paired leaflets, airy silhouette)
            if (s.leaf == leaf_shape::pinnate && !blossoms)
            {
                auto paint_pinnate_pass = [&](const float bright, const float ao, const uint32_t fronds_per_shoot)
                {
                    for (size_t si = 0; si < shoots.size(); si++)
                    {
                        const shoot& sh = shoots[si];
                        const uint32_t count = max(2u, static_cast<uint32_t>(round(fronds_per_shoot * (si == 0 ? 1.15f : 0.75f))));
                        for (uint32_t i = 0; i < count; i++)
                        {
                            const bool tip_f = (i + 1 == count);
                            const float t    = tip_f ? 0.94f : (0.14f + 0.78f * (static_cast<float>(i) + 0.35f * rng.unit()) / static_cast<float>(count));
                            Vector2 position, direction;
                            if (si == 0)
                            {
                                along(t, position, direction);
                            }
                            else
                            {
                                const Vector2 d = sh.to - sh.from;
                                position  = sh.from + d * t * 0.82f;
                                direction = Vector2(d.x / max(sh.length, 1e-6f), d.y / max(sh.length, 1e-6f));
                            }
                            const float twig_angle = atan2(direction.y, direction.x);
                            const float side       = (i % 2 ? 1.0f : -1.0f);
                            const float angle      = tip_f ? (twig_angle + 0.16f * rng.signed_unit()) : (twig_angle + side * (0.50f + 0.42f * rng.unit()));
                            // Keep compound rachises comfortably inside the tile bounds for a scalloped natural silhouette (zero clipped card borders)
                            float rachis_len       = leaf_len * (0.62f + 0.24f * rng.unit());
                            const float x_room     = w * 0.43f - leaf_len * 0.25f - abs(position.x);
                            const float y_room     = h * 0.90f - leaf_len * 0.22f - position.y;
                            if (abs(cos(angle)) * rachis_len > x_room)
                            {
                                rachis_len = max(x_room, leaf_len * 0.28f) / max(abs(cos(angle)), 0.25f);
                            }
                            if (sin(angle) > 0.1f && sin(angle) * rachis_len > y_room)
                            {
                                rachis_len = max(y_room, leaf_len * 0.28f) / max(sin(angle), 0.25f);
                            }
                            const Vector2 end(position.x + cos(angle) * rachis_len, position.y + sin(angle) * rachis_len);
                            draw_stick(a, frame, position, end, twig_radius * 0.32f, twig_radius * 0.15f, mix(twig, s.leaf_color, 0.35f), 0.88f, ao * 0.88f);

                            const uint32_t leaflet_pairs = 4u + (rng.state % 2u);
                            for (uint32_t l = 0; l < leaflet_pairs; l++)
                            {
                                const float lt = 0.22f + 0.76f * static_cast<float>(l) / static_cast<float>(max(1u, leaflet_pairs - 1u));
                                const Vector2 lp(position.x + cos(angle) * rachis_len * lt, position.y + sin(angle) * rachis_len * lt);
                                const float lf_len = rachis_len * (0.31f + 0.08f * sin(pi * lt)) * (0.86f + 0.24f * rng.unit());
                                for (int32_t sd = -1; sd <= 1; sd += 2)
                                {
                                    const bool underside = rng.unit() < 0.22f;
                                    leaf_paint paint;
                                    if (underside)
                                    {
                                        paint.color     = mix(s.leaf_color_back, leaf_color_for(s, time, rng), 0.25f) * bright;
                                        paint.vein      = paint.color * 1.15f;
                                        paint.roughness = 0.88f;
                                    }
                                    else
                                    {
                                        paint.color     = leaf_color_for(s, time, rng) * bright;
                                        paint.vein      = mix(paint.color, s.leaf_color_back * 1.25f, 0.58f);
                                        paint.roughness = 0.68f;
                                    }
                                    paint.curl      = 0.14f;
                                    paint.arch      = 0.05f * rng.signed_unit();
                                    paint.occlusion = ao;
                                    paint.seed      = rng.state;
                                    const float l_ang = angle + static_cast<float>(sd) * (0.82f + 0.28f * rng.unit());
                                    draw_leaf(a, frame, leaf_shape::pinnate, lp, l_ang, lf_len, 0.78f + 0.22f * rng.unit(), paint);
                                }
                            }
                        }
                    }
                };

                paint_pinnate_pass(0.64f, 0.44f, 3u);
                draw_woody_skeleton();
                paint_pinnate_pass(0.94f, 0.78f, 3u);
                paint_pinnate_pass(1.14f, 0.98f, 2u);
                return;
            }

            // General broadleaves (ovate, round, heart, lobed, palmate, blade, spring blossoms)
            draw_woody_skeleton();
            float total_length = 0.0f;
            for (const shoot& sh : shoots)
            {
                total_length += sh.length;
            }

            float leaf_area = 0.35f;
            switch (s.leaf)
            {
                case leaf_shape::lanceolate: leaf_area = 0.16f; break;
                case leaf_shape::blade:      leaf_area = 0.12f; break;
                case leaf_shape::ovate:      leaf_area = 0.42f; break;
                case leaf_shape::round:      leaf_area = 0.70f; break;
                case leaf_shape::heart:      leaf_area = 0.72f; break;
                case leaf_shape::palmate:    leaf_area = 0.60f; break;
                case leaf_shape::lobed:      leaf_area = 0.42f; break;
                case leaf_shape::pinnate:    leaf_area = 0.32f; break;
                default: break;
            }
            const float coverage  = blossoms ? 0.58f : 0.82f;
            const float wanted    = coverage * w * h / max(leaf_len * leaf_len * leaf_area, 1e-6f);
            const uint32_t leaves = clamp(static_cast<uint32_t>(wanted), max(1u, s.leaves_per_card), 160u);

            for (uint32_t i = 0; i < leaves; i++)
            {
                const float depth_frac = static_cast<float>(i) / static_cast<float>(max(1u, leaves - 1u));
                const float layer_ao   = 0.48f + 0.52f * depth_frac;
                const float layer_br   = 0.72f + 0.40f * depth_frac;

                float pick = rng.unit() * total_length;
                size_t shoot_index = 0;
                while (shoot_index + 1 < shoots.size() && pick > shoots[shoot_index].length)
                {
                    pick -= shoots[shoot_index].length;
                    shoot_index++;
                }
                const bool tip_leaf = i < shoots.size();
                if (tip_leaf)
                {
                    shoot_index = i;
                }
                const shoot& host = shoots[shoot_index];
                const float t     = tip_leaf ? 1.0f : 0.1f + 0.9f * pow(rng.unit(), 0.8f);
                Vector2 position, direction;
                if (shoot_index == 0)
                {
                    along(t, position, direction);
                }
                else
                {
                    const Vector2 d = host.to - host.from;
                    position  = host.from + d * t;
                    direction = Vector2(d.x / max(host.length, 1e-6f), d.y / max(host.length, 1e-6f));
                }
                const float twig_angle = atan2(direction.y, direction.x);
                const float side       = (i % 2 ? 1.0f : -1.0f);
                float angle            = tip_leaf ? twig_angle + 0.1f * rng.signed_unit() : twig_angle + side * (0.55f + 0.45f * rng.unit());
                if (s.leaf == leaf_shape::blade || s.leaf == leaf_shape::lanceolate)
                {
                    angle = tip_leaf ? twig_angle : twig_angle + side * (0.9f + 0.5f * rng.unit());
                }
                const float size_curve = 0.75f + 0.35f * sin(pi * min(t, 0.95f));
                const float length     = leaf_len * size_curve * (0.85f + 0.3f * rng.unit());
                const float width      = 0.65f + 0.38f * rng.unit();

                if (blossoms && rng.unit() < 0.65f)
                {
                    const uint32_t cluster = 3 + (rng.state % 3);
                    for (uint32_t f = 0; f < cluster; f++)
                    {
                        const float spread = leaf_len * 0.35f;
                        const Vector2 c(position.x + cos(angle) * spread * (0.5f + rng.unit()) + rng.signed_unit() * spread * 0.4f, position.y + sin(angle) * spread * (0.5f + rng.unit()) + rng.signed_unit() * spread * 0.4f);
                        draw_stick(a, frame, position, c, twig_radius * 0.3f, twig_radius * 0.2f, mix(twig, s.leaf_color, 0.5f), 0.80f, layer_ao);
                        draw_blossom(a, frame, c, leaf_len * 0.22f * (0.8f + 0.4f * rng.unit()), rng.unit() * pi, s.flower_color * (0.9f + 0.15f * rng.unit()));
                    }
                    continue;
                }

                const bool underside = rng.unit() < 0.26f;
                leaf_paint paint;
                paint.color     = (underside ? mix(leaf_color_for(s, time, rng), s.leaf_color_back, 0.55f) : leaf_color_for(s, time, rng)) * layer_br;
                paint.vein      = mix(paint.color, s.leaf_color_back * 1.2f * layer_br, 0.5f);
                paint.curl      = s.leaf == leaf_shape::palmate ? 0.12f : 0.16f;
                paint.arch      = 0.05f * rng.signed_unit();
                paint.roughness = underside ? 0.88f : 0.72f;
                paint.occlusion = layer_ao;
                paint.seed      = rng.state;

                const Vector2 petiole_end(position.x + cos(angle) * length * 0.08f, position.y + sin(angle) * length * 0.08f);
                draw_leaf(a, frame, s.leaf, petiole_end, angle, length, width, paint);
            }
        }

        // Bleed opaque leaf colors/normals/pbr outward into transparent pixels so mips never pull dark fringes
        void dilate_atlas_edges(atlas& a, const uint32_t passes)
        {
            const uint32_t sz = a.size;
            vector<uint8_t> valid(static_cast<size_t>(sz) * sz, 0);
            for (size_t i = 0; i < a.alpha.size(); i++)
            {
                if (a.alpha[i] > 0.08f)
                {
                    valid[i] = 1;
                }
            }

            vector<uint8_t> next_valid = valid;
            vector<float> next_color   = a.color;
            vector<float> next_normal  = a.normal;
            vector<float> next_rough   = a.roughness;
            vector<float> next_occ     = a.occlusion;

            for (uint32_t pass = 0; pass < passes; pass++)
            {
                ThreadPool::ParallelLoop([&](const uint32_t y_begin, const uint32_t y_end)
                {
                    for (uint32_t y = y_begin; y < y_end; y++)
                    {
                        const uint32_t tile_y0 = (y / a.tile_size) * a.tile_size;
                        const uint32_t tile_y1 = tile_y0 + a.tile_size;
                        for (uint32_t x = 0; x < sz; x++)
                        {
                            const size_t idx = static_cast<size_t>(y) * sz + x;
                            if (valid[idx])
                            {
                                continue;
                            }
                            const uint32_t tile_x0 = (x / a.tile_size) * a.tile_size;
                            const uint32_t tile_x1 = tile_x0 + a.tile_size;
                            float r = 0.0f, g = 0.0f, b = 0.0f, nx = 0.0f, ny = 0.0f, ro = 0.0f, oc = 0.0f, w_sum = 0.0f;
                            for (int32_t oy = -1; oy <= 1; oy++)
                            {
                                const int32_t ny_i = static_cast<int32_t>(y) + oy;
                                if (ny_i < static_cast<int32_t>(tile_y0) || ny_i >= static_cast<int32_t>(tile_y1))
                                {
                                    continue;
                                }
                                for (int32_t ox = -1; ox <= 1; ox++)
                                {
                                    if (ox == 0 && oy == 0)
                                    {
                                        continue;
                                    }
                                    const int32_t nx_i = static_cast<int32_t>(x) + ox;
                                    if (nx_i < static_cast<int32_t>(tile_x0) || nx_i >= static_cast<int32_t>(tile_x1))
                                    {
                                        continue;
                                    }
                                    const size_t n_idx = static_cast<size_t>(ny_i) * sz + static_cast<size_t>(nx_i);
                                    if (!valid[n_idx])
                                    {
                                        continue;
                                    }
                                    const float w = (ox == 0 || oy == 0) ? 1.0f : 0.707f;
                                    r     += a.color[n_idx * 3 + 0] * w;
                                    g     += a.color[n_idx * 3 + 1] * w;
                                    b     += a.color[n_idx * 3 + 2] * w;
                                    nx    += a.normal[n_idx * 2 + 0] * w;
                                    ny    += a.normal[n_idx * 2 + 1] * w;
                                    ro    += a.roughness[n_idx] * w;
                                    oc    += a.occlusion[n_idx] * w;
                                    w_sum += w;
                                }
                            }
                            if (w_sum > 0.0f)
                            {
                                const float inv = 1.0f / w_sum;
                                next_color[idx * 3 + 0]  = r * inv;
                                next_color[idx * 3 + 1]  = g * inv;
                                next_color[idx * 3 + 2]  = b * inv;
                                next_normal[idx * 2 + 0] = nx * inv;
                                next_normal[idx * 2 + 1] = ny * inv;
                                next_rough[idx]          = ro * inv;
                                next_occ[idx]            = oc * inv;
                                next_valid[idx]          = 1;
                            }
                        }
                    }
                }, sz);
                a.color.swap(next_color);
                a.normal.swap(next_normal);
                a.roughness.swap(next_rough);
                a.occlusion.swap(next_occ);
                valid.swap(next_valid);
                next_color  = a.color;
                next_normal = a.normal;
                next_rough  = a.roughness;
                next_occ    = a.occlusion;
                next_valid  = valid;
            }
        }

        void paint_atlas(const species& s, const season time, const uint32_t size, vector<uint8_t>& albedo, vector<uint8_t>& normal, vector<uint8_t>& roughness, vector<uint8_t>& occlusion)
        {
            atlas a;
            a.size      = size;
            a.tile_size = size / atlas_tiles;
            const size_t texels = static_cast<size_t>(size) * size;
            a.color.assign(texels * 3, 0.0f);
            a.alpha.assign(texels, 0.0f);
            a.normal.assign(texels * 2, 0.0f);
            a.roughness.assign(texels, 0.84f);
            a.occlusion.assign(texels, 0.85f);

            const bool needle       = s.leaf == leaf_shape::needle;
            const float width_ratio = needle ? 0.55f : (s.leaf == leaf_shape::scale ? 0.7f : 0.85f);
            const uint32_t total_tiles = atlas_tiles * atlas_tiles;

            // Each tile writes to a disjoint quadrant of the atlas, so all 4 variants paint in parallel
            ThreadPool::ParallelLoop([&](const uint32_t begin, const uint32_t end)
            {
                for (uint32_t tile = begin; tile < end; tile++)
                {
                    tile_frame frame;
                    frame.ox       = (tile % atlas_tiles) * a.tile_size;
                    frame.oy       = (tile / atlas_tiles) * a.tile_size;
                    frame.height_m = s.fronds > 0 ? s.frond_length : s.card_size;
                    frame.width_m  = s.fronds > 0 ? s.frond_length * 0.4f : s.card_size * width_ratio;
                    paint_twig(a, frame, s, time, tile);
                }
            }, total_tiles);

            // Blend a subtle macro spray-dome normal across each tile so whole sprays shade volumetrically
            ThreadPool::ParallelLoop([&](const uint32_t tile_begin, const uint32_t tile_end)
            {
                const float ts = static_cast<float>(a.tile_size);
                for (uint32_t tile = tile_begin; tile < tile_end; tile++)
                {
                    const uint32_t ox = (tile % atlas_tiles) * a.tile_size;
                    const uint32_t oy = (tile / atlas_tiles) * a.tile_size;
                    for (uint32_t y = 0; y < a.tile_size; y++)
                    {
                        const float dy = ((static_cast<float>(y) + 0.5f) / ts - 0.52f) * 2.0f;
                        for (uint32_t x = 0; x < a.tile_size; x++)
                        {
                            const size_t idx = static_cast<size_t>(oy + y) * a.size + (ox + x);
                            if (a.alpha[idx] <= 0.01f)
                            {
                                continue;
                            }
                            const float dx = ((static_cast<float>(x) + 0.5f) / ts - 0.5f) * 2.0f;
                            a.normal[idx * 2 + 0] = a.normal[idx * 2 + 0] * 0.72f + clamp(dx * 0.55f, -0.55f, 0.55f) * 0.28f;
                            a.normal[idx * 2 + 1] = a.normal[idx * 2 + 1] * 0.72f + clamp(-dy * 0.45f, -0.45f, 0.45f) * 0.28f;
                        }
                    }
                }
            }, total_tiles);

            // Compute fallback average color for any deep transparent background pixels beyond the dilation band
            Vector3 average(0.0f, 0.0f, 0.0f);
            float avg_rough = 0.0f;
            float avg_occ   = 0.0f;
            float weight    = 0.0f;
            for (size_t i = 0; i < texels; i++)
            {
                const float al = a.alpha[i];
                if (al > 0.0f)
                {
                    average   += Vector3(a.color[i * 3], a.color[i * 3 + 1], a.color[i * 3 + 2]) * al;
                    avg_rough += a.roughness[i] * al;
                    avg_occ   += a.occlusion[i] * al;
                    weight    += al;
                }
            }
            average   = weight > 0.0f ? average / weight : s.leaf_color;
            avg_rough = weight > 0.0f ? avg_rough / weight : 0.84f;
            avg_occ   = weight > 0.0f ? avg_occ / weight : 0.85f;

            // Dilate opaque border colors/normals/pbr 10 texels outward to prevent any dark mipmap fringes
            dilate_atlas_edges(a, 10u);

            albedo.resize(texels * 4);
            normal.resize(texels * 4);
            roughness.resize(texels * 4);
            occlusion.resize(texels * 4);
            ThreadPool::ParallelLoop([&](const uint32_t y_begin, const uint32_t y_end)
            {
                for (uint32_t y = y_begin; y < y_end; y++)
                {
                    for (uint32_t x = 0; x < size; x++)
                    {
                        const size_t i    = static_cast<size_t>(y) * size + x;
                        const float alpha = a.alpha[i];
                        Vector3 color(a.color[i * 3 + 0], a.color[i * 3 + 1], a.color[i * 3 + 2]);
                        const bool has_color = (color.x + color.y + color.z) > 1e-5f;
                        if (!has_color)
                        {
                            color = average;
                        }
                        albedo[i * 4 + 0] = to_srgb8(color.x);
                        albedo[i * 4 + 1] = to_srgb8(color.y);
                        albedo[i * 4 + 2] = to_srgb8(color.z);
                        albedo[i * 4 + 3] = to_unorm8(alpha);

                        const float nx  = clamp(a.normal[i * 2 + 0], -0.48f, 0.48f);
                        const float ny  = clamp(a.normal[i * 2 + 1], -0.48f, 0.48f);
                        const Vector3 n = Vector3(nx, ny, 1.0f).Normalized();
                        normal[i * 4 + 0] = to_unorm8(n.x * 0.5f + 0.5f);
                        normal[i * 4 + 1] = to_unorm8(-n.y * 0.5f + 0.5f);
                        normal[i * 4 + 2] = to_unorm8(n.z * 0.5f + 0.5f);
                        normal[i * 4 + 3] = 255;

                        const uint8_t ro = to_unorm8(has_color ? a.roughness[i] : avg_rough);
                        roughness[i * 4 + 0] = roughness[i * 4 + 1] = roughness[i * 4 + 2] = ro;
                        roughness[i * 4 + 3] = 255;

                        const uint8_t oc = to_unorm8(has_color ? a.occlusion[i] : avg_occ);
                        occlusion[i * 4 + 0] = occlusion[i * 4 + 1] = occlusion[i * 4 + 2] = oc;
                        occlusion[i * 4 + 3] = 255;
                    }
                }
            }, size);
        }
    }

    void generate_textures(const species& s, const season time, texture_set& output)
    {
        output.bark_size = 1024;
        output.leaf_size = 2048;
        paint_bark(s, output.bark_size, output.bark_albedo, output.bark_normal, output.bark_roughness, output.bark_occlusion);
        paint_atlas(s, time, output.leaf_size, output.leaf_albedo, output.leaf_normal, output.leaf_roughness, output.leaf_occlusion);
    }

    bool ensure_textures(const species& s, const season time, const string& directory, texture_paths& paths)
    {
        char hash_text[32];
        snprintf(hash_text, sizeof(hash_text), "%016llx", static_cast<unsigned long long>(texture_hash(s, time)));
        const string stem    = directory + (directory.empty() || directory.back() == '/' ? "" : "/") + s.id + "_" + to_string(time) + "_" + texture_version_tag + "_" + hash_text;
        paths.bark_albedo    = stem + "_bark.png";
        paths.bark_normal    = stem + "_bark_normal.png";
        paths.bark_roughness = stem + "_bark_roughness.png";
        paths.bark_occlusion = stem + "_bark_occlusion.png";
        paths.leaf_albedo    = stem + "_leaf.png";
        paths.leaf_normal    = stem + "_leaf_normal.png";
        paths.leaf_roughness = stem + "_leaf_roughness.png";
        paths.leaf_occlusion = stem + "_leaf_occlusion.png";

        if (FileSystem::Exists(paths.bark_albedo) && FileSystem::Exists(paths.bark_normal) && FileSystem::Exists(paths.bark_roughness) && FileSystem::Exists(paths.bark_occlusion) &&
            FileSystem::Exists(paths.leaf_albedo) && FileSystem::Exists(paths.leaf_normal) && FileSystem::Exists(paths.leaf_roughness) && FileSystem::Exists(paths.leaf_occlusion))
        {
            return true;
        }

        FileSystem::CreateDirectory_(directory);
        texture_set set;
        generate_textures(s, time, set);
        ImageImporter::SaveSdrRgba8(paths.bark_albedo, set.bark_size, set.bark_size, set.bark_albedo.data());
        ImageImporter::SaveSdrRgba8(paths.bark_normal, set.bark_size, set.bark_size, set.bark_normal.data());
        ImageImporter::SaveSdrRgba8(paths.bark_roughness, set.bark_size, set.bark_size, set.bark_roughness.data());
        ImageImporter::SaveSdrRgba8(paths.bark_occlusion, set.bark_size, set.bark_size, set.bark_occlusion.data());
        ImageImporter::SaveSdrRgba8(paths.leaf_albedo, set.leaf_size, set.leaf_size, set.leaf_albedo.data());
        ImageImporter::SaveSdrRgba8(paths.leaf_normal, set.leaf_size, set.leaf_size, set.leaf_normal.data());
        ImageImporter::SaveSdrRgba8(paths.leaf_roughness, set.leaf_size, set.leaf_size, set.leaf_roughness.data());
        ImageImporter::SaveSdrRgba8(paths.leaf_occlusion, set.leaf_size, set.leaf_size, set.leaf_occlusion.data());
        return FileSystem::Exists(paths.leaf_albedo) && FileSystem::Exists(paths.leaf_occlusion);
    }
}
