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

//= INCLUDES ====================
#include "pch.h"
#include "McpTextureKernel.h"
#include "../core/ThreadPool.h"
SP_WARNINGS_OFF
#include <freetype/freetype.h>
SP_WARNINGS_ON
//===============================

namespace spartan::mcp_texture_kernel
{
    namespace
    {
        inline float saturate(const float value)
        {
            return std::clamp(value, 0.0f, 1.0f);
        }

        inline float lerp(
            const float a,
            const float b,
            const float t
        )
        {
            return a + (b - a) * t;
        }

        inline float smooth_step(const float t)
        {
            const float clamped = saturate(t);
            return clamped * clamped * (3.0f - 2.0f * clamped);
        }

        inline float fractional(const float value)
        {
            return value - std::floor(value);
        }

        inline int wrap_index(const int value, const int period)
        {
            if (period <= 0)
            {
                return value;
            }

            const int wrapped = value % period;
            return wrapped < 0 ? wrapped + period : wrapped;
        }

        inline float hash(
            const int x,
            const int y,
            const uint32_t seed
        )
        {
            uint32_t value =
                static_cast<uint32_t>(x) * 374761393u +
                static_cast<uint32_t>(y) * 668265263u +
                seed * 2246822519u;
            value = (value ^ (value >> 13)) * 1274126177u;
            value = value ^ (value >> 16);
            return static_cast<float>(value & 0xFFFFFFu) /
                static_cast<float>(0xFFFFFF);
        }

        inline void gradient(
            const int x,
            const int y,
            const uint32_t seed,
            float& gradient_x,
            float& gradient_y
        )
        {
            const float angle =
                hash(x, y, seed) * 6.2831853f;
            gradient_x = std::cos(angle);
            gradient_y = std::sin(angle);
        }

        // value noise, the period keeps every octave tileable
        inline float value_noise(
            const float x,
            const float y,
            const int period_x,
            const int period_y,
            const uint32_t seed
        )
        {
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const float fx = smooth_step(x - static_cast<float>(x0));
            const float fy = smooth_step(y - static_cast<float>(y0));

            const int xa = wrap_index(x0, period_x);
            const int ya = wrap_index(y0, period_y);
            const int xb = wrap_index(x0 + 1, period_x);
            const int yb = wrap_index(y0 + 1, period_y);

            const float top = lerp(
                hash(xa, ya, seed),
                hash(xb, ya, seed),
                fx
            );
            const float bottom = lerp(
                hash(xa, yb, seed),
                hash(xb, yb, seed),
                fx
            );

            return lerp(top, bottom, fy);
        }

        inline float perlin_noise(
            const float x,
            const float y,
            const int period_x,
            const int period_y,
            const uint32_t seed
        )
        {
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            const float fx = x - static_cast<float>(x0);
            const float fy = y - static_cast<float>(y0);
            const float ux = smooth_step(fx);
            const float uy = smooth_step(fy);

            const int xa = wrap_index(x0, period_x);
            const int ya = wrap_index(y0, period_y);
            const int xb = wrap_index(x0 + 1, period_x);
            const int yb = wrap_index(y0 + 1, period_y);

            float gx = 0.0f;
            float gy = 0.0f;
            gradient(xa, ya, seed, gx, gy);
            const float dot_a = gx * fx + gy * fy;
            gradient(xb, ya, seed, gx, gy);
            const float dot_b = gx * (fx - 1.0f) + gy * fy;
            gradient(xa, yb, seed, gx, gy);
            const float dot_c = gx * fx + gy * (fy - 1.0f);
            gradient(xb, yb, seed, gx, gy);
            const float dot_d =
                gx * (fx - 1.0f) + gy * (fy - 1.0f);

            const float top    = lerp(dot_a, dot_b, ux);
            const float bottom = lerp(dot_c, dot_d, ux);
            return lerp(top, bottom, uy) * 0.5f + 0.5f;
        }

        inline float worley_noise(
            const float x,
            const float y,
            const int period_x,
            const int period_y,
            const uint32_t seed
        )
        {
            const int x0 = static_cast<int>(std::floor(x));
            const int y0 = static_cast<int>(std::floor(y));
            float nearest = 1.0f;

            for (int offset_y = -1; offset_y <= 1; offset_y++)
            {
                for (int offset_x = -1; offset_x <= 1; offset_x++)
                {
                    const int cell_x = x0 + offset_x;
                    const int cell_y = y0 + offset_y;
                    const int wrapped_x =
                        wrap_index(cell_x, period_x);
                    const int wrapped_y =
                        wrap_index(cell_y, period_y);
                    const float point_x =
                        static_cast<float>(cell_x) +
                        hash(wrapped_x, wrapped_y, seed);
                    const float point_y =
                        static_cast<float>(cell_y) +
                        hash(
                            wrapped_x,
                            wrapped_y,
                            seed + 7919u
                        );
                    const float delta_x = point_x - x;
                    const float delta_y = point_y - y;
                    const float distance = std::sqrt(
                        delta_x * delta_x + delta_y * delta_y
                    );
                    nearest = std::min(nearest, distance);
                }
            }

            return saturate(nearest);
        }

        inline float fbm(
            const layer& source,
            const float x,
            const float y,
            const uint32_t seed,
            const bool seamless
        )
        {
            const float base_frequency = std::max(
                0.25f,
                source.frequency
            );
            float amplitude    = 1.0f;
            float total        = 0.0f;
            float normalizer   = 0.0f;
            float frequency    = base_frequency;

            const uint32_t octaves = std::clamp(
                source.octaves,
                1u,
                8u
            );
            for (uint32_t octave = 0; octave < octaves; octave++)
            {
                const int period = seamless
                    ? std::max(1, static_cast<int>(
                        std::round(frequency)
                    ))
                    : 0;
                const float effective = seamless
                    ? static_cast<float>(period)
                    : frequency;

                float sample = 0.0f;
                switch (source.noise)
                {
                case noise_kind::perlin_fbm:
                    sample = perlin_noise(
                        x * effective,
                        y * effective,
                        period,
                        period,
                        seed + octave * 131u
                    );
                    break;
                case noise_kind::worley:
                    sample = worley_noise(
                        x * effective,
                        y * effective,
                        period,
                        period,
                        seed + octave * 131u
                    );
                    break;
                case noise_kind::ridged:
                    sample = 1.0f - std::abs(
                        perlin_noise(
                            x * effective,
                            y * effective,
                            period,
                            period,
                            seed + octave * 131u
                        ) * 2.0f - 1.0f
                    );
                    break;
                default:
                    sample = value_noise(
                        x * effective,
                        y * effective,
                        period,
                        period,
                        seed + octave * 131u
                    );
                    break;
                }

                total      += sample * amplitude;
                normalizer += amplitude;
                amplitude  *= std::clamp(source.gain, 0.05f, 0.95f);
                frequency  *= std::max(1.05f, source.lacunarity);
            }

            return normalizer > 0.0f ? total / normalizer : 0.0f;
        }

