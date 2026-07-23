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

//= INCLUDES ============================
#include "pch.h"
#include "Text3D.h"
#include "Render.h"
#include "../Entity.h"
#include "../../Geometry/Mesh.h"
#include "../../Core/ProgressTracker.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Renderer.h"
#include <cmath>
SP_WARNINGS_OFF
#include <freetype/freetype.h>
#include <freetype/ftoutln.h>
#include <sol/sol.hpp>
#include "../../IO/pugixml.hpp"
SP_WARNINGS_ON
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        constexpr uint8_t coverage_threshold = 64;
        constexpr uint32_t max_character_count = 4096;
        constexpr uint64_t max_raster_pixel_count = 4'000'000;
        constexpr uint32_t max_generated_quad_count = 250'000;

        struct RasterGlyph
        {
            vector<uint8_t> pixels;
            uint32_t width  = 0;
            uint32_t height = 0;
            uint32_t line    = 0;
            float x         = 0.0f;
            float y         = 0.0f;
        };

        vector<uint32_t> decode_utf8(
            const string& text,
            bool& character_limit_reached
        )
        {
            vector<uint32_t> codepoints;

            for (size_t i = 0; i < text.size();)
            {
                if (codepoints.size() >= max_character_count)
                {
                    character_limit_reached = true;
                    break;
                }

                const uint8_t first = static_cast<uint8_t>(text[i]);
                uint32_t codepoint  = 0;
                size_t length       = 1;

                if ((first & 0x80) == 0)
                {
                    codepoint = first;
                }
                else if ((first & 0xe0) == 0xc0)
                {
                    codepoint = first & 0x1f;
                    length    = 2;
                }
                else if ((first & 0xf0) == 0xe0)
                {
                    codepoint = first & 0x0f;
                    length    = 3;
                }
                else if ((first & 0xf8) == 0xf0)
                {
                    codepoint = first & 0x07;
                    length    = 4;
                }
                else
                {
                    codepoints.push_back(0xfffd);
                    i++;
                    continue;
                }

                if (i + length > text.size())
                {
                    codepoints.push_back(0xfffd);
                    break;
                }

                bool valid = true;
                for (size_t byte_index = 1; byte_index < length; byte_index++)
                {
                    const uint8_t byte =
                        static_cast<uint8_t>(text[i + byte_index]);

                    if ((byte & 0xc0) != 0x80)
                    {
                        valid = false;
                        break;
                    }

                    codepoint =
                        (codepoint << 6) |
                        static_cast<uint32_t>(byte & 0x3f);
                }

                codepoints.push_back(valid ? codepoint : 0xfffd);
                i += valid ? length : 1;
            }

            return codepoints;
        }

        bool is_solid(
            const RasterGlyph& glyph,
            const int64_t x,
            const int64_t y
        )
        {
            if (
                x < 0 ||
                y < 0 ||
                x >= static_cast<int64_t>(glyph.width) ||
                y >= static_cast<int64_t>(glyph.height)
            )
            {
                return false;
            }

            const size_t index =
                static_cast<size_t>(y) * glyph.width +
                static_cast<size_t>(x);

            return glyph.pixels[index] >= coverage_threshold;
        }

        void add_quad(
            vector<RHI_Vertex_PosTexNorTan>& vertices,
            vector<uint32_t>& indices,
            const Vector3& a,
            const Vector3& b,
            const Vector3& c,
            const Vector3& d,
            const Vector3& normal,
            const Vector2& uv_a,
            const Vector2& uv_b,
            const Vector2& uv_c,
            const Vector2& uv_d,
            bool& geometry_limit_reached
        )
        {
            if (geometry_limit_reached)
            {
                return;
            }

            if (
                vertices.size() / 4 >=
                max_generated_quad_count
            )
            {
                geometry_limit_reached = true;
                return;
            }

            const uint32_t start = static_cast<uint32_t>(vertices.size());
            Vector3 tangent      = Vector3::Right;

            if (abs(normal.Dot(Vector3::Right)) > 0.9f)
            {
                tangent = Vector3::Forward;
            }

            vertices.emplace_back(a, uv_a, normal, tangent);
            vertices.emplace_back(b, uv_b, normal, tangent);
            vertices.emplace_back(c, uv_c, normal, tangent);
            vertices.emplace_back(d, uv_d, normal, tangent);

            indices.push_back(start);
            indices.push_back(start + 2);
            indices.push_back(start + 1);
            indices.push_back(start);
            indices.push_back(start + 3);
            indices.push_back(start + 2);
        }

        float get_alignment_offset(
            const Text3DAlignment alignment,
            const float width
        )
        {
            if (alignment == Text3DAlignment::Center)
            {
                return -width * 0.5f;
            }

            if (alignment == Text3DAlignment::Right)
            {
                return -width;
            }

            return 0.0f;
        }
    }

    Text3D::Text3D(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_text, SetText, string);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_font_path, SetFontPath, string);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_size, SetSize, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_depth, SetDepth, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_weight, SetWeight, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(
            m_letter_spacing,
            SetLetterSpacing,
            float
        );
        SP_REGISTER_ATTRIBUTE_VALUE_SET(
            m_line_spacing,
            SetLineSpacing,
            float
        );
        SP_REGISTER_ATTRIBUTE_VALUE_SET(
            m_resolution,
            SetResolution,
            uint32_t
        );
        SP_REGISTER_ATTRIBUTE_VALUE_SET(
            m_alignment,
            SetAlignment,
            Text3DAlignment
        );
    }

    Text3D::~Text3D()
    {
        ClearMesh();
    }

    void Text3D::Initialize()
    {
        if (m_font_path.empty())
        {
            m_font_path =
                ResourceCache::GetResourceDirectory(
                    ResourceDirectory::Fonts
                ) +
                "/OpenSans/OpenSans-Medium.ttf";
        }

        SetDirty();
    }

    void Text3D::Tick()
    {
        if (ProgressTracker::IsLoading())
        {
            return;
        }

        if (!m_dirty)
        {
            if (m_mesh)
            {
                Render* render = m_entity_ptr->GetComponent<Render>();
                if (
                    render &&
                    !render->GetMaterial() &&
                    Renderer::GetStandardMaterial()
                )
                {
                    render->SetDefaultMaterial();
                }
            }

            return;
        }

        if (Timer::GetTimeSec() - m_dirty_time < 0.2)
        {
            return;
        }

        GenerateMesh();
    }

    void Text3D::Remove()
    {
        ClearMesh();
    }

    void Text3D::Save(pugi::xml_node& node)
    {
        node.append_attribute("text")           = m_text.c_str();
        node.append_attribute("font_path")      = m_font_path.c_str();
        node.append_attribute("size")           = m_size;
        node.append_attribute("depth")          = m_depth;
        node.append_attribute("weight")         = m_weight;
        node.append_attribute("letter_spacing") = m_letter_spacing;
        node.append_attribute("line_spacing")   = m_line_spacing;
        node.append_attribute("resolution")     = m_resolution;
        node.append_attribute("alignment")      =
            static_cast<uint32_t>(m_alignment);
    }

    void Text3D::Load(pugi::xml_node& node)
    {
        SetText(node.attribute("text").as_string("Text"));

        const string font_path =
            node.attribute("font_path").as_string();
        if (!font_path.empty())
        {
            SetFontPath(font_path);
        }

        SetSize(node.attribute("size").as_float(1.0f));
        SetDepth(node.attribute("depth").as_float(0.1f));
        SetWeight(node.attribute("weight").as_float(0.0f));
        SetLetterSpacing(
            node.attribute("letter_spacing").as_float(0.0f)
        );
        SetLineSpacing(
            node.attribute("line_spacing").as_float(1.2f)
        );
        SetResolution(
            node.attribute("resolution").as_uint(128)
        );

        const uint32_t alignment =
            node.attribute("alignment").as_uint(0);
        if (
            alignment <=
            static_cast<uint32_t>(Text3DAlignment::Right)
        )
        {
            SetAlignment(
                static_cast<Text3DAlignment>(alignment)
            );
        }

        SetDirty();
    }

    void Text3D::RegisterForScripting(sol::state_view state)
    {
        state.new_enum(
            "Text3DAlignment",
            "Left",
            Text3DAlignment::Left,
            "Center",
            Text3DAlignment::Center,
            "Right",
            Text3DAlignment::Right
        );

        state.new_usertype<Text3D>(
            "Text3D",
            sol::base_classes,
            sol::bases<Component>(),
            "GetText",
            &Text3D::GetText,
            "SetText",
            &Text3D::SetText,
            "GetFontPath",
            &Text3D::GetFontPath,
            "SetFontPath",
            &Text3D::SetFontPath,
            "GetSize",
            &Text3D::GetSize,
            "SetSize",
            &Text3D::SetSize,
            "GetDepth",
            &Text3D::GetDepth,
            "SetDepth",
            &Text3D::SetDepth,
            "GetWeight",
            &Text3D::GetWeight,
            "SetWeight",
            &Text3D::SetWeight,
            "GetLetterSpacing",
            &Text3D::GetLetterSpacing,
            "SetLetterSpacing",
            &Text3D::SetLetterSpacing,
            "GetLineSpacing",
            &Text3D::GetLineSpacing,
            "SetLineSpacing",
            &Text3D::SetLineSpacing,
            "GetResolution",
            &Text3D::GetResolution,
            "SetResolution",
            &Text3D::SetResolution,
            "GetAlignment",
            &Text3D::GetAlignment,
            "SetAlignment",
            &Text3D::SetAlignment,
            "GenerateMesh",
            &Text3D::GenerateMesh
        );
    }

    sol::reference Text3D::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void Text3D::SetText(const string& text)
    {
        if (m_text != text)
        {
            m_text = text;
            SetDirty();
        }
    }

    void Text3D::SetFontPath(const string& font_path)
    {
        if (
            m_font_path != font_path &&
            FileSystem::IsSupportedFontFile(font_path) &&
            FileSystem::IsFile(font_path)
        )
        {
            m_font_path = font_path;
            SetDirty();
        }
    }

    void Text3D::SetSize(const float size)
    {
        if (!std::isfinite(size))
        {
            return;
        }

        const float clamped = clamp(size, 0.01f, 1000.0f);
        if (m_size != clamped)
        {
            m_size = clamped;
            SetDirty();
        }
    }

    void Text3D::SetDepth(const float depth)
    {
        if (!std::isfinite(depth))
        {
            return;
        }

        const float clamped = clamp(depth, 0.001f, 1000.0f);
        if (m_depth != clamped)
        {
            m_depth = clamped;
            SetDirty();
        }
    }

    void Text3D::SetWeight(const float weight)
    {
        if (!std::isfinite(weight))
        {
            return;
        }

        const float clamped = clamp(weight, 0.0f, 1.0f);
        if (m_weight != clamped)
        {
            m_weight = clamped;
            SetDirty();
        }
    }

    void Text3D::SetLetterSpacing(const float spacing)
    {
        if (!std::isfinite(spacing))
        {
            return;
        }

        const float clamped = clamp(spacing, -10.0f, 100.0f);
        if (m_letter_spacing != clamped)
        {
            m_letter_spacing = clamped;
            SetDirty();
        }
    }

    void Text3D::SetLineSpacing(const float spacing)
    {
        if (!std::isfinite(spacing))
        {
            return;
        }

        const float clamped = clamp(spacing, 0.1f, 10.0f);
        if (m_line_spacing != clamped)
        {
            m_line_spacing = clamped;
            SetDirty();
        }
    }

    void Text3D::SetResolution(const uint32_t resolution)
    {
        const uint32_t clamped = clamp(resolution, 32u, 512u);
        if (m_resolution != clamped)
        {
            m_resolution = clamped;
            SetDirty();
        }
    }

    void Text3D::SetAlignment(const Text3DAlignment alignment)
    {
        if (m_alignment != alignment)
        {
            m_alignment = alignment;
            SetDirty();
        }
    }

    void Text3D::SetDirty()
    {
        m_dirty      = true;
        m_dirty_time = Timer::GetTimeSec();
    }

    bool Text3D::GenerateMesh()
    {
        if (
            m_text.empty() ||
            m_font_path.empty() ||
            !FileSystem::IsFile(m_font_path)
        )
        {
            ClearMesh();
            m_dirty = false;
            return false;
        }

        Render* render = m_entity_ptr->GetComponent<Render>();
        if (
            m_render_bound &&
            (
                !render ||
                render->GetObjectId() != m_bound_render_id ||
                render->GetMesh() != m_mesh.get()
            )
        )
        {
            m_mesh.reset();
            m_render_bound     = false;
            m_created_render   = false;
            m_bound_render_id = 0;
        }

        if (
            !m_render_bound &&
            render &&
            render->GetMesh()
        )
        {
            SP_LOG_WARNING(
                "3d text requires an entity without an existing mesh"
            );
            m_dirty = false;
            return false;
        }

        FT_Library library = nullptr;
        if (FT_Init_FreeType(&library) != FT_Err_Ok)
        {
            SP_LOG_ERROR("failed to initialize freetype for 3d text");
            ClearMesh();
            m_dirty = false;
            return false;
        }

        FT_Face face = nullptr;
        if (
            FT_New_Face(
                library,
                m_font_path.c_str(),
                0,
                &face
            ) != FT_Err_Ok
        )
        {
            SP_LOG_ERROR(
                "failed to load 3d text font \"%s\"",
                m_font_path.c_str()
            );
            FT_Done_FreeType(library);
            ClearMesh();
            m_dirty = false;
            return false;
        }

        if (
            FT_Set_Pixel_Sizes(
                face,
                0,
                m_resolution
            ) != FT_Err_Ok
        )
        {
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            ClearMesh();
            m_dirty = false;
            return false;
        }

        const float scale =
            m_size /
            static_cast<float>(m_resolution);
        const float depth_half = m_depth * 0.5f;
        bool character_limit_reached = false;
        const vector<uint32_t> codepoints = decode_utf8(
            m_text,
            character_limit_reached
        );
        if (character_limit_reached)
        {
            SP_LOG_WARNING(
                "3d text exceeded its character limit"
            );
            FT_Done_Face(face);
            FT_Done_FreeType(library);
            ClearMesh();
            m_dirty = false;
            return false;
        }

        vector<RasterGlyph> glyphs;
        vector<float> line_widths(1, 0.0f);
        uint32_t line_index = 0;
        FT_UInt previous_glyph_index = 0;
        float pen_x = 0.0f;
        uint64_t raster_pixel_count = 0;
        bool raster_limit_reached = false;

        for (const uint32_t codepoint : codepoints)
        {
            if (codepoint == '\r')
            {
                continue;
            }

            if (codepoint == '\n')
            {
                line_widths[line_index] = pen_x;
                line_widths.push_back(0.0f);
                line_index++;
                pen_x = 0.0f;
                previous_glyph_index = 0;
                continue;
            }

            const FT_UInt glyph_index =
                FT_Get_Char_Index(face, codepoint);

            if (
                previous_glyph_index != 0 &&
                glyph_index != 0 &&
                FT_HAS_KERNING(face)
            )
            {
                FT_Vector kerning;
                if (
                    FT_Get_Kerning(
                        face,
                        previous_glyph_index,
                        glyph_index,
                        FT_KERNING_DEFAULT,
                        &kerning
                    ) == FT_Err_Ok
                )
                {
                    pen_x +=
                        static_cast<float>(kerning.x >> 6) *
                        scale;
                }
            }

            if (
                FT_Load_Glyph(
                    face,
                    glyph_index,
                    FT_LOAD_DEFAULT
                ) != FT_Err_Ok
            )
            {
                previous_glyph_index = glyph_index;
                continue;
            }

            if (
                m_weight > 0.0f &&
                face->glyph->format == FT_GLYPH_FORMAT_OUTLINE
            )
            {
                const FT_Pos strength = static_cast<FT_Pos>(
                    (m_weight / m_size) *
                    static_cast<float>(m_resolution) *
                    64.0f
                );
                FT_Outline_Embolden(
                    &face->glyph->outline,
                    strength
                );
            }

            if (
                FT_Render_Glyph(
                    face->glyph,
                    FT_RENDER_MODE_NORMAL
                ) != FT_Err_Ok
            )
            {
                previous_glyph_index = glyph_index;
                continue;
            }

            const FT_Bitmap& bitmap = face->glyph->bitmap;
            if (bitmap.width > 0 && bitmap.rows > 0)
            {
                const uint64_t glyph_pixel_count =
                    static_cast<uint64_t>(bitmap.width) *
                    bitmap.rows;
                if (
                    raster_pixel_count + glyph_pixel_count >
                    max_raster_pixel_count
                )
                {
                    raster_limit_reached = true;
                    break;
                }
                raster_pixel_count += glyph_pixel_count;

                RasterGlyph glyph;
                glyph.width  = bitmap.width;
                glyph.height = bitmap.rows;
                glyph.line   = line_index;
                glyph.x      =
                    pen_x +
                    static_cast<float>(face->glyph->bitmap_left) *
                    scale;
                glyph.y      =
                    -static_cast<float>(line_index) *
                    m_size *
                    m_line_spacing +
                    static_cast<float>(face->glyph->bitmap_top) *
                    scale;
                glyph.pixels.resize(
                    static_cast<size_t>(glyph.width) *
                    glyph.height
                );

                for (uint32_t y = 0; y < glyph.height; y++)
                {
                    const int32_t pitch = bitmap.pitch;
                    const ptrdiff_t row_offset =
                        pitch >= 0
                        ? static_cast<ptrdiff_t>(y) * pitch
                        : static_cast<ptrdiff_t>(
                            glyph.height - 1 - y
                        ) * -pitch;
                    const uint8_t* source =
                        bitmap.buffer + row_offset;

                    for (uint32_t x = 0; x < glyph.width; x++)
                    {
                        uint8_t coverage = 0;

                        if (bitmap.pixel_mode == FT_PIXEL_MODE_GRAY)
                        {
                            coverage = source[x];
                        }
                        else if (
                            bitmap.pixel_mode == FT_PIXEL_MODE_MONO
                        )
                        {
                            coverage =
                                (
                                    source[x >> 3] &
                                    (0x80 >> (x & 7))
                                )
                                ? 255
                                : 0;
                        }
                        else if (
                            bitmap.pixel_mode == FT_PIXEL_MODE_BGRA
                        )
                        {
                            coverage = source[x * 4 + 3];
                        }

                        glyph.pixels[
                            static_cast<size_t>(y) *
                            glyph.width +
                            x
                        ] = coverage;
                    }
                }

                glyphs.push_back(move(glyph));
            }

            pen_x +=
                static_cast<float>(face->glyph->advance.x >> 6) *
                scale +
                m_letter_spacing;
            line_widths[line_index] = pen_x;
            previous_glyph_index    = glyph_index;
        }

        FT_Done_Face(face);
        FT_Done_FreeType(library);

        if (raster_limit_reached)
        {
            SP_LOG_WARNING(
                "3d text exceeded its raster complexity limit"
            );
            ClearMesh();
            m_dirty = false;
            return false;
        }

        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;
        bool geometry_limit_reached = false;
        const size_t estimated_quad_count = min(
            glyphs.size() *
            static_cast<size_t>(m_resolution) *
            3,
            static_cast<size_t>(max_generated_quad_count)
        );
        vertices.reserve(estimated_quad_count * 4);
        indices.reserve(estimated_quad_count * 6);

        for (const RasterGlyph& glyph : glyphs)
        {
            const float alignment_offset = get_alignment_offset(
                m_alignment,
                line_widths[min(
                    glyph.line,
                    static_cast<uint32_t>(line_widths.size() - 1)
                )]
            );

            for (uint32_t y = 0; y < glyph.height; y++)
            {
                uint32_t x = 0;
                while (x < glyph.width)
                {
                    if (!is_solid(glyph, x, y))
                    {
                        x++;
                        continue;
                    }

                    const uint32_t run_start = x;
                    while (
                        x < glyph.width &&
                        is_solid(glyph, x, y)
                    )
                    {
                        x++;
                    }

                    const float x0 =
                        glyph.x +
                        alignment_offset +
                        static_cast<float>(run_start) *
                        scale;
                    const float x1 =
                        glyph.x +
                        alignment_offset +
                        static_cast<float>(x) *
                        scale;
                    const float y1 =
                        glyph.y -
                        static_cast<float>(y) *
                        scale;
                    const float y0 = y1 - scale;
                    const Vector2 uv0(
                        static_cast<float>(run_start) /
                        glyph.width,
                        static_cast<float>(y + 1) /
                        glyph.height
                    );
                    const Vector2 uv1(
                        static_cast<float>(x) /
                        glyph.width,
                        static_cast<float>(y) /
                        glyph.height
                    );

                    add_quad(
                        vertices,
                        indices,
                        Vector3(x0, y0, depth_half),
                        Vector3(x1, y0, depth_half),
                        Vector3(x1, y1, depth_half),
                        Vector3(x0, y1, depth_half),
                        Vector3::Forward,
                        Vector2(uv0.x, uv0.y),
                        Vector2(uv1.x, uv0.y),
                        Vector2(uv1.x, uv1.y),
                        Vector2(uv0.x, uv1.y),
                        geometry_limit_reached
                    );

                    add_quad(
                        vertices,
                        indices,
                        Vector3(x1, y0, -depth_half),
                        Vector3(x0, y0, -depth_half),
                        Vector3(x0, y1, -depth_half),
                        Vector3(x1, y1, -depth_half),
                        Vector3::Backward,
                        Vector2(uv1.x, uv0.y),
                        Vector2(uv0.x, uv0.y),
                        Vector2(uv0.x, uv1.y),
                        Vector2(uv1.x, uv1.y),
                        geometry_limit_reached
                    );
                }
            }

            for (uint32_t y = 0; y < glyph.height; y++)
            {
                for (uint32_t x = 0; x < glyph.width; x++)
                {
                    if (!is_solid(glyph, x, y))
                    {
                        continue;
                    }

                    const float x0 =
                        glyph.x +
                        alignment_offset +
                        static_cast<float>(x) *
                        scale;
                    const float x1 = x0 + scale;
                    const float y1 =
                        glyph.y -
                        static_cast<float>(y) *
                        scale;
                    const float y0 = y1 - scale;
                    const Vector2 side_uv0(0.0f, 0.0f);
                    const Vector2 side_uv1(1.0f, 1.0f);

                    if (
                        !is_solid(
                            glyph,
                            static_cast<int64_t>(x) - 1,
                            y
                        )
                    )
                    {
                        add_quad(
                            vertices,
                            indices,
                            Vector3(x0, y0, -depth_half),
                            Vector3(x0, y0, depth_half),
                            Vector3(x0, y1, depth_half),
                            Vector3(x0, y1, -depth_half),
                            Vector3::Left,
                            side_uv0,
                            Vector2(side_uv1.x, side_uv0.y),
                            side_uv1,
                            Vector2(side_uv0.x, side_uv1.y),
                            geometry_limit_reached
                        );
                    }

                    if (!is_solid(glyph, x + 1, y))
                    {
                        add_quad(
                            vertices,
                            indices,
                            Vector3(x1, y0, depth_half),
                            Vector3(x1, y0, -depth_half),
                            Vector3(x1, y1, -depth_half),
                            Vector3(x1, y1, depth_half),
                            Vector3::Right,
                            side_uv0,
                            Vector2(side_uv1.x, side_uv0.y),
                            side_uv1,
                            Vector2(side_uv0.x, side_uv1.y),
                            geometry_limit_reached
                        );
                    }

                    if (!is_solid(glyph, x, y + 1))
                    {
                        add_quad(
                            vertices,
                            indices,
                            Vector3(x0, y0, -depth_half),
                            Vector3(x1, y0, -depth_half),
                            Vector3(x1, y0, depth_half),
                            Vector3(x0, y0, depth_half),
                            Vector3::Down,
                            side_uv0,
                            Vector2(side_uv1.x, side_uv0.y),
                            side_uv1,
                            Vector2(side_uv0.x, side_uv1.y),
                            geometry_limit_reached
                        );
                    }

                    if (
                        !is_solid(
                            glyph,
                            x,
                            static_cast<int64_t>(y) - 1
                        )
                    )
                    {
                        add_quad(
                            vertices,
                            indices,
                            Vector3(x0, y1, depth_half),
                            Vector3(x1, y1, depth_half),
                            Vector3(x1, y1, -depth_half),
                            Vector3(x0, y1, -depth_half),
                            Vector3::Up,
                            side_uv0,
                            Vector2(side_uv1.x, side_uv0.y),
                            side_uv1,
                            Vector2(side_uv0.x, side_uv1.y),
                            geometry_limit_reached
                        );
                    }
                }
            }
        }

        if (geometry_limit_reached)
        {
            SP_LOG_WARNING(
                "3d text exceeded its generated geometry limit"
            );
            ClearMesh();
            m_dirty = false;
            return false;
        }

        if (vertices.empty() || indices.empty())
        {
            ClearMesh();
            m_dirty = false;
            return false;
        }

        if (
            m_mesh &&
            render &&
            render->GetMesh() == m_mesh.get() &&
            m_mesh->UpdateGeometry(vertices, indices)
        )
        {
            render->SetMesh(m_mesh.get());
            m_dirty = false;

            if (
                !render->GetMaterial() &&
                Renderer::GetStandardMaterial()
            )
            {
                render->SetDefaultMaterial();
            }

            return true;
        }

        shared_ptr<Mesh> mesh = make_shared<Mesh>();
        mesh->SetObjectName("text_3d_mesh");
        mesh->SetFlag(
            static_cast<uint32_t>(MeshFlags::PostProcessOptimize),
            false
        );
        mesh->SetFlag(
            static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale),
            false
        );
        mesh->SetDynamic(true);
        mesh->AddGeometry(vertices, indices, false);
        mesh->CreateGpuBuffers();

        if (!render)
        {
            render = m_entity_ptr->AddComponent<Render>();
            m_created_render = true;
        }
        render->SetMesh(mesh.get());
        m_mesh            = move(mesh);
        m_render_bound    = true;
        m_bound_render_id = render->GetObjectId();
        m_dirty           = false;

        if (
            !render->GetMaterial() &&
            Renderer::GetStandardMaterial()
        )
        {
            render->SetDefaultMaterial();
        }

        return true;
    }

    void Text3D::ClearMesh()
    {
        if (!m_mesh && !m_render_bound)
        {
            return;
        }

        if (m_entity_ptr)
        {
            Render* render = m_entity_ptr->GetComponent<Render>();
            if (
                render &&
                render->GetObjectId() == m_bound_render_id &&
                render->GetMesh() == m_mesh.get()
            )
            {
                if (m_created_render)
                {
                    m_entity_ptr->RemoveComponent<Render>();
                }
                else
                {
                    render->ClearMesh();
                }
            }
        }

        m_mesh.reset();
        m_created_render   = false;
        m_render_bound     = false;
        m_bound_render_id = 0;
        m_dirty            = false;
    }
}
