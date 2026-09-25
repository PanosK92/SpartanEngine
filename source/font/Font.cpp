/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#include "Font.h"
#include "../rendering/Renderer.h"
#include "../rhi/RHI_Viewport.h"
#include "../resource/import/FontImporter.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_CommandList.h"
//==========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        const uint8_t ASCII_TAB      = 9;
        const uint8_t ASCII_NEW_LINE = 10;
        const uint8_t ASCII_SPACE    = 32;

        // the shader treats a negative u as a solid quad that ignores the atlas
        const float uv_solid = -1.0f;

        // r8g8b8a8_unorm, red in the lowest byte
        uint32_t pack_color(const Color& color)
        {
            auto to_byte = [](const float value)
            {
                return static_cast<uint32_t>(clamp(value, 0.0f, 1.0f) * 255.0f + 0.5f);
            };

            return to_byte(color.r) | (to_byte(color.g) << 8) | (to_byte(color.b) << 16) | (to_byte(color.a) << 24);
        }

        bool is_digit(const char character)
        {
            return character >= '0' && character <= '9';
        }
    }

    Font::Font(const string& file_path, const uint32_t font_size, const Color& color, const Font_Outline_Type outline) : IResource(ResourceType::Font)
    {
        for (uint32_t i = 0; i < buffer_count; i++)
        {
            m_buffers_vertex[i] = make_shared<RHI_Buffer>();
            m_buffers_index[i]  = make_shared<RHI_Buffer>();
        }
        m_color   = color;
        m_outline = outline;

        SetSize(font_size);
        LoadFromFile(file_path);
    }

    void Font::SaveToFile(const string& file_path)
    {

    }

    void Font::LoadFromFile(const string& file_path)
    {
        const Stopwatch timer;

        // load
        if (!FontImporter::LoadFromFile(this, file_path))
        {
            SP_LOG_ERROR("Failed to load font \"%s\"", file_path.c_str());
            return;
        }

        // find max character height (todo, actually get spacing from FreeType)
        for (const auto& [char_code, glyph] : m_glyphs)
        {
            m_char_max_width  = max(glyph.width, m_char_max_width);
            m_char_max_height = max(glyph.height, m_char_max_height);
            m_ascent          = max(glyph.offset_y, m_ascent);
            m_descent         = max(static_cast<int32_t>(glyph.height) - glyph.offset_y, m_descent);

            if (char_code >= '0' && char_code <= '9')
            {
                m_digit_advance = max(glyph.horizontal_advance, m_digit_advance);
            }
        }

        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
    }

    void Font::add_quad(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, const uint32_t color)
    {
        // pixels have a top left origin with y down, the orthographic projection is centered with y up
        const float half_width  = 0.5f * Renderer::GetViewport().width;
        const float half_height = 0.5f * Renderer::GetViewport().height;
        x0 -= half_width;
        x1 -= half_width;
        y0  = half_height - y0;
        y1  = half_height - y1;

        auto push_vertex = [this, color](const float x, const float y, const float u, const float v)
        {
            RHI_Vertex_Pos2dTexCol8& vertex = m_vertices.emplace_back();
            vertex.pos[0] = x;
            vertex.pos[1] = y;
            vertex.tex[0] = u;
            vertex.tex[1] = v;
            vertex.col    = color;
        };

        const uint32_t vertex_offset = static_cast<uint32_t>(m_vertices.size());
        push_vertex(x0, y0, u0, v0);
        push_vertex(x1, y1, u1, v1);
        push_vertex(x0, y1, u0, v1);
        push_vertex(x1, y0, u1, v0);

        // same winding as before, front facing under back face culling
        const uint32_t quad_indices[6] = { 0, 1, 2, 0, 3, 1 };
        for (const uint32_t index : quad_indices)
        {
            m_indices.push_back(vertex_offset + index);
        }
    }

    void Font::add_text_baseline(const char* text, float x, float y, const uint32_t color, const bool tabular_digits)
    {
        // define a maximum vertex limit
        const uint32_t max_vertices = 100'000;

        // whole pixels keep the atlas texels aligned with the screen, so glyphs stay sharp
        const float origin_x = floor(x + 0.5f);
        float cursor_x       = origin_x;
        float cursor_y       = floor(y + 0.5f);

        for (const char* p = text; *p != '\0'; ++p)
        {
            const char character = *p;

            if (m_vertices.size() + 4 > max_vertices)
            {
                return;
            }

            const Glyph& glyph = m_glyphs[character];

            if (character == ASCII_TAB)
            {
                // use max character width for consistent tab stops (works reliably across all resolutions)
                const float tab_spacing   = static_cast<float>(m_char_max_width) * 4.0f;
                const float relative_x    = cursor_x - origin_x;
                const float next_tab_stop = (floor(relative_x / tab_spacing) + 1.0f) * tab_spacing;
                cursor_x                  = origin_x + next_tab_stop;
            }
            else if (character == ASCII_NEW_LINE)
            {
                cursor_x  = origin_x;
                cursor_y += m_char_max_height;
            }
            else if (character == ASCII_SPACE)
            {
                cursor_x += glyph.horizontal_advance;
            }
            else
            {
                const bool tabular = tabular_digits && is_digit(character);
                const float advance = static_cast<float>(tabular ? m_digit_advance : glyph.horizontal_advance);
                const float center  = tabular ? floor((advance - static_cast<float>(glyph.horizontal_advance)) * 0.5f) : 0.0f;

                const float left = cursor_x + center + glyph.offset_x;
                const float top  = cursor_y - glyph.offset_y;
                add_quad(left, top, left + glyph.width, top + glyph.height, glyph.uv_x_left, glyph.uv_y_top, glyph.uv_x_right, glyph.uv_y_bottom, color);

                cursor_x += advance;
            }
        }
    }

    void Font::AddText(const char* text, const Vector2& position_screen_percentage)
    {
        const float x = Renderer::GetViewport().width  * position_screen_percentage.x;
        const float y = Renderer::GetViewport().height * position_screen_percentage.y;
        add_text_baseline(text, x, y, pack_color(m_color), false);
    }

    void Font::AddText(const char* text, const Vector2& position_pixels, const Color& color, const bool tabular_digits)
    {
        add_text_baseline(text, position_pixels.x, position_pixels.y + static_cast<float>(m_ascent), pack_color(color), tabular_digits);
    }

    float Font::GetTextWidth(const char* text, const bool tabular_digits)
    {
        float width = 0.0f;
        for (const char* p = text; *p != '\0' && *p != ASCII_NEW_LINE; ++p)
        {
            const bool tabular = tabular_digits && is_digit(*p);
            width += static_cast<float>(tabular ? m_digit_advance : m_glyphs[*p].horizontal_advance);
        }

        return width;
    }

    void Font::AddRect(const Vector2& min_pixels, const Vector2& max_pixels, const Color& color)
    {
        if (color.a <= 0.0f || max_pixels.x <= min_pixels.x || max_pixels.y <= min_pixels.y)
        {
            return;
        }

        add_quad(min_pixels.x, min_pixels.y, max_pixels.x, max_pixels.y, uv_solid, uv_solid, uv_solid, uv_solid, pack_color(color));
    }

    bool Font::HasText() const
    {
        // the screenshot and the output both draw text in the same frame, the second one reuses the upload
        return !m_vertices.empty() || (m_upload_frame == Renderer::GetFrameNumber() && m_index_count[m_buffer_index] != 0);
    }

    void Font::SetSize(const uint32_t size)
    {
        m_font_size = clamp<uint32_t>(size, 8, 50);
    }

    void Font::UpdateVertexAndIndexBuffers()
    {
        if (m_vertices.empty())
        {
            return;
        }

        m_buffer_index = (m_buffer_index + 1) % buffer_count;
        m_upload_frame = Renderer::GetFrameNumber();

        const uint32_t vertex_stride = static_cast<uint32_t>(sizeof(m_vertices[0]));
        const uint32_t index_stride  = static_cast<uint32_t>(sizeof(m_indices[0]));

        // grow gpu buffers if needed, capacity is tracked via element count, stride stays per-element
        // so d3d12's IASetVertexBuffers receives a valid stride that fits within the 2048 byte limit
        {
            uint64_t vertex_capacity = static_cast<uint64_t>(m_buffers_vertex[m_buffer_index]->GetStride()) * m_buffers_vertex[m_buffer_index]->GetElementCount();
            uint64_t vertex_needed   = static_cast<uint64_t>(m_vertices.size()) * vertex_stride;
            if (vertex_needed > vertex_capacity)
            {
                m_buffers_vertex[m_buffer_index] = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Vertex,
                    vertex_stride,
                    static_cast<uint32_t>(m_vertices.size()),
                    nullptr,
                    true,
                    "font_vertex"
                );
            }

            uint64_t index_capacity = static_cast<uint64_t>(m_buffers_index[m_buffer_index]->GetStride()) * m_buffers_index[m_buffer_index]->GetElementCount();
            uint64_t index_needed   = static_cast<uint64_t>(m_indices.size()) * index_stride;
            if (index_needed > index_capacity)
            {
                m_buffers_index[m_buffer_index] = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Index,
                    index_stride,
                    static_cast<uint32_t>(m_indices.size()),
                    nullptr,
                    true,
                    "font_index"
                );
            }
        }

        uint64_t vertex_data_size = static_cast<uint64_t>(m_vertices.size()) * vertex_stride;
        RHI_CommandList::UpdateBuffer(m_buffers_vertex[m_buffer_index].get(), 0, vertex_data_size, m_vertices.data());

        uint64_t index_data_size = static_cast<uint64_t>(m_indices.size()) * index_stride;
        RHI_CommandList::UpdateBuffer(m_buffers_index[m_buffer_index].get(), 0, index_data_size, m_indices.data());

        m_index_count[m_buffer_index] = static_cast<uint32_t>(m_indices.size());

        m_vertices.clear();
        m_indices.clear();
    }

    uint32_t Font::GetIndexCount()
    {
        return m_index_count[m_buffer_index];
    }
}
