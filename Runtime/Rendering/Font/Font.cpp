/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==================================
#include "Spartan.h"
#include "Font.h"
#include "../Renderer.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../RHI/RHI_VertexBuffer.h"
#include "../../RHI/RHI_IndexBuffer.h"
#include "../../Resource/ResourceCache.h"
#include "../../Resource/Import/FontImporter.h"
#include "../../Core/Stopwatch.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

#define ASCII_TAB       9
#define ASCII_NEW_LINE  10
#define ASCII_SPACE     32

namespace Spartan
{
    Font::Font(Context* context, const string& file_path, const int font_size, const Vector4& color) : IResource(context, ResourceType::Font)
    {
        m_rhi_device        = m_context->GetSubsystem<Renderer>()->GetRhiDevice();
        m_vertex_buffer     = make_shared<RHI_VertexBuffer>(m_rhi_device);
        m_index_buffer      = make_shared<RHI_IndexBuffer>(m_rhi_device);
        m_char_max_width    = 0;
        m_char_max_height   = 0;
        m_color             = color;
        
        SetSize(font_size);
        Font::LoadFromFile(file_path);
    }

    bool Font::SaveToFile(const string& file_path)
    {
        return true;
    }

    bool Font::LoadFromFile(const string& file_path)
    {
        if (!m_context)
            return false;

        const Stopwatch timer;

        // Load
        if (!m_context->GetSubsystem<ResourceCache>()->GetFontImporter()->LoadFromFile(this, file_path))
        {
            LOG_ERROR("Failed to load font \"%s\"", file_path.c_str());
            return false;
        }

        // Find max character height (todo, actually get spacing from FreeType)
        for (const auto& char_info : m_glyphs)
        {
            m_char_max_width    = Helper::Max<int>(char_info.second.width, m_char_max_width);
            m_char_max_height   = Helper::Max<int>(char_info.second.height, m_char_max_height);
        }
        
        LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
        return true;
    }

    void Font::SetText(const string& text, const Vector2& position)
    {
        const bool same_text    = text == m_current_text;
        const bool has_buffers  = (m_vertex_buffer && m_index_buffer);

        if (same_text || !has_buffers)
            return;

        Vector2 pen = position;
        m_current_text = text;
        m_vertices.clear();

        // Draw each letter onto a quad.
        for (auto text_char : m_current_text)
        {
            Glyph& glyph = m_glyphs[text_char];

            if (text_char == ASCII_TAB)
            {
                const uint32_t space_offset         = m_glyphs[ASCII_SPACE].horizontal_advance;
                const uint32_t space_count          = 8; // spaces in a typical terminal
                const uint32_t tab_spacing          = space_offset * space_count;
                const uint32_t offset_from_start    = static_cast<uint32_t>(Math::Helper::Abs(pen.x - position.x));
                const uint32_t next_column_index    = (offset_from_start / tab_spacing) + 1;
                const uint32_t offset_to_column     = (next_column_index * tab_spacing) - offset_from_start;
                pen.x                               += offset_to_column;
            }
            else if (text_char == ASCII_NEW_LINE)
            {
                pen.y -= m_char_max_height;
                pen.x = position.x;
            }
            else if (text_char == ASCII_SPACE)
            {
                // Advance
                pen.x += glyph.horizontal_advance;
            }
            else // Any other char
            {
                // First triangle in quad.        
                m_vertices.emplace_back(pen.x + glyph.offset_x,                 pen.y + glyph.offset_y,                  0.0f, glyph.uv_x_left,  glyph.uv_y_top);       // top left
                m_vertices.emplace_back(pen.x + glyph.offset_x  + glyph.width,  pen.y + glyph.offset_y - glyph.height,   0.0f, glyph.uv_x_right, glyph.uv_y_bottom);    // bottom right
                m_vertices.emplace_back(pen.x + glyph.offset_x,                 pen.y + glyph.offset_y - glyph.height,   0.0f, glyph.uv_x_left,  glyph.uv_y_bottom);    // bottom left
                // Second triangle in quad.
                m_vertices.emplace_back(pen.x + glyph.offset_x,                 pen.y + glyph.offset_y,                  0.0f, glyph.uv_x_left,  glyph.uv_y_top);       // top left
                m_vertices.emplace_back(pen.x + glyph.offset_x  + glyph.width,  pen.y + glyph.offset_y,                  0.0f, glyph.uv_x_right, glyph.uv_y_top);       // top right
                m_vertices.emplace_back(pen.x + glyph.offset_x  + glyph.width,  pen.y + glyph.offset_y - glyph.height,   0.0f, glyph.uv_x_right, glyph.uv_y_bottom);    // bottom right

                // Advance
                pen.x += glyph.horizontal_advance;
            }
        }
        m_vertices.shrink_to_fit();
        
        m_indices.clear();
        for (uint32_t i = 0; i < m_vertices.size(); i++)
        {
            m_indices.emplace_back(i);
        }

        UpdateBuffers(m_vertices, m_indices);
    }

    void Font::SetSize(const uint32_t size)
    {
        m_font_size = Helper::Clamp<uint32_t>(size, 8, 50);
    }

    bool Font::UpdateBuffers(vector<RHI_Vertex_PosTex>& vertices, vector<uint32_t>& indices) const
    {
        if (!m_context || !m_vertex_buffer || !m_index_buffer)
        {
            LOG_ERROR_INVALID_INTERNALS();
            return false;
        }

        // Grow buffers (if needed)
        if (vertices.size() > m_vertex_buffer->GetVertexCount())
        {
            // Vertex buffer
            if (!m_vertex_buffer->CreateDynamic<RHI_Vertex_PosTex>(static_cast<uint32_t>(vertices.size())))
            {
                LOG_ERROR("Failed to update vertex buffer.");
                return false;
            }

            // Index buffer
            if (!m_index_buffer->CreateDynamic<uint32_t>(static_cast<uint32_t>(indices.size())))
            {
                LOG_ERROR("Failed to update index buffer.");
                return false;
            }
        }

        bool mapped_vertex = false;
        if (const auto vertex_buffer = static_cast<RHI_Vertex_PosTex*>(m_vertex_buffer->Map()))
        {
            copy(vertices.begin(), vertices.end(), vertex_buffer);
            mapped_vertex = m_vertex_buffer->Unmap();
        }

        bool mapped_index = false;
        if (const auto index_buffer = static_cast<uint32_t*>(m_index_buffer->Map()))
        {
            copy(indices.begin(), indices.end(), index_buffer);
            mapped_index = m_index_buffer->Unmap();
        }

        return mapped_vertex && mapped_index;
    }
}
