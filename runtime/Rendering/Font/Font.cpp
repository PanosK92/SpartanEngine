/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "pch.h"
#include "Font.h"
#include "../Renderer.h"
#include "../../Core/Stopwatch.h"
#include "../../Resource/ResourceCache.h"
#include "../../Resource/Import/FontImporter.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../RHI/RHI_VertexBuffer.h"
#include "../../RHI/RHI_IndexBuffer.h"
//=============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        constexpr uint8_t ASCII_TAB      = 9;
        constexpr uint8_t ASCII_NEW_LINE = 10;
        constexpr uint8_t ASCII_SPACE    = 32;
    }

    Font::Font(const string& file_path, const uint32_t font_size, const Color& color) : IResource(ResourceType::Font)
    {
        m_vertex_buffer   = make_shared<RHI_VertexBuffer>(true, "font");
        m_index_buffer    = make_shared<RHI_IndexBuffer>(true, "font");
        m_char_max_width  = 0;
        m_char_max_height = 0;
        m_color           = color;

        SetSize(font_size);
        LoadFromFile(file_path);
    }

    bool Font::SaveToFile(const string& file_path)
    {
        return true;
    }

    bool Font::LoadFromFile(const string& file_path)
    {
        const Stopwatch timer;

        // load
        if (!FontImporter::LoadFromFile(this, file_path))
        {
            SP_LOG_ERROR("Failed to load font \"%s\"", file_path.c_str());
            return false;
        }

        // find max character height (todo, actually get spacing from FreeType)
        for (const auto& char_info : m_glyphs)
        {
            m_char_max_width    = Helper::Max<int>(char_info.second.width, m_char_max_width);
            m_char_max_height   = Helper::Max<int>(char_info.second.height, m_char_max_height);
        }

        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
        return true;
    }

    void Font::AddText(const string& text, const Vector2& position_screen_percentage)
    {
        const float viewport_width  = Renderer::GetViewport().width;
        const float viewport_height = Renderer::GetViewport().height;
        const float aspect_ratio    = viewport_width / viewport_height;

        // adjust the screen percentage position to compensate for the aspect ratio
        Vector2 adjusted_position_percentage;
        adjusted_position_percentage.x = position_screen_percentage.x / aspect_ratio;
        adjusted_position_percentage.y = position_screen_percentage.y;

        // convert the adjusted screen percentage position to actual screen coordinates
        Vector2 position;
        position.x = viewport_width  * adjusted_position_percentage.x;
        position.y = viewport_height * adjusted_position_percentage.y;

        // make the origin be the top left corner
        position.x -= 0.5f * viewport_width;
        position.y += 0.5f * viewport_height;

        // don't yet understand why this is needed, but it corrects a slight y offset
        position.y -= m_char_max_height * 1.5f;

        // set the cursor to the starting position
        Vector2 cursor      = position;
        float starting_pos_x = cursor.x;

        // generate vertices - draw each latter onto a quad
        vector<RHI_Vertex_PosTex> vertices;
        for (char character : text)
        {
            Glyph& glyph = m_glyphs[character];

            if (character == ASCII_TAB)
            {
                const uint32_t space_offset = m_glyphs[ASCII_SPACE].horizontal_advance;
                const uint32_t tab_spacing  = space_offset * 4; // spaces in a typical editor
                float offset_from_start     = cursor.x - starting_pos_x; // keep as float for precision
                uint32_t next_column_index  = tab_spacing == 0 ? 4 : static_cast<uint32_t>((offset_from_start / tab_spacing) + 1);
                float offset_to_column      = (next_column_index * tab_spacing) - offset_from_start;
                cursor.x                    += offset_to_column; // apply offset to align to next tab stop
            }
            else if (character == ASCII_NEW_LINE)
            {
                cursor.y -= m_char_max_height;
                cursor.x = starting_pos_x;
            }
            else if (character == ASCII_SPACE)
            {
                cursor.x += glyph.horizontal_advance;
            }
            else
            {
                // first triangle in quad.
                vertices.emplace_back(cursor.x + glyph.offset_x,                cursor.y + glyph.offset_y,                0.0f, glyph.uv_x_left,  glyph.uv_y_top);    // top left
                vertices.emplace_back(cursor.x + glyph.offset_x + glyph.width,  cursor.y + glyph.offset_y - glyph.height, 0.0f, glyph.uv_x_right, glyph.uv_y_bottom); // bottom right
                vertices.emplace_back(cursor.x + glyph.offset_x,                cursor.y + glyph.offset_y - glyph.height, 0.0f, glyph.uv_x_left,  glyph.uv_y_bottom); // bottom left

                // second triangle in quad
                vertices.emplace_back(cursor.x + glyph.offset_x,                cursor.y + glyph.offset_y,                0.0f, glyph.uv_x_left,  glyph.uv_y_top);    // top left
                vertices.emplace_back(cursor.x + glyph.offset_x  + glyph.width, cursor.y + glyph.offset_y,                0.0f, glyph.uv_x_right, glyph.uv_y_top);    // top right
                vertices.emplace_back(cursor.x + glyph.offset_x  + glyph.width, cursor.y + glyph.offset_y - glyph.height, 0.0f, glyph.uv_x_right, glyph.uv_y_bottom); // bottom right

                // advance
                cursor.x += glyph.horizontal_advance;
            }
        }

        // generate indices
        vector<uint32_t> indices;
        for (uint32_t i = 0; i < static_cast<uint32_t>(vertices.size()); i++)
        {
            indices.emplace_back(i);
        }

        // store the generated data for this text
        m_text_data.emplace_back(vertices, indices, position);
    }

    bool Font::HasText() const
    {
        return !m_text_data.empty();
    }

    void Font::SetSize(const uint32_t size)
    {
        m_font_size = Helper::Clamp<uint32_t>(size, 8, 50);
    }

    void Font::UpdateVertexAndIndexBuffers()
    {
        SP_ASSERT(m_vertex_buffer && m_index_buffer);

        if (m_text_data.empty())
            return;

        // combine all vertices/indices into one
        vector<RHI_Vertex_PosTex> vertices;
        vector<uint32_t> indices;
        uint32_t vertex_offset = 0;
        for (const TextData& text_data : m_text_data)
        {
            vertices.insert(vertices.end(), text_data.vertices.begin(), text_data.vertices.end());

            for (uint32_t index : text_data.indices)
            {
                indices.push_back(index); // index + vertex_offset causes issues for some weird compiler related reason
            }

            vertex_offset += static_cast<uint32_t>(text_data.vertices.size());
        }

        // create/grow buffers
        if (vertices.size() > m_vertex_buffer->GetVertexCount())
        {
            m_vertex_buffer->CreateDynamic<RHI_Vertex_PosTex>(static_cast<uint32_t>(vertices.size()));
            m_index_buffer->CreateDynamic<uint32_t>(static_cast<uint32_t>(indices.size()));
        }

        // copy the data over to the gpu
        {
            if (RHI_Vertex_PosTex* vertex_buffer = static_cast<RHI_Vertex_PosTex*>(m_vertex_buffer->GetMappedData()))
            {
                copy(vertices.begin(), vertices.end(), vertex_buffer);
            }

            if (uint32_t* index_buffer = static_cast<uint32_t*>(m_index_buffer->GetMappedData()))
            {
                copy(indices.begin(), indices.end(), index_buffer);
            }
        }

        m_text_data.clear();
    }

    uint32_t Font::GetIndexCount()
    {
        SP_ASSERT(m_index_buffer);
        return m_index_buffer->GetIndexCount();
    }
}
