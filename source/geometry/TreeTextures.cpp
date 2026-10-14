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
            float f1 = 1e9f;
            float f2 = 1e9f;
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
            for (uint32_t y = 0; y < size; y++)
            {
                for (uint32_t x = 0; x < size; x++)
                {
                    auto h = [&](const int32_t ix, const int32_t iy)
                    {
                        return height[static_cast<size_t>(wrap(iy, size)) * size + wrap(ix, size)];
                    };
                    const float dx = (h(x + 1, y) - h(x - 1, y)) * strength;
                    const float dy = (h(x, y + 1) - h(x, y - 1)) * strength;
                    const Vector3 n = Vector3(-dx, dy, 1.0f).Normalized();
                    uint8_t* p = &output[(static_cast<size_t>(y) * size + x) * 4];
                    p[0] = to_unorm8(n.x * 0.5f + 0.5f);
                    p[1] = to_unorm8(n.y * 0.5f + 0.5f);
                    p[2] = to_unorm8(n.z * 0.5f + 0.5f);
                    p[3] = 255;
                }
            }
        }

        void paint_bark(const species& s, const uint32_t size, vector<uint8_t>& albedo, vector<uint8_t>& normal, vector<uint8_t>& roughness)
        {
            const uint32_t seed = static_cast<uint32_t>(texture_hash(s, season::summer) & 0xffffffffu);
            vector<float> height(static_cast<size_t>(size) * size, 0.0f);
            albedo.resize(static_cast<size_t>(size) * size * 4);
            roughness.resize(albedo.size());
            const Vector3 light = s.bark_color;
            const Vector3 dark  = s.bark_color_dark;
            float strength      = 4.0f;

            for (uint32_t y = 0; y < size; y++)
            {
                for (uint32_t x = 0; x < size; x++)
                {
                    const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(size);
                    const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(size);
                    const float fine  = fbm(u, v, 32, 32, 3, seed + 900u);
                    float h           = 0.5f;
                    Vector3 color     = light;

                    switch (s.bark)
                    {
                        case bark_style::furrowed:
                        {
                            const float warp   = fbm(u, v, 4, 2, 3, seed + 5u) - 0.5f;
                            const float n      = fbm(u + warp * 0.08f, v, 7, 2, 5, seed);
                            const float ridge  = 1.0f - abs(n * 2.0f - 1.0f);
                            const cell cracks  = worley(u, v, 6, 3, seed + 31u, 0.5f);
                            const float breaks = smoothstep(0.0f, 0.08f, cracks.f2 - cracks.f1);
                            h     = pow(saturate(ridge), 1.6f) * (0.55f + 0.45f * breaks);
                            color = mix(dark, light, smoothstep(0.15f, 0.75f, h)) * (0.85f + 0.3f * fine);
                            strength = 6.0f;
                            break;
                        }
                        case bark_style::plated:
                        {
                            // Narrow, vertically broken scales rather than large polygonal paving stones.
                            const float warp = fbm(u, v, 4, 3, 4, seed + 3u) - 0.5f;
                            const float grain = fbm(u + warp * 0.035f, v, 48, 5, 4, seed + 17u);
                            const cell plates = worley(u + warp * 0.04f, v, 18, 7, seed, 0.65f);
                            const float gap = smoothstep(0.008f, 0.085f, plates.f2 - plates.f1);
                            const float flake = fbm(u, v, 38, 18, 4, seed + 77u);
                            h = gap * 0.55f + grain * 0.3f + flake * 0.15f;
                            color = mix(dark, light, 0.25f + 0.65f * gap) * (0.72f + grain * 0.35f + flake * 0.2f);
                            strength = 8.0f;
                            break;
                        }
                        case bark_style::smooth:
                        {
                            const float mottle    = fbm(u, v, 3, 3, 5, seed);
                            const float lenticel  = value_noise(u * 6.0f, v * 64.0f, 6, 64, seed + 11u);
                            const float dash      = smoothstep(0.82f, 0.9f, lenticel) * smoothstep(0.4f, 0.6f, fbm(u, v, 24, 4, 2, seed + 12u));
                            h     = 0.5f + (mottle - 0.5f) * 0.25f - dash * 0.25f;
                            color = mix(light, dark, saturate((mottle - 0.45f) * 0.9f)) * (0.93f + 0.14f * fine);
                            color = mix(color, dark * 0.8f, dash * 0.7f);
                            strength = 1.5f;
                            break;
                        }
                        case bark_style::birch:
                        {
                            const float lenticel = value_noise(u * 5.0f, v * 48.0f, 5, 48, seed + 11u);
                            const float dash     = smoothstep(0.78f, 0.86f, lenticel) * smoothstep(0.35f, 0.65f, fbm(u, v, 20, 3, 2, seed + 12u));
                            const float scar     = smoothstep(0.7f, 0.78f, fbm(u, v, 3, 2, 5, seed + 40u));
                            const float peel     = fbm(u, v, 6, 10, 4, seed + 50u);
                            h     = 0.6f - dash * 0.4f - scar * 0.5f + (peel - 0.5f) * 0.12f;
                            color = light * (0.9f + 0.15f * peel);
                            color = mix(color, dark, saturate(dash + scar));
                            strength = 2.5f;
                            break;
                        }
                        case bark_style::peeling:
                        {
                            const cell patch   = worley(u, v, 4, 3, seed, 0.9f);
                            const float tone   = static_cast<float>(patch.id & 1023) / 1023.0f;
                            const float edge   = smoothstep(0.0f, 0.06f, patch.f2 - patch.f1);
                            const Vector3 pale = mix(light, Vector3(0.62f, 0.60f, 0.48f), 0.5f);
                            const Vector3 tint = tone < 0.33f ? dark * 1.3f : (tone < 0.66f ? light : pale);
                            h     = 0.45f + 0.15f * edge + (fine - 0.5f) * 0.1f;
                            color = mix(dark, tint, 0.35f + 0.65f * edge) * (0.92f + 0.16f * fine);
                            strength = 2.5f;
                            break;
                        }
                        case bark_style::fibrous:
                        {
                            const float warp   = fbm(u, v, 3, 1, 3, seed + 9u) - 0.5f;
                            const float strand = fbm(u + warp * 0.1f, v, 22, 2, 4, seed);
                            const float ridge  = 1.0f - abs(strand * 2.0f - 1.0f);
                            h     = pow(saturate(ridge), 1.3f);
                            color = mix(dark, light, smoothstep(0.1f, 0.85f, h)) * (0.88f + 0.24f * fine);
                            strength = 5.0f;
                            break;
                        }
                        case bark_style::twisted:
                        {
                            // The mesh supplies the trunk twist; bark grain follows the stem, not a diagonal rope.
                            const float warp = fbm(u, v, 4, 2, 4, seed + 7u) - 0.5f;
                            const float strand = fbm(u + warp * 0.055f, v, 36, 3, 5, seed);
                            const float ridge = smoothstep(0.23f, 0.64f, strand);
                            const float weather = fbm(u, v, 9, 6, 4, seed + 60u);
                            h = ridge * 0.7f + fine * 0.18f + weather * 0.12f;
                            color = mix(dark, light, 0.2f + ridge * 0.65f) * (0.82f + fine * 0.3f);
                            color = mix(color, Vector3(0.32f, 0.31f, 0.28f), smoothstep(0.52f, 0.8f, weather) * 0.3f);
                            strength = 8.0f;
                            break;
                        }
                        case bark_style::ringed:
                        {
                            const float ring_pos = fmod(v * 3.0f + (fbm(u, v, 2, 1, 2, seed + 4u) - 0.5f) * 0.08f, 1.0f);
                            const float ring     = smoothstep(0.0f, 0.03f, ring_pos) * (1.0f - smoothstep(0.93f, 1.0f, ring_pos));
                            const float streak   = fbm(u, v, 18, 2, 3, seed);
                            h     = 0.4f + 0.4f * ring + (streak - 0.5f) * 0.15f;
                            color = mix(dark, light, 0.35f + 0.65f * ring) * (0.9f + 0.2f * streak);
                            strength = 4.0f;
                            break;
                        }
                    }

                    height[static_cast<size_t>(y) * size + x] = h;
                    uint8_t* p = &albedo[(static_cast<size_t>(y) * size + x) * 4];
                    p[0] = to_srgb8(color.x);
                    p[1] = to_srgb8(color.y);
                    p[2] = to_srgb8(color.z);
                    p[3] = 255;
                    // Dry fissures scatter more than the exposed, weathered ridges.
                    const uint8_t r = static_cast<uint8_t>(255.0f * clamp(s.bark_roughness + (0.5f - h) * 0.18f + (fine - 0.5f) * 0.12f, 0.55f, 1.0f));
                    uint8_t* rp = &roughness[(static_cast<size_t>(y) * size + x) * 4];
                    rp[0] = rp[1] = rp[2] = r;
                    rp[3] = 255;
                }
            }
            height_to_normal(height, size, strength, normal);
        }

        // the twig atlas, painted in physical units so leaves keep their proportions on a card that is not square
        struct atlas
        {
            uint32_t size      = 1024;
            uint32_t tile_size = 512;
            vector<float> color;  // linear rgb
            vector<float> alpha;
            vector<float> normal; // xy, z is derived
            vector<uint8_t> occupied_tile;
        };

        struct tile_frame
        {
            uint32_t ox     = 0;
            uint32_t oy     = 0;
            float width_m   = 0.4f; // physical width of the tile, x runs from -width/2 to width/2
            float height_m  = 0.5f; // physical height, y runs from 0 at the twig base to height at the top
        };

        // runs fn over the pixels whose centres fall in the physical box, fn returns coverage and fills colour and normal
        void raster(atlas& a, const tile_frame& frame, Vector2 lo, Vector2 hi, const function<float(const Vector2&, float, Vector3&, Vector2&)>& fn)
        {
            const float ts = static_cast<float>(a.tile_size);
            const float px_x = frame.width_m / ts;
            const float px_y = frame.height_m / ts;
            const float pixel_m = min(px_x, px_y);
            int32_t x0 = static_cast<int32_t>(floor((lo.x / frame.width_m + 0.5f) * ts)) - 1;
            int32_t x1 = static_cast<int32_t>(ceil((hi.x / frame.width_m + 0.5f) * ts)) + 1;
            int32_t y0 = static_cast<int32_t>(floor((1.0f - hi.y / frame.height_m) * ts)) - 1;
            int32_t y1 = static_cast<int32_t>(ceil((1.0f - lo.y / frame.height_m) * ts)) + 1;
            // a two texel gutter keeps mips from pulling the neighbour tile in
            x0 = max(x0, 2);
            y0 = max(y0, 2);
            x1 = min(x1, static_cast<int32_t>(a.tile_size) - 2);
            y1 = min(y1, static_cast<int32_t>(a.tile_size) - 2);
            for (int32_t y = y0; y < y1; y++)
            {
                for (int32_t x = x0; x < x1; x++)
                {
                    const Vector2 p(((static_cast<float>(x) + 0.5f) / ts - 0.5f) * frame.width_m, (1.0f - (static_cast<float>(y) + 0.5f) / ts) * frame.height_m);
                    Vector3 color;
                    Vector2 n(0.0f, 0.0f);
                    const float coverage = fn(p, pixel_m, color, n);
                    if (coverage <= 0.0f)
                    {
                        continue;
                    }
                    const size_t index = static_cast<size_t>(frame.oy + y) * a.size + frame.ox + x;
                    const float c      = saturate(coverage);
                    a.color[index * 3 + 0] = a.color[index * 3 + 0] * (1.0f - c) + color.x * c;
                    a.color[index * 3 + 1] = a.color[index * 3 + 1] * (1.0f - c) + color.y * c;
                    a.color[index * 3 + 2] = a.color[index * 3 + 2] * (1.0f - c) + color.z * c;
                    a.normal[index * 2 + 0] = a.normal[index * 2 + 0] * (1.0f - c) + n.x * c;
                    a.normal[index * 2 + 1] = a.normal[index * 2 + 1] * (1.0f - c) + n.y * c;
                    a.alpha[index] = max(a.alpha[index], c);
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

        void draw_stick(atlas& a, const tile_frame& frame, const Vector2& from, const Vector2& to, const float r0, const float r1, const Vector3& color)
        {
            const float r = max(r0, r1);
            const Vector2 lo(min(from.x, to.x) - r, min(from.y, to.y) - r);
            const Vector2 hi(max(from.x, to.x) + r, max(from.y, to.y) + r);
            Vector2 dir = to - from;
            const float len = max(sqrt(dir.x * dir.x + dir.y * dir.y), 1e-6f);
            dir = Vector2(dir.x / len, dir.y / len);
            const Vector2 across(-dir.y, dir.x);
            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, Vector3& out_color, Vector2& out_normal)
            {
                float t;
                const float d      = segment_distance(p, from, to, t);
                // thin twigs stay at least about a texel wide, sub texel lines alias into dotted ones
                const float radius = max(r0 + (r1 - r0) * t, pixel * 0.6f);
                const float cover  = saturate((radius - d) / pixel + 0.5f);
                const float side   = ((p.x - from.x) * across.x + (p.y - from.y) * across.y) / max(radius, 1e-6f);
                out_color  = color * (0.75f + 0.25f * sqrt(saturate(1.0f - side * side)));
                out_normal = Vector2(across.x * side * 0.6f, across.y * side * 0.6f);
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
                case leaf_shape::elliptic:   return 0.27f * pow(sin(pi * s), 0.85f);
                case leaf_shape::ovate:      return 0.32f * pow(sin(pi * pow(s, 0.75f)), 0.8f);
                case leaf_shape::lanceolate: return 0.10f * pow(sin(pi * s), 0.75f);
                case leaf_shape::round:      return 0.92f * sqrt(s * (1.0f - s));
                case leaf_shape::heart:      return 0.95f * sqrt(s * (1.0f - s)) * (1.15f - 0.45f * s);
                case leaf_shape::lobed:      return 0.33f * pow(sin(pi * s), 0.7f) * (0.68f + 0.32f * cos(2.0f * pi * 4.5f * s));
                case leaf_shape::blade:      return 0.075f * pow(sin(pi * s), 0.6f);
                case leaf_shape::pinnate:    return 0.07f * pow(sin(pi * s), 0.7f);
                default:                     return 0.25f * sin(pi * s);
            }
        }

        struct leaf_paint
        {
            Vector3 color;
            Vector3 vein;
            float curl = 0.35f;
        };

        // a leaf with its petiole at base, pointing along angle, len long, width scaled for foreshortening
        void draw_leaf(atlas& a, const tile_frame& frame, const leaf_shape shape, const Vector2& base, const float angle, const float len, const float width_scale, const leaf_paint& paint)
        {
            const Vector2 axis(cos(angle), sin(angle));
            const Vector2 across(-axis.y, axis.x);
            const float reach = len * 1.05f;
            const Vector2 lo(base.x - reach, base.y - reach);
            const Vector2 hi(base.x + reach, base.y + reach);
            const bool palmate = shape == leaf_shape::palmate;

            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, Vector3& out_color, Vector2& out_normal)
            {
                const Vector2 d(p.x - base.x, p.y - base.y);
                const float x = (d.x * axis.x + d.y * axis.y) / len;
                const float y = (d.x * across.x + d.y * across.y) / (len * max(width_scale, 0.2f));

                float edge;   // positive inside, in leaf lengths
                float s_along;
                float y_rel;
                if (palmate)
                {
                    // five lobes around a centre a little up the midrib, the petiole side is cut away
                    const float cx    = x - 0.42f;
                    const float r     = sqrt(cx * cx + y * y);
                    const float phi   = atan2(y, cx);
                    const float lobe  = 0.56f * (0.5f + 0.5f * pow(abs(cos(2.5f * phi)), 0.7f)) * (1.0f + 0.035f * sin(phi * 28.0f));
                    const float cut   = abs(phi) > 2.6f ? 0.15f : 1.0f;
                    edge    = lobe * cut - r;
                    s_along = saturate(x);
                    y_rel   = r > 1e-4f ? y / max(lobe, 1e-3f) : 0.0f;
                }
                else
                {
                    // past either end there is no blade at all, a zero width there would still antialias into a hairline
                    const float w = leaf_half_width(shape, x);
                    edge    = w > 0.0f ? w - abs(y) : -1.0f;
                    s_along = x;
                    y_rel   = w > 1e-4f ? y / w : 0.0f;
                }

                const float edge_px = edge * len * max(width_scale, 0.2f) / pixel;
                // the petiole, a short stalk under the blade
                const float petiole_px = (0.012f * len - abs(d.x * across.x + d.y * across.y)) / pixel;
                const bool on_petiole  = x > -0.12f && x < 0.08f && petiole_px > -0.5f;
                float cover = saturate(edge_px + 0.5f);
                if (on_petiole)
                {
                    cover = max(cover, saturate(petiole_px + 0.5f));
                }
                if (cover <= 0.0f)
                {
                    return 0.0f;
                }

                // midrib, secondary veins pointing toward the tip, darker margin, lighter base
                const float midrib = 1.0f - smoothstep(0.0f, 0.06f, abs(y_rel));
                const float vein_phase = s_along * 9.0f - abs(y_rel) * 1.6f;
                const float vein = (1.0f - smoothstep(0.0f, 0.09f, abs(vein_phase - round(vein_phase)))) * smoothstep(0.08f, 0.25f, abs(y_rel)) * (1.0f - smoothstep(0.75f, 0.95f, abs(y_rel)));
                const float margin = smoothstep(0.7f, 1.0f, abs(y_rel));
                Vector3 c = paint.color * (0.88f + 0.18f * smoothstep(0.0f, 0.6f, s_along));
                c = mix(c, paint.vein, saturate(midrib * 0.55f + vein * 0.22f));
                c = c * (1.0f - margin * 0.18f);
                out_color = c;

                // the blade curls up from the midrib, the vein channels dent it a little
                const float tilt = y_rel * paint.curl - midrib * 0.15f * (y_rel >= 0.0f ? 1.0f : -1.0f);
                out_normal = Vector2(across.x * tilt, across.y * tilt);
                return cover;
            });
        }

        void draw_blossom(atlas& a, const tile_frame& frame, const Vector2& center, const float radius, const float rotation, const Vector3& color)
        {
            const Vector2 lo(center.x - radius, center.y - radius);
            const Vector2 hi(center.x + radius, center.y + radius);
            raster(a, frame, lo, hi, [&](const Vector2& p, const float pixel, Vector3& out_color, Vector2& out_normal)
            {
                const Vector2 d(p.x - center.x, p.y - center.y);
                const float r    = sqrt(d.x * d.x + d.y * d.y) / radius;
                const float phi  = atan2(d.y, d.x) + rotation;
                // five rounded petals with the small notch cherry petals have
                const float petal = 0.62f + 0.38f * pow(abs(cos(2.5f * phi)), 0.5f) - 0.08f * pow(abs(cos(5.0f * phi)), 8.0f);
                const float cover = saturate((petal - r) * radius / pixel + 0.5f);
                const float heart = 1.0f - smoothstep(0.12f, 0.3f, r);
                out_color  = mix(color * (0.85f + 0.2f * r), Vector3(0.85f, 0.75f, 0.3f), heart * 0.6f);
                out_normal = Vector2(d.x / radius * 0.3f, d.y / radius * 0.3f);
                return cover;
            });
        }

        // cypress and sequoia, fans of overlapping scale leaves that read as soft lumpy sprays
        void draw_scale_spray(atlas& a, const tile_frame& frame, const Vector2& from, const float angle, const float len, const uint32_t depth, const leaf_paint& paint, const uint32_t seed)
        {
            const Vector2 dir(cos(angle), sin(angle));
            const Vector2 to(from.x + dir.x * len, from.y + dir.y * len);
            const float thickness = len * 0.07f;
            const uint32_t scales = 10;
            for (uint32_t i = 0; i < scales; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(scales);
                const Vector2 p(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
                leaf_paint shaded = paint;
                shaded.color = paint.color * (0.85f + 0.3f * hash_unit(static_cast<int32_t>(i), static_cast<int32_t>(depth), seed));
                draw_leaf(a, frame, leaf_shape::ovate, p, angle + (i % 2 ? 0.5f : -0.5f), thickness * 2.6f * (1.0f - t * 0.4f), 1.0f, shaded);
            }
            if (depth == 0)
            {
                return;
            }
            const uint32_t children = 3;
            for (uint32_t i = 0; i < children; i++)
            {
                const float t    = 0.3f + 0.22f * static_cast<float>(i);
                const Vector2 p(from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t);
                const float side = (i % 2 ? 1.0f : -1.0f) * (0.55f + 0.3f * hash_unit(static_cast<int32_t>(i), static_cast<int32_t>(depth) + 9, seed));
                draw_scale_spray(a, frame, p, angle + side, len * 0.5f, depth - 1, paint, seed + i * 31u + 7u);
            }
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
            const Vector3 hue = Vector3(1.0f + var * 0.5f * rng.signed_unit(), 1.0f, 1.0f + var * 0.5f * rng.signed_unit());
            return Vector3(base.x * hue.x, base.y * hue.y, base.z * hue.z) * value;
        }

        // one twig variant, the twig grows from the bottom centre of its tile up to near the top
        void paint_twig(atlas& a, const tile_frame& frame, const species& s, const season time, const uint32_t variant)
        {
            paint_rng rng = { static_cast<uint32_t>(texture_hash(s, time)) ^ (variant * 0x9e3779b9u) };
            const bool bare        = time == season::bare && !s.evergreen;
            const bool blossoms    = s.flowers && time == season::spring;
            const Vector3 twig     = s.bark_color * 0.85f;
            const float w          = frame.width_m;
            const float h          = frame.height_m;
            const float leaf_len   = s.leaf_size;

            // the main twig, a gentle random curve
            const uint32_t knots = 6;
            vector<Vector2> path;
            float x_offset = 0.0f;
            for (uint32_t i = 0; i <= knots; i++)
            {
                const float t = static_cast<float>(i) / static_cast<float>(knots);
                x_offset += rng.signed_unit() * w * 0.025f;
                path.push_back(Vector2(x_offset * t, t * h * (s.leaf == leaf_shape::needle || s.leaf == leaf_shape::scale ? 0.92f : 0.8f)));
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
            const float twig_radius = max(0.0025f, s.card_size * 0.008f);

            if (s.fronds > 0)
            {
                // a palm frond, a rachis up the middle and leaflets that start stiff and droop toward the tip
                const float rachis_top = h * 0.98f;
                draw_stick(a, frame, Vector2(0.0f, 0.0f), Vector2(0.0f, rachis_top), w * 0.022f, w * 0.006f, mix(twig, s.leaf_color, 0.5f));
                const uint32_t pairs = 34;
                for (uint32_t i = 0; i < pairs; i++)
                {
                    const float t = 0.06f + 0.92f * static_cast<float>(i) / static_cast<float>(pairs);
                    const float y = t * rachis_top;
                    const float len = w * 0.5f * pow(sin(pi * min(1.0f, 0.15f + t * 0.95f)), 0.5f) * (0.85f + 0.3f * rng.unit());
                    for (int32_t side = -1; side <= 1; side += 2)
                    {
                        // leaflets point up and out, flatter toward the tip of the frond
                        const float rise  = 0.55f - 0.25f * t + 0.1f * rng.signed_unit();
                        const float angle = side < 0 ? pi - rise : rise;
                        leaf_paint paint;
                        paint.color = leaf_color_for(s, time, rng);
                        paint.vein  = mix(paint.color, s.leaf_color_back, 0.6f);
                        paint.curl  = 0.25f;
                        draw_leaf(a, frame, leaf_shape::lanceolate, Vector2(0.0f, y), angle, len, 0.8f, paint);
                    }
                }
                return;
            }

            if (s.leaf == leaf_shape::needle)
            {
                // a spray, side shoots alternate off the main twig and every shoot carries needles both sides
                struct shoot
                {
                    Vector2 from;
                    Vector2 to;
                };
                vector<shoot> shoots;
                for (uint32_t i = 0; i + 1 < path.size(); i++)
                {
                    shoots.push_back({ path[i], path[i + 1] });
                }
                const uint32_t side_count = 8;
                for (uint32_t i = 0; i < side_count; i++)
                {
                    Vector2 position, direction;
                    along(0.08f + 0.78f * static_cast<float>(i) / static_cast<float>(side_count), position, direction);
                    const float side  = i % 2 ? 1.0f : -1.0f;
                    const float angle = atan2(direction.y, direction.x) + side * (0.8f + 0.2f * rng.signed_unit());
                    float length      = h * 0.42f * (1.0f - 0.55f * static_cast<float>(i) / static_cast<float>(side_count));
                    // the shoot and its needles stay inside the tile, a cut at the tile edge shows as a straight line
                    const float room = w * 0.47f - leaf_len - abs(position.x);
                    if (abs(cos(angle)) * length > room)
                    {
                        length = max(room, 0.0f) / max(abs(cos(angle)), 1e-3f);
                    }
                    shoots.push_back({ position, Vector2(position.x + cos(angle) * length, position.y + sin(angle) * length) });
                }
                for (const shoot& sh : shoots)
                {
                    draw_stick(a, frame, sh.from, sh.to, twig_radius, twig_radius * 0.5f, twig);
                }
                // needles every few millimetres, a spray reads as a soft mass rather than as a comb
                // short fir needles are a millimetre or two wide and sit almost touching, long pine needles are thinner for
                // their length, a floor in metres keeps both readable once the card is a few pixels across
                const float pixel_m       = frame.width_m / static_cast<float>(a.tile_size);
                const float needle_step   = max(leaf_len * 0.07f, max(0.0012f, pixel_m * 1.2f));
                const float needle_radius = max(max(leaf_len * 0.014f, 0.0009f), pixel_m * 0.7f);
                for (const shoot& sh : shoots)
                {
                    const Vector2 d = sh.to - sh.from;
                    const float base_angle = atan2(d.y, d.x);
                    const float shoot_len  = sqrt(d.x * d.x + d.y * d.y);
                    const uint32_t per_shoot = clamp(static_cast<uint32_t>(shoot_len / needle_step), 4u, 220u);
                    for (uint32_t i = 0; i < per_shoot; i++)
                    {
                        const float t = static_cast<float>(i) / static_cast<float>(per_shoot);
                        const Vector2 p(sh.from.x + d.x * t, sh.from.y + d.y * t);
                        for (int32_t side = -1; side <= 1; side += 2)
                        {
                            const float angle = base_angle + static_cast<float>(side) * (0.75f + 0.3f * rng.signed_unit());
                            const Vector3 color = leaf_color_for(s, time, rng) * (0.8f + 0.35f * t);
                            draw_stick(a, frame, p, Vector2(p.x + cos(angle) * leaf_len, p.y + sin(angle) * leaf_len), needle_radius, needle_radius * 0.45f, color);
                        }
                    }
                }
                return;
            }

            if (s.leaf == leaf_shape::scale)
            {
                leaf_paint paint;
                paint.color = leaf_color_for(s, time, rng);
                paint.vein  = paint.color * 1.1f;
                paint.curl  = 0.5f;
                draw_stick(a, frame, path.front(), path.back(), twig_radius, twig_radius * 0.4f, twig);
                for (uint32_t i = 0; i < 4; i++)
                {
                    Vector2 position, direction;
                    along(0.1f + 0.22f * static_cast<float>(i), position, direction);
                    const float side = i % 2 ? 1.0f : -1.0f;
                    paint.color = leaf_color_for(s, time, rng);
                    draw_scale_spray(a, frame, position, atan2(direction.y, direction.x) + side * 0.5f, h * (0.6f - 0.1f * static_cast<float>(i)), 2, paint, rng.state + i);
                }
                paint.color = leaf_color_for(s, time, rng);
                draw_scale_spray(a, frame, path.back(), pi * 0.5f, h * 0.2f, 1, paint, rng.state);
                return;
            }

            // broadleaves, bamboo and acacia: the twig, then side twigs when bare, then leaves or blossoms
            for (uint32_t i = 0; i + 1 < path.size(); i++)
            {
                const float t0 = static_cast<float>(i) / static_cast<float>(path.size() - 1);
                const float t1 = static_cast<float>(i + 1) / static_cast<float>(path.size() - 1);
                draw_stick(a, frame, path[i], path[i + 1], twig_radius * (1.0f - 0.6f * t0), twig_radius * (1.0f - 0.6f * t1), twig);
            }

            if (bare)
            {
                // winter twigs fork twice, the buds at the tips are what make a bare crown read as alive
                function<void(Vector2, float, float, uint32_t)> fork = [&](const Vector2 from, const float angle, const float len, const uint32_t depth)
                {
                    const Vector2 to(from.x + cos(angle) * len, from.y + sin(angle) * len);
                    draw_stick(a, frame, from, to, twig_radius * (0.45f + 0.2f * depth), twig_radius * 0.3f, twig);
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

            // a spray, the main twig and alternating side twigs, so one card carries a whole leafy shoot
            struct shoot
            {
                Vector2 from;
                Vector2 to;
                float length = 0.0f;
            };
            vector<shoot> shoots;
            shoots.push_back({ path.front(), path.back(), h * 0.8f });
            const bool sprays    = s.leaf != leaf_shape::blade;
            const uint32_t sides = sprays ? 4 + variant % 2 : 0;
            for (uint32_t i = 0; i < sides; i++)
            {
                Vector2 position, direction;
                along(0.14f + 0.6f * (static_cast<float>(i) + 0.3f * rng.unit()) / static_cast<float>(sides), position, direction);
                const float side  = i % 2 ? 1.0f : -1.0f;
                const float angle = atan2(direction.y, direction.x) + side * (0.55f + 0.3f * rng.unit());
                float length      = h * (0.45f - 0.22f * static_cast<float>(i) / static_cast<float>(sides)) * (0.85f + 0.3f * rng.unit());
                const float room  = w * 0.47f - leaf_len * 0.9f - abs(position.x);
                if (abs(cos(angle)) * length > room)
                {
                    length = max(room, 0.0f) / max(abs(cos(angle)), 1e-3f);
                }
                if (length < leaf_len * 0.8f)
                {
                    continue;
                }
                const Vector2 end(position.x + cos(angle) * length, position.y + sin(angle) * length);
                draw_stick(a, frame, position, end, twig_radius * 0.55f, twig_radius * 0.3f, twig);
                shoots.push_back({ position, end, length });
            }
            float total_length = 0.0f;
            for (const shoot& sh : shoots)
            {
                total_length += sh.length;
            }

            // enough leaves to cover a good part of the card, a card that is mostly air needs ten more to fill a crown
            float leaf_area = 0.35f;
            switch (s.leaf)
            {
                case leaf_shape::lanceolate: leaf_area = 0.16f; break;
                case leaf_shape::blade:      leaf_area = 0.12f; break;
                case leaf_shape::ovate:      leaf_area = 0.42f; break;
                case leaf_shape::round:      leaf_area = 0.7f;  break;
                case leaf_shape::heart:      leaf_area = 0.72f; break;
                case leaf_shape::palmate:    leaf_area = 0.6f;  break;
                case leaf_shape::lobed:      leaf_area = 0.42f; break;
                case leaf_shape::pinnate:    leaf_area = 0.3f;  break;
                default: break;
            }
            const float coverage  = blossoms ? 0.55f : 0.75f;
            const float wanted    = coverage * w * h / max(leaf_len * leaf_len * leaf_area, 1e-6f);
            const uint32_t leaves = clamp(static_cast<uint32_t>(wanted), max(1u, s.leaves_per_card), 140u);

            for (uint32_t i = 0; i < leaves; i++)
            {
                // leaves go to shoots by length, each shoot ends in a leaf
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
                const shoot& host   = shoots[shoot_index];
                const float t       = tip_leaf ? 1.0f : 0.1f + 0.9f * pow(rng.unit(), 0.8f);
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
                // blades and compound leaves hang, broad leaves hold out from the twig
                if (s.leaf == leaf_shape::blade || s.leaf == leaf_shape::lanceolate)
                {
                    angle = tip_leaf ? twig_angle : twig_angle + side * (0.9f + 0.5f * rng.unit());
                }
                const float size_curve = 0.75f + 0.35f * sin(pi * min(t, 0.95f));
                const float length     = leaf_len * size_curve * (0.85f + 0.3f * rng.unit());
                const float width      = 0.6f + 0.4f * rng.unit(); // a leaf turned away from the card is foreshortened

                if (blossoms && rng.unit() < 0.7f)
                {
                    const uint32_t cluster = 3 + (rng.state % 3);
                    for (uint32_t f = 0; f < cluster; f++)
                    {
                        const float spread = leaf_len * 0.35f;
                        const Vector2 c(position.x + cos(angle) * spread * (0.5f + rng.unit()) + rng.signed_unit() * spread * 0.4f, position.y + sin(angle) * spread * (0.5f + rng.unit()) + rng.signed_unit() * spread * 0.4f);
                        draw_stick(a, frame, position, c, twig_radius * 0.3f, twig_radius * 0.2f, mix(twig, s.leaf_color, 0.5f));
                        draw_blossom(a, frame, c, leaf_len * 0.22f * (0.8f + 0.4f * rng.unit()), rng.unit() * pi, s.flower_color * (0.9f + 0.15f * rng.unit()));
                    }
                    continue;
                }

                leaf_paint paint;
                paint.color = leaf_color_for(s, time, rng);
                // about one leaf in four shows its paler underside
                if (rng.unit() < 0.25f)
                {
                    paint.color = mix(paint.color, s.leaf_color_back, 0.55f);
                }
                paint.vein = mix(paint.color, s.leaf_color_back * 1.2f, 0.5f);
                paint.curl = s.leaf == leaf_shape::palmate ? 0.2f : 0.4f;

                if (s.leaf == leaf_shape::pinnate)
                {
                    // a compound leaf, a rachis lined with leaflets, acacia and carob are built this way
                    const Vector2 end(position.x + cos(angle) * length, position.y + sin(angle) * length);
                    draw_stick(a, frame, position, end, twig_radius * 0.35f, twig_radius * 0.2f, mix(twig, paint.color, 0.5f));
                    const uint32_t leaflets = 9;
                    for (uint32_t l = 0; l < leaflets; l++)
                    {
                        const float lt = 0.15f + 0.85f * static_cast<float>(l) / static_cast<float>(leaflets);
                        const Vector2 lp(position.x + cos(angle) * length * lt, position.y + sin(angle) * length * lt);
                        for (int32_t sd = -1; sd <= 1; sd += 2)
                        {
                            draw_leaf(a, frame, leaf_shape::elliptic, lp, angle + static_cast<float>(sd) * 1.1f, length * 0.2f, 0.8f, paint);
                        }
                    }
                    continue;
                }

                const Vector2 petiole_end(position.x + cos(angle) * length * 0.08f, position.y + sin(angle) * length * 0.08f);
                draw_leaf(a, frame, s.leaf, petiole_end, angle, length, width, paint);
            }
        }

        void paint_atlas(const species& s, const season time, const uint32_t size, vector<uint8_t>& albedo, vector<uint8_t>& normal)
        {
            atlas a;
            a.size      = size;
            a.tile_size = size / atlas_tiles;
            a.color.assign(static_cast<size_t>(size) * size * 3, 0.0f);
            a.alpha.assign(static_cast<size_t>(size) * size, 0.0f);
            a.normal.assign(static_cast<size_t>(size) * size * 2, 0.0f);

            // the card the twig is painted for, matches the generator's card proportions
            const bool needle      = s.leaf == leaf_shape::needle;
            const float width_ratio = needle ? 0.55f : (s.leaf == leaf_shape::scale ? 0.7f : 0.85f);
            for (uint32_t tile = 0; tile < atlas_tiles * atlas_tiles; tile++)
            {
                tile_frame frame;
                frame.ox       = (tile % atlas_tiles) * a.tile_size;
                frame.oy       = (tile / atlas_tiles) * a.tile_size;
                frame.height_m = s.fronds > 0 ? s.frond_length : s.card_size;
                frame.width_m  = s.fronds > 0 ? s.frond_length * 0.4f : s.card_size * width_ratio;
                paint_twig(a, frame, s, time, tile);
            }

            // transparent texels take the average leaf colour, so mips and bilinear taps never pull in black fringes
            Vector3 average(0.0f, 0.0f, 0.0f);
            float weight = 0.0f;
            for (size_t i = 0; i < a.alpha.size(); i++)
            {
                average += Vector3(a.color[i * 3], a.color[i * 3 + 1], a.color[i * 3 + 2]) * a.alpha[i];
                weight  += a.alpha[i];
            }
            average = weight > 0.0f ? average / weight : s.leaf_color;

            albedo.resize(static_cast<size_t>(size) * size * 4);
            normal.resize(static_cast<size_t>(size) * size * 4);
            for (size_t i = 0; i < a.alpha.size(); i++)
            {
                const float alpha = a.alpha[i];
                Vector3 color(a.color[i * 3], a.color[i * 3 + 1], a.color[i * 3 + 2]);
                // the canvas starts black, coverage blending darkened the edge texels, undo that before the fill
                if (alpha > 0.0f && alpha < 1.0f)
                {
                    color = color / alpha;
                }
                color = alpha > 0.0f ? mix(average, color, saturate(alpha * 2.0f)) : average;
                albedo[i * 4 + 0] = to_srgb8(color.x);
                albedo[i * 4 + 1] = to_srgb8(color.y);
                albedo[i * 4 + 2] = to_srgb8(color.z);
                albedo[i * 4 + 3] = to_unorm8(alpha);

                const float nx = clamp(a.normal[i * 2 + 0], -0.9f, 0.9f);
                const float ny = clamp(a.normal[i * 2 + 1], -0.9f, 0.9f);
                const Vector3 n = Vector3(nx, ny, 1.0f).Normalized();
                normal[i * 4 + 0] = to_unorm8(n.x * 0.5f + 0.5f);
                normal[i * 4 + 1] = to_unorm8(-n.y * 0.5f + 0.5f);
                normal[i * 4 + 2] = to_unorm8(n.z * 0.5f + 0.5f);
                normal[i * 4 + 3] = 255;
            }
        }
    }

    void generate_textures(const species& s, const season time, texture_set& output)
    {
        output.bark_size = 512;
        output.leaf_size = 1024;
        paint_bark(s, output.bark_size, output.bark_albedo, output.bark_normal, output.bark_roughness);
        paint_atlas(s, time, output.leaf_size, output.leaf_albedo, output.leaf_normal);
    }

    bool ensure_textures(const species& s, const season time, const string& directory, texture_paths& paths)
    {
        char hash_text[32];
        snprintf(hash_text, sizeof(hash_text), "%016llx", static_cast<unsigned long long>(texture_hash(s, time)));
        const string stem = directory + (directory.empty() || directory.back() == '/' ? "" : "/") + s.id + "_" + to_string(time) + "_" + hash_text;
        paths.bark_albedo = stem + "_bark.png";
        paths.bark_normal = stem + "_bark_normal.png";
        paths.bark_roughness = stem + "_bark_roughness.png";
        paths.leaf_albedo = stem + "_leaf.png";
        paths.leaf_normal = stem + "_leaf_normal.png";

        if (FileSystem::Exists(paths.bark_albedo) && FileSystem::Exists(paths.bark_normal) && FileSystem::Exists(paths.bark_roughness) && FileSystem::Exists(paths.leaf_albedo) && FileSystem::Exists(paths.leaf_normal))
        {
            return true;
        }

        FileSystem::CreateDirectory_(directory);
        texture_set set;
        generate_textures(s, time, set);
        ImageImporter::SaveSdrRgba8(paths.bark_albedo, set.bark_size, set.bark_size, set.bark_albedo.data());
        ImageImporter::SaveSdrRgba8(paths.bark_normal, set.bark_size, set.bark_size, set.bark_normal.data());
        ImageImporter::SaveSdrRgba8(paths.bark_roughness, set.bark_size, set.bark_size, set.bark_roughness.data());
        ImageImporter::SaveSdrRgba8(paths.leaf_albedo, set.leaf_size, set.leaf_size, set.leaf_albedo.data());
        ImageImporter::SaveSdrRgba8(paths.leaf_normal, set.leaf_size, set.leaf_size, set.leaf_normal.data());
        return FileSystem::Exists(paths.leaf_albedo);
    }
}