        inline float box_coverage(
            const float distance,
            const float softness
        )
        {
            if (softness <= 0.0f)
            {
                return distance <= 0.0f ? 1.0f : 0.0f;
            }

            return saturate(0.5f - distance / softness);
        }

        inline float polygon_distance(
            const std::vector<float>& points,
            const float x,
            const float y
        )
        {
            const size_t count = points.size() / 2;
            if (count < 3)
            {
                return 1.0f;
            }

            float distance = 1.0e6f;
            bool inside = false;
            for (size_t i = 0, j = count - 1; i < count; j = i++)
            {
                const float xi = points[i * 2];
                const float yi = points[i * 2 + 1];
                const float xj = points[j * 2];
                const float yj = points[j * 2 + 1];

                const float edge_x = xj - xi;
                const float edge_y = yj - yi;
                const float point_x = x - xi;
                const float point_y = y - yi;
                const float length_squared =
                    edge_x * edge_x + edge_y * edge_y;
                const float projection = length_squared > 0.0f
                    ? saturate(
                        (point_x * edge_x + point_y * edge_y) /
                        length_squared
                    )
                    : 0.0f;
                const float delta_x =
                    point_x - edge_x * projection;
                const float delta_y =
                    point_y - edge_y * projection;
                distance = std::min(
                    distance,
                    std::sqrt(
                        delta_x * delta_x + delta_y * delta_y
                    )
                );

                const bool crosses =
                    (yi > y) != (yj > y);
                if (
                    crosses &&
                    x <
                        (xj - xi) * (y - yi) /
                        (yj - yi + 1.0e-9f) + xi
                )
                {
                    inside = !inside;
                }
            }

            return inside ? -distance : distance;
        }

        struct sample_result
        {
            float value = 1.0f;
            float alpha = 1.0f;
        };

