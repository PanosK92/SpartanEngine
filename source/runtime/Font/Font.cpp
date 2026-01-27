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

//= INCLUDES ===============================
#include "pch.h"
#include "Font.h"
#include "../Rendering/Renderer.h"
#include "../Resource/Import/FontImporter.h"
#include "../RHI/RHI_Buffer.h"
#include "../RHI/RHI_CommandList.h"
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
            m_char_max_width  = max(char_info.second.width, m_char_max_width);
            m_char_max_height = max(char_info.second.height, m_char_max_height);
        }

        SP_LOG_INFO("Loading \"%s\" took %d ms", FileSystem::GetFileNameFromFilePath(file_path).c_str(), static_cast<int>(timer.GetElapsedTimeMs()));
    }

    void Font::AddText(const char* text, const Vector2& position_screen_percentage)
    {
        // define a maximum vertex limit
        const uint32_t max_vertices = 100'000;
        uint32_t vertex_offset      = static_cast<uint32_t>(m_vertices.size());

        const float viewport_width  = Renderer::GetViewport().width;
        const float viewport_height = Renderer::GetViewport().height;

        // convert screen percentage to pixel coordinates
        Vector2 position;
        position.x = viewport_width  * position_screen_percentage.x;
        position.y = viewport_height * (-position_screen_percentage.y); // flip y-axis to match the screen space coordinates (y is positive downwards in screen space)

        // make the origin be the top left corner
        position.x -= 0.5f * viewport_width;
        position.y += 0.5f * viewport_height;
    
        // generate vertices - draw each letter onto a quad
        Vector2 cursor = position;
        for (const char* p = text; *p != '\0'; ++p)
        {
            char character = *p;

            // check if adding this character would exceed the vertex limit
            if (m_vertices.size() + 6 > max_vertices)
                return;
    
            Glyph& glyph = m_glyphs[character];
    
            if (character == ASCII_TAB)
            {
                // use max character width for consistent tab stops (works reliably across all resolutions)
                const float tab_spacing  = static_cast<float>(m_char_max_width) * 4.0f;
                float relative_x         = cursor.x - position.x;
                float next_tab_stop      = (floor(relative_x / tab_spacing) + 1.0f) * tab_spacing;
                cursor.x                 = position.x + next_tab_stop;
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
    
                // add indices for the two triangles
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
        m_buffer_index = (m_buffer_index + 1) % buffer_count;
    
        // grow gpu buffers if needed
        {
            // compute how many bytes we need
            uint64_t vertex_data_size = static_cast<uint64_t>(m_vertices.size()) * sizeof(m_vertices[0]);
            uint64_t index_data_size  = static_cast<uint64_t>(m_indices.size())  * sizeof(m_indices[0]);
            
            // grow vertex buffer if needed
            if (vertex_data_size > m_buffers_vertex[m_buffer_index]->GetStride())
            {
                m_buffers_vertex[m_buffer_index] = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Vertex,
                    static_cast<uint32_t>(vertex_data_size), // stride = total size in bytes
                    1,                                       // element count = 1
                    nullptr,
                    true,
                    "font_vertex"
                );
            }
            
            // grow index buffer if needed
            if (index_data_size > m_buffers_index[m_buffer_index]->GetStride())
            {
                m_buffers_index[m_buffer_index] = make_shared<RHI_Buffer>(
                    RHI_Buffer_Type::Index,
                    static_cast<uint32_t>(index_data_size), // stride = total size in bytes
                    1,
                    nullptr,
                    true,
                    "font_index"
                );
            }
        }
    
        // map vertices and indices to gpu buffers
        uint64_t vertex_data_size = static_cast<uint64_t>(m_vertices.size()) * sizeof(m_vertices[0]);
        cmd_list->UpdateBuffer(m_buffers_vertex[m_buffer_index].get(), 0, vertex_data_size, m_vertices.data());
        
        uint64_t index_data_size = static_cast<uint64_t>(m_indices.size()) * sizeof(m_indices[0]);
        cmd_list->UpdateBuffer(m_buffers_index[m_buffer_index].get(), 0, index_data_size, m_indices.data());
    
        // store the used index count
        m_index_count[m_buffer_index] = static_cast<uint32_t>(m_indices.size());
    
        // clear vertices and indices
        m_vertices.clear();
        m_indices.clear();
    }

    uint32_t Font::GetIndexCount()
    {
        return m_index_count[m_buffer_index];
    }
}
