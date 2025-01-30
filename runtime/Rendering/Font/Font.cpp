/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "../../Resource/Import/FontImporter.h"
#include "../../RHI/RHI_Buffer.h"
#include "../../RHI/RHI_CommandList.h"
//=============================================

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
    }

    Font::Font(const string& file_path, const uint32_t font_size, const Color& color) : IResource(ResourceType::Font)
    {
        for (uint32_t i = 0; i < buffer_count; i++)
        {
            m_buffers_vertex[i] = make_shared<RHI_Buffer>();
            m_buffers_index[i]  = make_shared<RHI_Buffer>();
        }
        m_color = color;

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
        for (const auto& char_info : m_glyphs)
        {
            m_char_max_width    = max(char_info.second.width, m_char_max_width);
            m_char_max_height   = max(char_info.second.height, m_char_max_height);
        }

        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
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
        Vector2 cursor = position;
    
        uint32_t vertex_offset = static_cast<uint32_t>(m_vertices.size());
    
        // generate vertices - draw each latter onto a quad
        for (char character : text)
        {
            Glyph& glyph = m_glyphs[character];
    
            if (character == ASCII_TAB)
            {
                // compute the width of a single space
                const float space_offset = static_cast<float>(m_glyphs[ASCII_SPACE].horizontal_advance);
                const float tab_spacing  = space_offset * 4.0f; // 4 spaces per tab
    
                // calculate the next tab stop
                float next_tab_stop = std::floor((cursor.x + tab_spacing) / tab_spacing) * tab_spacing;
    
                // advance the cursor to the next tab stop
                cursor.x = next_tab_stop;
            }
            else if (character == ASCII_NEW_LINE)
            {
                cursor.x  = position.x;
                cursor.y -= m_char_max_height;
            }
            else if (character == ASCII_SPACE)
            {
                cursor.x += glyph.horizontal_advance;
            }
            else
            {
                // first triangle in quad
                m_vertices.push_back({cursor.x + glyph.offset_x,               cursor.y + glyph.offset_y,                0.0f, glyph.uv_x_left,  glyph.uv_y_top});
                m_vertices.push_back({cursor.x + glyph.offset_x + glyph.width, cursor.y + glyph.offset_y - glyph.height, 0.0f, glyph.uv_x_right, glyph.uv_y_bottom});
                m_vertices.push_back({cursor.x + glyph.offset_x,               cursor.y + glyph.offset_y - glyph.height, 0.0f, glyph.uv_x_left,  glyph.uv_y_bottom});
    
                // second triangle in quad
                m_vertices.push_back({cursor.x + glyph.offset_x,               cursor.y + glyph.offset_y,                0.0f, glyph.uv_x_left,  glyph.uv_y_top});
                m_vertices.push_back({cursor.x + glyph.offset_x + glyph.width, cursor.y + glyph.offset_y,                0.0f, glyph.uv_x_right, glyph.uv_y_top});
                m_vertices.push_back({cursor.x + glyph.offset_x + glyph.width, cursor.y + glyph.offset_y - glyph.height, 0.0f, glyph.uv_x_right, glyph.uv_y_bottom});
    
                // add indices for the two triangles (6 indices for 2 triangles)
                for (uint32_t i = 0; i < 6; ++i)
                {
                    m_indices.push_back(vertex_offset + i);
                }
    
                // advance the cursor and vertex offset
                cursor.x      += glyph.horizontal_advance;
                vertex_offset += 6;
            }
        }
    }

    bool Font::HasText() const
    {
        return !m_vertices.empty() && !m_indices.empty();
    }

    void Font::SetSize(const uint32_t size)
    {
        m_font_size = clamp<uint32_t>(size, 8, 50);
    }

    void Font::UpdateVertexAndIndexBuffers(RHI_CommandList* cmd_list)
    {
        SP_ASSERT(HasText());
    
        m_buffer_index = (m_buffer_index + 1) % buffer_count;

        // grow gpu buffers if needed
        {
            if (m_vertices.size() > m_buffers_vertex[m_buffer_index]->GetElementCount())
            {
                m_buffers_vertex[m_buffer_index] = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Vertex,                  // type
                    sizeof(m_vertices[0]),                    // stride
                    static_cast<uint32_t>(m_vertices.size()), // element count
                    m_vertices.data(),                        // data
                    true,                                     // mappable
                    "font_vertex"
                );
            }
    
            if (m_indices.size() > m_buffers_index[m_buffer_index]->GetElementCount())
            {
                m_buffers_index[m_buffer_index] = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Index,                  // type
                    sizeof(m_indices[0]),                    // stride
                    static_cast<uint32_t>(m_indices.size()), // element count
                    m_indices.data(),                        // data
                    true,                                    // mappable
                    "font_index"
                );
            }
        }

        // map vertices and indices to gpu buffers
        cmd_list->UpdateBuffer(m_buffers_vertex[m_buffer_index].get(), 0, m_buffers_vertex[m_buffer_index]->GetObjectSize(), m_vertices.data(), true);
        cmd_list->UpdateBuffer(m_buffers_index[m_buffer_index].get(),  0, m_buffers_index[m_buffer_index]->GetObjectSize(),  m_indices.data(),  true);

        // clear vertices and indices
        m_vertices.clear();
        m_indices.clear();
    }

    uint32_t Font::GetIndexCount()
    {
        return m_buffers_index[m_buffer_index]->GetElementCount();
    }
}