        inline sample_result evaluate(
            const layer& source,
            const float u,
            const float v,
            const uint32_t seed,
            const bool seamless,
            const float texel,
            const std::vector<float>& coverage_buffer,
            const uint32_t buffer_width,
            const uint32_t buffer_height
        )
        {
            sample_result output;
            switch (source.type)
            {
            case layer_type::fill:
            {
                output.value = 1.0f;
                break;
            }
            case layer_type::linear_gradient:
            {
                const float radians =
                    source.angle * 0.01745329f;
                const float direction_x = std::cos(radians);
                const float direction_y = std::sin(radians);
                const float projection =
                    (u - 0.5f) * direction_x +
                    (v - 0.5f) * direction_y;
                output.value = saturate(projection + 0.5f);
                break;
            }
            case layer_type::radial_gradient:
            {
                const float delta_x =
                    (u - source.x) / std::max(0.001f, source.width);
                const float delta_y =
                    (v - source.y) / std::max(0.001f, source.height);
                const float distance = std::sqrt(
                    delta_x * delta_x + delta_y * delta_y
                );
                output.value = saturate(distance);
                break;
            }
            case layer_type::noise:
            {
                float sample_u = u;
                float sample_v = v;
                if (source.warp > 0.0f)
                {
                    const float warp_x = value_noise(
                        u * 4.0f,
                        v * 4.0f,
                        seamless ? 4 : 0,
                        seamless ? 4 : 0,
                        seed + 5501u
                    );
                    const float warp_y = value_noise(
                        u * 4.0f,
                        v * 4.0f,
                        seamless ? 4 : 0,
                        seamless ? 4 : 0,
                        seed + 9377u
                    );
                    sample_u += (warp_x - 0.5f) * source.warp;
                    sample_v += (warp_y - 0.5f) * source.warp;
                }

                output.value = fbm(
                    source,
                    sample_u,
                    sample_v,
                    seed,
                    seamless
                );
                break;
            }
            case layer_type::checker:
            {
                const int cell_x = static_cast<int>(
                    std::floor(u * std::max(1.0f, source.count_x))
                );
                const int cell_y = static_cast<int>(
                    std::floor(v * std::max(1.0f, source.count_y))
                );
                output.value =
                    ((cell_x + cell_y) & 1) ? 1.0f : 0.0f;
                break;
            }
            case layer_type::stripes:
            {
                const float radians =
                    source.angle * 0.01745329f;
                const float projection =
                    u * std::cos(radians) +
                    v * std::sin(radians);
                const float phase = fractional(
                    projection * std::max(1.0f, source.count_x)
                );
                const float duty = std::clamp(
                    source.duty,
                    0.02f,
                    0.98f
                );
                const float softness = std::max(
                    texel * std::max(1.0f, source.count_x),
                    0.0005f
                );
                const float inside = std::min(
                    phase,
                    duty - phase
                );
                const float outside = std::min(
                    phase - duty,
                    1.0f - phase
                );
                const float distance = phase < duty
                    ? inside
                    : -outside;
                output.value = saturate(
                    distance / softness + 0.5f
                );
                break;
            }
            case layer_type::bricks:
            case layer_type::tiles:
            {
                const float rows = std::max(1.0f, source.count_y);
                const float columns = std::max(1.0f, source.count_x);
                const float row = v * rows;
                const int row_index = static_cast<int>(
                    std::floor(row)
                );
                const float shift = source.type == layer_type::bricks
                    ? static_cast<float>(row_index) * source.row_offset
                    : 0.0f;
                const float column = u * columns + shift;
                const int column_index = static_cast<int>(
                    std::floor(column)
                );

                const float local_x = fractional(column);
                const float local_y = fractional(row);
                const float mortar = std::clamp(
                    source.mortar,
                    0.0f,
                    0.45f
                );
                const float inside_x = std::min(
                    local_x,
                    1.0f - local_x
                );
                const float inside_y = std::min(
                    local_y,
                    1.0f - local_y
                );
                const float edge = std::min(inside_x, inside_y);
                const float softness = std::max(texel * columns, 0.0005f);
                float brick = saturate(
                    (edge - mortar * 0.5f) / softness
                );

                if (source.variation > 0.0f)
                {
                    const float tint = hash(
                        wrap_index(
                            column_index,
                            static_cast<int>(columns)
                        ),
                        wrap_index(
                            row_index,
                            static_cast<int>(rows)
                        ),
                        seed
                    );
                    brick *= 1.0f - source.variation * tint;
                }

                output.value = brick;
                break;
            }
            case layer_type::spots:
            {
                const float frequency = std::max(
                    1.0f,
                    source.frequency
                );
                const int period = seamless
                    ? static_cast<int>(std::round(frequency))
                    : 0;
                const float effective = seamless
                    ? static_cast<float>(std::max(1, period))
                    : frequency;
                const float distance = worley_noise(
                    u * effective,
                    v * effective,
                    period,
                    period,
                    seed
                );
                const float radius = std::clamp(
                    source.density * 0.5f,
                    0.02f,
                    0.9f
                );
                output.value = 1.0f - smooth_step(
                    (distance - radius * 0.5f) /
                    std::max(0.001f, radius * source.sharpness)
                );
                break;
            }
            case layer_type::scratches:
            {
                const float radians =
                    source.angle * 0.01745329f;
                const float rotated_u =
                    u * std::cos(radians) - v * std::sin(radians);
                const float rotated_v =
                    u * std::sin(radians) + v * std::cos(radians);
                layer stretched = source;
                stretched.noise = noise_kind::ridged;
                const float streak = fbm(
                    stretched,
                    rotated_u * 0.08f,
                    rotated_v,
                    seed,
                    false
                );
                const float threshold = 1.0f - std::clamp(
                    source.density,
                    0.01f,
                    1.0f
                ) * 0.35f;
                output.value = saturate(
                    (streak - threshold) /
                    std::max(0.001f, 1.0f - threshold)
                );
                break;
            }
            case layer_type::shape:
            {
                const float softness = texel * 1.5f;
                float distance = 1.0f;
                if (source.shape == shape_kind::ellipse)
                {
                    const float delta_x =
                        (u - source.x) /
                        std::max(0.001f, source.width * 0.5f);
                    const float delta_y =
                        (v - source.y) /
                        std::max(0.001f, source.height * 0.5f);
                    distance =
                        (std::sqrt(
                            delta_x * delta_x + delta_y * delta_y
                        ) - 1.0f) *
                        std::max(0.001f, source.width * 0.5f);
                }
                else if (source.shape == shape_kind::polygon)
                {
                    distance = polygon_distance(
                        source.points,
                        u,
                        v
                    );
                }
                else if (source.shape == shape_kind::line)
                {
                    const std::vector<float>& points = source.points;
                    if (points.size() >= 4)
                    {
                        const float edge_x = points[2] - points[0];
                        const float edge_y = points[3] - points[1];
                        const float point_x = u - points[0];
                        const float point_y = v - points[1];
                        const float length_squared =
                            edge_x * edge_x + edge_y * edge_y;
                        const float projection = length_squared > 0.0f
                            ? saturate(
                                (point_x * edge_x + point_y * edge_y) /
                                length_squared
                            )
                            : 0.0f;
                        const float delta_x =
                            point_x - edge_x * projection;
                        const float delta_y =
                            point_y - edge_y * projection;
                        distance =
                            std::sqrt(
                                delta_x * delta_x + delta_y * delta_y
                            ) -
                            std::max(0.001f, source.thickness * 0.5f);
                    }
                }
                else
                {
                    const float half_width =
                        std::max(0.001f, source.width * 0.5f);
                    const float half_height =
                        std::max(0.001f, source.height * 0.5f);
                    const float corner = std::clamp(
                        source.shape == shape_kind::rounded_rectangle
                            ? source.corner
                            : 0.0f,
                        0.0f,
                        std::min(half_width, half_height)
                    );
                    const float delta_x =
                        std::abs(u - source.x) - (half_width - corner);
                    const float delta_y =
                        std::abs(v - source.y) - (half_height - corner);
                    const float outside_x = std::max(delta_x, 0.0f);
                    const float outside_y = std::max(delta_y, 0.0f);
                    distance =
                        std::sqrt(
                            outside_x * outside_x +
                            outside_y * outside_y
                        ) +
                        std::min(std::max(delta_x, delta_y), 0.0f) -
                        corner;
                }

                if (source.thickness > 0.0f &&
                    source.shape != shape_kind::line)
                {
                    distance =
                        std::abs(distance) -
                        source.thickness * 0.5f;
                }

                output.alpha = box_coverage(distance, softness);
                output.value = 1.0f;
                break;
            }
            case layer_type::text:
            {
                if (
                    coverage_buffer.empty() ||
                    buffer_width == 0 ||
                    buffer_height == 0
                )
                {
                    output.alpha = 0.0f;
                    output.value = 0.0f;
                    break;
                }

                const float sample_x =
                    u * static_cast<float>(buffer_width) - 0.5f;
                const float sample_y =
                    v * static_cast<float>(buffer_height) - 0.5f;
                const int x0 = static_cast<int>(
                    std::floor(sample_x)
                );
                const int y0 = static_cast<int>(
                    std::floor(sample_y)
                );
                const float fx = sample_x - static_cast<float>(x0);
                const float fy = sample_y - static_cast<float>(y0);

                auto fetch = [&](const int x, const int y)
                {
                    if (
                        x < 0 ||
                        y < 0 ||
                        x >= static_cast<int>(buffer_width) ||
                        y >= static_cast<int>(buffer_height)
                    )
                    {
                        return 0.0f;
                    }

                    return coverage_buffer[
                        static_cast<size_t>(y) * buffer_width + x
                    ];
                };

                const float top = lerp(
                    fetch(x0, y0),
                    fetch(x0 + 1, y0),
                    fx
                );
                const float bottom = lerp(
                    fetch(x0, y0 + 1),
                    fetch(x0 + 1, y0 + 1),
                    fx
                );
                output.alpha = saturate(lerp(top, bottom, fy));
                output.value = 1.0f;
                break;
            }
            }

            if (source.type == layer_type::noise ||
                source.type == layer_type::spots ||
                source.type == layer_type::scratches ||
                source.type == layer_type::linear_gradient ||
                source.type == layer_type::radial_gradient)
            {
                output.value = saturate(
                    (output.value - 0.5f) *
                    std::max(0.01f, source.contrast) +
                    0.5f +
                    source.brightness
                );
            }

            if (source.invert)
            {
                output.value = 1.0f - output.value;
            }

            const float low = std::min(source.clip_low, source.clip_high);
            const float high = std::max(source.clip_low, source.clip_high);
            if (high - low > 0.0001f)
            {
                output.value = saturate(
                    (output.value - low) / (high - low)
                );
            }

            return output;
        }

