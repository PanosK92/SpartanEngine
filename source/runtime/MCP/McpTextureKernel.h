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

#pragma once

// composites cpu textures from a stack of procedural layers into albedo, normal, roughness and packed maps

#include <cstdint>
#include <string>
#include <vector>
#include "McpJson.h"

namespace spartan::mcp_texture_kernel
{
    constexpr uint32_t minimum_resolution = 32;
    constexpr uint32_t maximum_resolution = 2048;
    constexpr uint32_t maximum_layers     = 24;

    struct color4
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    enum class blend_mode
    {
        normal,
        multiply,
        screen,
        overlay,
        add,
        subtract,
        difference,
        darken,
        lighten
    };

    enum class layer_type
    {
        fill,
        linear_gradient,
        radial_gradient,
        noise,
        checker,
        stripes,
        bricks,
        tiles,
        spots,
        scratches,
        shape,
        text
    };

    enum class noise_kind
    {
        value_fbm,
        perlin_fbm,
        ridged,
        worley
    };

    enum class shape_kind
    {
        rectangle,
        rounded_rectangle,
        ellipse,
        polygon,
        line
    };

    struct layer
    {
        layer_type type    = layer_type::fill;
        blend_mode blend   = blend_mode::normal;
        float opacity      = 1.0f;

        color4 color_a;
        color4 color_b;
        bool tint_by_value  = true;
        bool value_as_alpha = false;
        bool invert         = false;

        // uv transform, rotation is in degrees around the center
        float scale_x   = 1.0f;
        float scale_y   = 1.0f;
        float offset_x  = 0.0f;
        float offset_y  = 0.0f;
        float rotation  = 0.0f;

        // noise
        noise_kind noise   = noise_kind::value_fbm;
        float frequency    = 4.0f;
        uint32_t octaves   = 4;
        float lacunarity   = 2.0f;
        float gain         = 0.5f;
        float warp         = 0.0f;
        float contrast     = 1.0f;
        float brightness   = 0.0f;

        // repeating patterns
        float count_x     = 4.0f;
        float count_y     = 4.0f;
        float mortar      = 0.06f;
        float row_offset  = 0.5f;
        float variation   = 0.0f;
        float angle       = 0.0f;
        float duty        = 0.5f;
        float density     = 1.0f;
        float sharpness   = 1.0f;

        // shapes
        shape_kind shape  = shape_kind::rectangle;
        float x           = 0.5f;
        float y           = 0.5f;
        float width       = 0.5f;
        float height      = 0.3f;
        float corner      = 0.05f;
        float thickness   = 0.0f;
        std::vector<float> points;

        // text
        std::string text;
        std::string font       = "Calibri";
        float font_size        = 0.16f;
        float letter_spacing   = 0.0f;
        float line_spacing     = 1.2f;
        int alignment          = 1;

        // material contributions, negative means the layer does not contribute
        float relief          = 0.0f;
        float roughness_value = -1.0f;
        float roughness_range = -1.0f;
        float metalness_value = -1.0f;
        float occlusion       = 0.0f;

        // clamps the pattern before it is used
        float clip_low  = 0.0f;
        float clip_high = 1.0f;
    };

    struct request
    {
        uint32_t width     = 512;
        uint32_t height    = 512;
        uint32_t seed      = 1337;
        bool seamless      = true;
        float normal_strength   = 1.0f;
        float base_roughness    = 0.75f;
        float base_metalness    = 0.0f;
        std::string font_directory = "data/fonts";
        std::vector<layer> layers;
    };

    struct statistics
    {
        float mean_r          = 0.0f;
        float mean_g          = 0.0f;
        float mean_b          = 0.0f;
        float mean_luminance  = 0.0f;
        float contrast        = 0.0f;
        float coverage        = 0.0f;
        float seam_error      = 0.0f;
        float relief_range    = 0.0f;
    };

    struct result
    {
        uint32_t width  = 0;
        uint32_t height = 0;
        std::vector<uint8_t> albedo;
        std::vector<uint8_t> normal;
        std::vector<uint8_t> roughness;
        std::vector<uint8_t> packed;
        statistics stats;
    };

    bool parse_color(const std::string& text, color4& output);

    bool generate(const request& settings, result& output, std::string& error);

    bool layer_from_json(const mcp_json::value& source, layer& output, std::string& error);

    bool request_from_json(const std::string& layers_json, request& output, std::string& error);
}