        inline float blend_channel(
            const blend_mode mode,
            const float destination,
            const float source
        )
        {
            switch (mode)
            {
            case blend_mode::multiply:
                return destination * source;
            case blend_mode::screen:
                return 1.0f -
                    (1.0f - destination) * (1.0f - source);
            case blend_mode::overlay:
                return destination < 0.5f
                    ? 2.0f * destination * source
                    : 1.0f -
                        2.0f *
                        (1.0f - destination) *
                        (1.0f - source);
            case blend_mode::add:
                return destination + source;
            case blend_mode::subtract:
                return destination - source;
            case blend_mode::difference:
                return std::abs(destination - source);
            case blend_mode::darken:
                return std::min(destination, source);
            case blend_mode::lighten:
                return std::max(destination, source);
            default:
                return source;
            }
        }

        // renders the text of a layer into a coverage buffer so the layer uv
        // transform can rotate and scale it like any other pattern
        inline bool rasterize_text(
            const layer& source,
            const request& settings,
            std::vector<float>& coverage,
            uint32_t& buffer_width,
            uint32_t& buffer_height,
            std::string& error
        )
        {
            buffer_width  = settings.width;
            buffer_height = settings.height;
            coverage.assign(
                static_cast<size_t>(buffer_width) * buffer_height,
                0.0f
            );

            if (source.text.empty())
            {
                return true;
            }

            FT_Library library = nullptr;
            if (FT_Init_FreeType(&library) != 0)
            {
                error = "freetype could not be initialized";
                return false;
            }

            std::string font_name = source.font;
            if (font_name.empty())
            {
                font_name = "Calibri";
            }
            if (font_name.find('.') == std::string::npos)
            {
                font_name += ".ttf";
            }

            std::string font_path = font_name;
            if (
                font_name.find('/') == std::string::npos &&
                font_name.find('\\') == std::string::npos
            )
            {
                font_path =
                    settings.font_directory + "/" + font_name;
            }

            FT_Face face = nullptr;
            if (
                FT_New_Face(
                    library,
                    font_path.c_str(),
                    0,
                    &face
                ) != 0
            )
            {
                FT_Done_FreeType(library);
                error = "font could not be opened: " + font_path;
                return false;
            }

            const uint32_t pixel_size = std::clamp(
                static_cast<uint32_t>(
                    source.font_size *
                    static_cast<float>(buffer_height)
                ),
                4u,
                512u
            );
            FT_Set_Pixel_Sizes(face, 0, pixel_size);

            // split into lines so multi line labels stay readable
            std::vector<std::string> lines;
            std::string current;
            for (const char character : source.text)
            {
                if (character == '\n')
                {
                    lines.push_back(current);
                    current.clear();
                    continue;
                }
                current.push_back(character);
            }
            lines.push_back(current);

            const float spacing =
                source.letter_spacing *
                static_cast<float>(pixel_size);
            const float line_height =
                std::max(1.0f, source.line_spacing) *
                static_cast<float>(pixel_size);
            const float block_height =
                line_height * static_cast<float>(lines.size());
            float pen_y =
                source.y * static_cast<float>(buffer_height) -
                block_height * 0.5f +
                static_cast<float>(pixel_size) * 0.8f;

            for (const std::string& line : lines)
            {
                float advance = 0.0f;
                for (const char character : line)
                {
                    if (
                        FT_Load_Char(
                            face,
                            static_cast<FT_ULong>(
                                static_cast<unsigned char>(character)
                            ),
                            FT_LOAD_RENDER
                        ) != 0
                    )
                    {
                        continue;
                    }

                    advance +=
                        static_cast<float>(
                            face->glyph->advance.x >> 6
                        ) +
                        spacing;
                }

                float pen_x =
                    source.x * static_cast<float>(buffer_width);
                if (source.alignment == 1)
                {
                    pen_x -= advance * 0.5f;
                }
                else if (source.alignment == 2)
                {
                    pen_x -= advance;
                }

                for (const char character : line)
                {
                    if (
                        FT_Load_Char(
                            face,
                            static_cast<FT_ULong>(
                                static_cast<unsigned char>(character)
                            ),
                            FT_LOAD_RENDER
                        ) != 0
                    )
                    {
                        continue;
                    }

                    const FT_Bitmap& bitmap = face->glyph->bitmap;
                    const int origin_x = static_cast<int>(
                        pen_x + static_cast<float>(face->glyph->bitmap_left)
                    );
                    const int origin_y = static_cast<int>(
                        pen_y - static_cast<float>(face->glyph->bitmap_top)
                    );

                    for (
                        uint32_t row = 0;
                        row < bitmap.rows;
                        row++
                    )
                    {
                        const int target_y =
                            origin_y + static_cast<int>(row);
                        if (
                            target_y < 0 ||
                            target_y >= static_cast<int>(buffer_height)
                        )
                        {
                            continue;
                        }

                        for (
                            uint32_t column = 0;
                            column < bitmap.width;
                            column++
                        )
                        {
                            const int target_x =
                                origin_x + static_cast<int>(column);
                            if (
                                target_x < 0 ||
                                target_x >= static_cast<int>(buffer_width)
                            )
                            {
                                continue;
                            }

                            const uint32_t pitch =
                                static_cast<uint32_t>(
                                    std::abs(bitmap.pitch)
                                );
                            const float alpha =
                                static_cast<float>(
                                    bitmap.buffer[
                                        row * pitch + column
                                    ]
                                ) / 255.0f;
                            float& target = coverage[
                                static_cast<size_t>(target_y) *
                                buffer_width +
                                target_x
                            ];
                            target = std::max(target, alpha);
                        }
                    }

                    pen_x +=
                        static_cast<float>(
                            face->glyph->advance.x >> 6
                        ) +
                        spacing;
                }

                pen_y += line_height;
            }

            FT_Done_Face(face);
            FT_Done_FreeType(library);
            return true;
        }

        inline uint8_t to_byte(const float value)
        {
            return static_cast<uint8_t>(
                std::lround(saturate(value) * 255.0f)
            );
        }

        bool parse_color_value(
            const mcp_json::value& source,
            color4& output
        )
        {
            if (source.type == mcp_json::kind::string)
            {
                return parse_color(source.string_value, output);
            }

            if (
                !source.is_array() ||
                source.array_items.size() < 3 ||
                source.array_items.size() > 4
            )
            {
                return false;
            }

            const float red = static_cast<float>(
                source.array_items[0].number_or(0.0)
            );
            const float green = static_cast<float>(
                source.array_items[1].number_or(0.0)
            );
            const float blue = static_cast<float>(
                source.array_items[2].number_or(0.0)
            );
            const float color_divisor =
                red > 1.0f ||
                green > 1.0f ||
                blue > 1.0f
                    ? 255.0f
                    : 1.0f;

            output.r = red / color_divisor;
            output.g = green / color_divisor;
            output.b = blue / color_divisor;
            if (source.array_items.size() == 4)
            {
                const float alpha = static_cast<float>(
                    source.array_items[3].number_or(1.0)
                );
                output.a = alpha > 1.0f
                    ? alpha / 255.0f
                    : alpha;
            }
            else
            {
                output.a = 1.0f;
            }

            return true;
        }
    }

    bool parse_color(const std::string& text, color4& output)
    {
        if (text.empty())
        {
            return false;
        }

        if (text[0] == '#')
        {
            const std::string digits = text.substr(1);
            if (digits.size() != 6 && digits.size() != 8)
            {
                return false;
            }

            auto component = [&digits](const size_t index)
            {
                const std::string part = digits.substr(index, 2);
                return static_cast<float>(
                    std::stoul(part, nullptr, 16)
                ) / 255.0f;
            };

            try
            {
                output.r = component(0);
                output.g = component(2);
                output.b = component(4);
                output.a = digits.size() == 8
                    ? component(6)
                    : 1.0f;
            }
            catch (...)
            {
                return false;
            }

            return true;
        }

        std::vector<float> parts;
        std::string current;
        for (const char character : text + ",")
        {
            if (character == ',')
            {
                try
                {
                    parts.push_back(std::stof(current));
                }
                catch (...)
                {
                    return false;
                }
                current.clear();
                continue;
            }
            current.push_back(character);
        }

        if (parts.size() < 3)
        {
            return false;
        }

        const bool is_byte_range =
            parts[0] > 1.0f ||
            parts[1] > 1.0f ||
            parts[2] > 1.0f;
        const float divisor = is_byte_range ? 255.0f : 1.0f;
        output.r = parts[0] / divisor;
        output.g = parts[1] / divisor;
        output.b = parts[2] / divisor;
        output.a = parts.size() > 3
            ? (parts[3] > 1.0f ? parts[3] / 255.0f : parts[3])
            : 1.0f;
        return true;
    }

    bool generate(const request& settings, result& output, std::string& error)
    {
        if (
            settings.width < minimum_resolution ||
            settings.height < minimum_resolution ||
            settings.width > maximum_resolution ||
            settings.height > maximum_resolution
        )
        {
            error = "resolution must be between 32 and 2048";
            return false;
        }
        if (settings.layers.empty())
        {
            error = "at least one layer is required";
            return false;
        }
        if (settings.layers.size() > maximum_layers)
        {
            error = "too many layers, the limit is 24";
            return false;
        }

        const uint32_t width  = settings.width;
        const uint32_t height = settings.height;
        const size_t pixel_count =
            static_cast<size_t>(width) * height;
        const float texel = 1.0f /
            static_cast<float>(std::max(width, height));

        const bool has_color_layer = std::any_of(
            settings.layers.begin(),
            settings.layers.end(),
            [](const layer& source)
            {
                return source.contributes_color;
            }
        );
        std::vector<float> albedo(pixel_count * 4, 0.0f);
        if (!has_color_layer)
        {
            for (size_t index = 0; index < pixel_count; index++)
            {
                albedo[index * 4 + 0] = 1.0f;
                albedo[index * 4 + 1] = 1.0f;
                albedo[index * 4 + 2] = 1.0f;
                albedo[index * 4 + 3] = 1.0f;
            }
        }
        std::vector<float> relief(pixel_count, 0.0f);
        std::vector<float> roughness(
            pixel_count,
            saturate(settings.base_roughness)
        );
        std::vector<float> metalness(
            pixel_count,
            saturate(settings.base_metalness)
        );
        std::vector<float> occlusion(pixel_count, 1.0f);

        uint32_t layer_index = 0;
        for (const layer& source : settings.layers)
        {
            std::vector<float> coverage;
            uint32_t coverage_width  = 0;
            uint32_t coverage_height = 0;
            if (source.type == layer_type::text)
            {
                if (
                    !rasterize_text(
                        source,
                        settings,
                        coverage,
                        coverage_width,
                        coverage_height,
                        error
                    )
                )
                {
                    return false;
                }
            }

            const uint32_t layer_seed =
                settings.seed + layer_index * 7717u;
            const float radians = source.rotation * 0.01745329f;
            const float cosine = std::cos(radians);
            const float sine   = std::sin(radians);
            const float scale_x = std::abs(source.scale_x) < 0.0001f
                ? 1.0f
                : source.scale_x;
            const float scale_y = std::abs(source.scale_y) < 0.0001f
                ? 1.0f
                : source.scale_y;

            ThreadPool::ParallelLoop([&](
                const uint32_t row_start,
                const uint32_t row_end
            )
            {
            for (uint32_t y = row_start; y < row_end; y++)
            {
                for (uint32_t x = 0; x < width; x++)
                {
                    const size_t index =
                        static_cast<size_t>(y) * width + x;
                    const float u =
                        (static_cast<float>(x) + 0.5f) /
                        static_cast<float>(width);
                    const float v =
                        (static_cast<float>(y) + 0.5f) /
                        static_cast<float>(height);

                    const float centered_u = u - 0.5f;
                    const float centered_v = v - 0.5f;
                    const float rotated_u =
                        centered_u * cosine - centered_v * sine;
                    const float rotated_v =
                        centered_u * sine + centered_v * cosine;
                    const float sample_u =
                        rotated_u * scale_x + 0.5f + source.offset_x;
                    const float sample_v =
                        rotated_v * scale_y + 0.5f + source.offset_y;

                    const sample_result sample =
                        evaluate(
                            source,
                            sample_u,
                            sample_v,
                            layer_seed,
                            settings.seamless,
                            texel,
                            coverage,
                            coverage_width,
                            coverage_height
                        );

                    float alpha =
                        sample.alpha *
                        saturate(source.opacity);
                    if (source.value_as_alpha)
                    {
                        alpha *= sample.value;
                    }
                    if (alpha <= 0.0f)
                    {
                        continue;
                    }

                    if (source.contributes_color)
                    {
                        const color4 tint = source.tint_by_value
                            ? color4
                            {
                                lerp(
                                    source.color_a.r,
                                    source.color_b.r,
                                    sample.value
                                ),
                                lerp(
                                    source.color_a.g,
                                    source.color_b.g,
                                    sample.value
                                ),
                                lerp(
                                    source.color_a.b,
                                    source.color_b.b,
                                    sample.value
                                ),
                                lerp(
                                    source.color_a.a,
                                    source.color_b.a,
                                    sample.value
                                )
                            }
                            : source.color_a;

                        const float source_alpha = alpha * tint.a;
                        if (source_alpha <= 0.0f)
                        {
                            continue;
                        }

                        float* pixel = &albedo[index * 4];
                        const float blended_r = blend_channel(
                            source.blend,
                            pixel[0],
                            tint.r
                        );
                        const float blended_g = blend_channel(
                            source.blend,
                            pixel[1],
                            tint.g
                        );
                        const float blended_b = blend_channel(
                            source.blend,
                            pixel[2],
                            tint.b
                        );

                        pixel[0] = lerp(
                            pixel[0],
                            blended_r,
                            source_alpha
                        );
                        pixel[1] = lerp(
                            pixel[1],
                            blended_g,
                            source_alpha
                        );
                        pixel[2] = lerp(
                            pixel[2],
                            blended_b,
                            source_alpha
                        );
                        pixel[3] = source_alpha +
                            pixel[3] * (1.0f - source_alpha);
                    }

                    if (source.relief != 0.0f)
                    {
                        relief[index] +=
                            source.relief *
                            sample.value *
                            alpha;
                    }
                    if (source.roughness_value >= 0.0f)
                    {
                        const float target = source.roughness_range >= 0.0f
                            ? lerp(
                                source.roughness_value,
                                source.roughness_range,
                                sample.value
                            )
                            : source.roughness_value;
                        roughness[index] = lerp(
                            roughness[index],
                            target,
                            alpha
                        );
                    }
                    if (source.metalness_value >= 0.0f)
                    {
                        metalness[index] = lerp(
                            metalness[index],
                            source.metalness_value,
                            alpha
                        );
                    }
                    if (source.occlusion > 0.0f)
                    {
                        occlusion[index] = lerp(
                            occlusion[index],
                            1.0f - source.occlusion,
                            alpha * sample.value
                        );
                    }
                }
            }
            }, height);

            layer_index++;
        }

        // normal map from the accumulated relief, wrapping keeps tiles seamless
        float relief_min = relief.empty() ? 0.0f : relief[0];
        float relief_max = relief_min;
        for (const float value : relief)
        {
            relief_min = std::min(relief_min, value);
            relief_max = std::max(relief_max, value);
        }

        const float relief_range = relief_max - relief_min;
        output.width  = width;
        output.height = height;
        output.albedo.resize(pixel_count * 4);
        output.normal.resize(pixel_count * 4);
        output.roughness.resize(pixel_count * 4);
        output.packed.resize(pixel_count * 4);

        auto relief_at = [&](const int x, const int y)
        {
            const int wrapped_x = wrap_index(
                x,
                static_cast<int>(width)
            );
            const int wrapped_y = wrap_index(
                y,
                static_cast<int>(height)
            );
            const float value = relief[
                static_cast<size_t>(wrapped_y) * width + wrapped_x
            ];
            return relief_range > 0.0001f
                ? (value - relief_min) / relief_range
                : 0.0f;
        };

        double sum_r = 0.0;
        double sum_g = 0.0;
        double sum_b = 0.0;
        double sum_luminance = 0.0;
        double sum_luminance_squared = 0.0;
        double sum_alpha = 0.0;

        for (uint32_t y = 0; y < height; y++)
        {
            for (uint32_t x = 0; x < width; x++)
            {
                const size_t index =
                    static_cast<size_t>(y) * width + x;
                const float* pixel = &albedo[index * 4];

                output.albedo[index * 4 + 0] =
                    to_byte(pixel[0]);
                output.albedo[index * 4 + 1] =
                    to_byte(pixel[1]);
                output.albedo[index * 4 + 2] =
                    to_byte(pixel[2]);
                output.albedo[index * 4 + 3] =
                    to_byte(pixel[3]);

                const int signed_x = static_cast<int>(x);
                const int signed_y = static_cast<int>(y);
                const float left   = relief_at(signed_x - 1, signed_y);
                const float right  = relief_at(signed_x + 1, signed_y);
                const float top    = relief_at(signed_x, signed_y - 1);
                const float bottom = relief_at(signed_x, signed_y + 1);
                const float strength =
                    std::max(0.0f, settings.normal_strength) *
                    static_cast<float>(std::max(width, height)) *
                    0.02f;
                float normal_x = (left - right) * strength;
                float normal_y = (top - bottom) * strength;
                const float length = std::sqrt(
                    normal_x * normal_x +
                    normal_y * normal_y +
                    1.0f
                );
                normal_x /= length;
                normal_y /= length;
                const float normal_z = 1.0f / length;

                output.normal[index * 4 + 0] =
                    to_byte(normal_x * 0.5f + 0.5f);
                output.normal[index * 4 + 1] =
                    to_byte(normal_y * 0.5f + 0.5f);
                output.normal[index * 4 + 2] =
                    to_byte(normal_z * 0.5f + 0.5f);
                output.normal[index * 4 + 3] = 255;

                const uint8_t roughness_byte =
                    to_byte(roughness[index]);
                output.roughness[index * 4 + 0] = roughness_byte;
                output.roughness[index * 4 + 1] = roughness_byte;
                output.roughness[index * 4 + 2] = roughness_byte;
                output.roughness[index * 4 + 3] = 255;

                output.packed[index * 4 + 0] =
                    to_byte(occlusion[index]);
                output.packed[index * 4 + 1] = roughness_byte;
                output.packed[index * 4 + 2] =
                    to_byte(metalness[index]);
                output.packed[index * 4 + 3] =
                    to_byte(relief_at(signed_x, signed_y));

                const double luminance =
                    0.2126 * pixel[0] +
                    0.7152 * pixel[1] +
                    0.0722 * pixel[2];
                sum_r += pixel[0];
                sum_g += pixel[1];
                sum_b += pixel[2];
                sum_alpha += pixel[3];
                sum_luminance += luminance;
                sum_luminance_squared += luminance * luminance;
            }
        }

        const double count = static_cast<double>(pixel_count);
        output.stats.mean_r =
            static_cast<float>(sum_r / count);
        output.stats.mean_g =
            static_cast<float>(sum_g / count);
        output.stats.mean_b =
            static_cast<float>(sum_b / count);
        output.stats.mean_luminance =
            static_cast<float>(sum_luminance / count);
        output.stats.coverage =
            static_cast<float>(sum_alpha / count);
        output.stats.relief_range = relief_range;

        const double variance = std::max(
            0.0,
            sum_luminance_squared / count -
            (sum_luminance / count) * (sum_luminance / count)
        );
        output.stats.contrast =
            static_cast<float>(std::sqrt(variance));

        // how visible a tiling seam would be, the agent uses this to iterate
        double seam = 0.0;
        for (uint32_t y = 0; y < height; y++)
        {
            const size_t left =
                (static_cast<size_t>(y) * width) * 4;
            const size_t right =
                (static_cast<size_t>(y) * width + width - 1) * 4;
            for (uint32_t channel = 0; channel < 3; channel++)
            {
                seam += std::abs(
                    albedo[left + channel] -
                    albedo[right + channel]
                );
            }
        }
        for (uint32_t x = 0; x < width; x++)
        {
            const size_t top = static_cast<size_t>(x) * 4;
            const size_t bottom =
                (static_cast<size_t>(height - 1) * width + x) * 4;
            for (uint32_t channel = 0; channel < 3; channel++)
            {
                seam += std::abs(
                    albedo[top + channel] -
                    albedo[bottom + channel]
                );
            }
        }

        output.stats.seam_error = static_cast<float>(
            seam / (3.0 * (width + height))
        );
        return true;
    }

    bool layer_from_json(const mcp_json::value& source, layer& output, std::string& error)
    {
        if (!source.is_object())
        {
            error = "every layer must be a json object";
            return false;
        }

        const std::string type = source.member_string("type", "fill");
        if (type == "fill")                 { output.type = layer_type::fill; }
        else if (type == "linear_gradient" ||
                 type == "gradient")        { output.type = layer_type::linear_gradient; }
        else if (type == "radial_gradient") { output.type = layer_type::radial_gradient; }
        else if (type == "noise")           { output.type = layer_type::noise; }
        else if (type == "checker")         { output.type = layer_type::checker; }
        else if (type == "stripes")         { output.type = layer_type::stripes; }
        else if (type == "bricks")          { output.type = layer_type::bricks; }
        else if (type == "tiles")           { output.type = layer_type::tiles; }
        else if (type == "spots")           { output.type = layer_type::spots; }
        else if (type == "scratches")       { output.type = layer_type::scratches; }
        else if (type == "shape")           { output.type = layer_type::shape; }
        else if (type == "text")            { output.type = layer_type::text; }
        else
        {
            error = "unknown layer type: " + type;
            return false;
        }

        const std::string blend =
            source.member_string("blend", "normal");
        if (blend == "normal")          { output.blend = blend_mode::normal; }
        else if (blend == "multiply")   { output.blend = blend_mode::multiply; }
        else if (blend == "screen")     { output.blend = blend_mode::screen; }
        else if (blend == "overlay")    { output.blend = blend_mode::overlay; }
        else if (blend == "add")        { output.blend = blend_mode::add; }
        else if (blend == "subtract")   { output.blend = blend_mode::subtract; }
        else if (blend == "difference") { output.blend = blend_mode::difference; }
        else if (blend == "darken")     { output.blend = blend_mode::darken; }
        else if (blend == "lighten")    { output.blend = blend_mode::lighten; }
        else
        {
            error = "unknown blend mode: " + blend;
            return false;
        }

        const std::string noise =
            source.member_string("noise", "value");
        if (noise == "value" || noise == "value_fbm")   { output.noise = noise_kind::value_fbm; }
        else if (noise == "perlin" || noise == "perlin_fbm") { output.noise = noise_kind::perlin_fbm; }
        else if (noise == "ridged")                     { output.noise = noise_kind::ridged; }
        else if (noise == "worley" || noise == "cells") { output.noise = noise_kind::worley; }

        const std::string shape =
            source.member_string("shape", "rectangle");
        if (shape == "rectangle" || shape == "rect")    { output.shape = shape_kind::rectangle; }
        else if (shape == "rounded_rectangle" ||
                 shape == "rounded_rect")               { output.shape = shape_kind::rounded_rectangle; }
        else if (shape == "ellipse" || shape == "circle") { output.shape = shape_kind::ellipse; }
        else if (shape == "polygon")                    { output.shape = shape_kind::polygon; }
        else if (shape == "line")                       { output.shape = shape_kind::line; }

        if (const mcp_json::value* color = source.find("color"))
        {
            if (!parse_color_value(*color, output.color_a))
            {
                error =
                    "color must be #rrggbb, #rrggbbaa, or an rgba array";
                return false;
            }
            output.contributes_color = true;
        }

        if (const mcp_json::value* color_b = source.find("color_b"))
        {
            if (!parse_color_value(*color_b, output.color_b))
            {
                error =
                    "color_b must be #rrggbb, #rrggbbaa, or an rgba array";
                return false;
            }
        }
        else
        {
            output.color_b = output.color_a;
            output.tint_by_value = false;
        }

        output.opacity = static_cast<float>(
            source.member_number("opacity", 1.0)
        );
        output.value_as_alpha =
            source.member_boolean("value_as_alpha", false);
        output.invert = source.member_boolean("invert", false);

        output.scale_x = static_cast<float>(
            source.member_number("scale_x", 1.0)
        );
        output.scale_y = static_cast<float>(
            source.member_number("scale_y", 1.0)
        );
        output.offset_x = static_cast<float>(
            source.member_number("offset_x", 0.0)
        );
        output.offset_y = static_cast<float>(
            source.member_number("offset_y", 0.0)
        );
        output.rotation = static_cast<float>(
            source.member_number("rotation", 0.0)
        );

        output.frequency = static_cast<float>(
            source.member_number("frequency", 4.0)
        );
        output.octaves = static_cast<uint32_t>(
            std::clamp(
                source.member_number("octaves", 4.0),
                1.0,
                8.0
            )
        );
        output.lacunarity = static_cast<float>(
            source.member_number("lacunarity", 2.0)
        );
        output.gain = static_cast<float>(
            source.member_number("gain", 0.5)
        );
        output.warp = static_cast<float>(
            source.member_number("warp", 0.0)
        );
        output.contrast = static_cast<float>(
            source.member_number("contrast", 1.0)
        );
        output.brightness = static_cast<float>(
            source.member_number("brightness", 0.0)
        );

        output.count_x = static_cast<float>(
            source.member_number("count_x", 4.0)
        );
        output.count_y = static_cast<float>(
            source.member_number("count_y", 4.0)
        );
        output.mortar = static_cast<float>(
            source.member_number("mortar", 0.06)
        );
        output.row_offset = static_cast<float>(
            source.member_number("row_offset", 0.5)
        );
        output.variation = static_cast<float>(
            source.member_number("variation", 0.0)
        );
        output.angle = static_cast<float>(
            source.member_number("angle", 0.0)
        );
        output.duty = static_cast<float>(
            source.member_number("duty", 0.5)
        );
        output.density = static_cast<float>(
            source.member_number("density", 1.0)
        );
        output.sharpness = static_cast<float>(
            source.member_number("sharpness", 1.0)
        );

        output.x = static_cast<float>(
            source.member_number("x", 0.5)
        );
        output.y = static_cast<float>(
            source.member_number("y", 0.5)
        );
        output.width = static_cast<float>(
            source.member_number("width", 0.5)
        );
        output.height = static_cast<float>(
            source.member_number("height", 0.3)
        );
        output.corner = static_cast<float>(
            source.member_number("corner", 0.05)
        );
        output.thickness = static_cast<float>(
            source.member_number("thickness", 0.0)
        );

        if (const mcp_json::value* points = source.find("points"))
        {
            if (points->is_array())
            {
                for (
                    const mcp_json::value& item :
                    points->array_items
                )
                {
                    output.points.push_back(
                        static_cast<float>(item.number_or(0.0))
                    );
                }
            }
        }

        output.text = source.member_string("text", "");
        output.font = source.member_string("font", "Calibri");
        output.font_size = static_cast<float>(
            source.member_number("font_size", 0.16)
        );
        output.letter_spacing = static_cast<float>(
            source.member_number("letter_spacing", 0.0)
        );
        output.line_spacing = static_cast<float>(
            source.member_number("line_spacing", 1.2)
        );

        const std::string alignment =
            source.member_string("align", "center");
        output.alignment =
            alignment == "left" ? 0 :
            alignment == "right" ? 2 : 1;

        output.relief = static_cast<float>(
            source.member_number("relief", 0.0)
        );
        output.roughness_value = static_cast<float>(
            source.member_number("roughness", -1.0)
        );
        output.roughness_range = static_cast<float>(
            source.member_number("roughness_b", -1.0)
        );
        output.metalness_value = static_cast<float>(
            source.member_number("metalness", -1.0)
        );
        output.occlusion = static_cast<float>(
            source.member_number("occlusion", 0.0)
        );
        output.clip_low = static_cast<float>(
            source.member_number("clip_low", 0.0)
        );
        output.clip_high = static_cast<float>(
            source.member_number("clip_high", 1.0)
        );
        return true;
    }

    bool request_from_json(const std::string& layers_json, request& output, std::string& error)
    {
        mcp_json::value parsed;
        if (!mcp_json::parse(layers_json, parsed, error))
        {
            error = "layers must be valid json: " + error;
            return false;
        }

        if (!parsed.is_array())
        {
            error = "layers must be a json array";
            return false;
        }

        for (const mcp_json::value& item : parsed.array_items)
        {
            layer entry;
            if (!layer_from_json(item, entry, error))
            {
                return false;
            }
            output.layers.push_back(std::move(entry));
        }

        return true;
    }

}
