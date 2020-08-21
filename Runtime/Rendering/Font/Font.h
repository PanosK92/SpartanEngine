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

#pragma once

//= INCLUDES ==============================
#include <memory>
#include <unordered_map>
#include "Glyph.h"
#include "../../RHI/RHI_Definition.h"
#include "../../Resource/IResource.h"
#include "../../Math/Vector4.h"
#include "../../Core/Spartan_Definitions.h"
//=========================================

namespace Spartan
{
    namespace Math
    {
        class Vector2;
    }

    enum Font_Hinting_Type
    {
        Font_Hinting_None,
        Font_Hinting_Light,
        Font_Hinting_Normal
    };

    enum Font_Outline_Type
    {
        Font_Outline_None,
        Font_Outline_Edge,
        Font_Outline_Positive,
        Font_Outline_Negative
    };

    class SPARTAN_CLASS Font : public IResource
    {
    public:
        Font(Context* context, const std::string& file_path, int font_size, const Math::Vector4& color);
        ~Font() = default;

        //= RESOURCE INTERFACE =================================
        bool SaveToFile(const std::string& file_path) override;
        bool LoadFromFile(const std::string& file_path) override;
        //======================================================

        void SetText(const std::string& text, const Math::Vector2& position);
        void SetSize(uint32_t size);

        const Math::Vector4& GetColor()                                 const { return m_color; }
        void SetColor(const Math::Vector4& color)                             { m_color = color; }

        const Math::Vector4& GetColorOutline()                          const { return m_color_outline; }
        void SetColorOutline(const Math::Vector4& color)                      { m_color_outline = color; }

        void SetOutline(const Font_Outline_Type outline)                      { m_outline = outline; }
        const Font_Outline_Type GetOutline()                            const { return m_outline; }

        void SetOutlineSize(const uint32_t outline_size)                      { m_outline_size = outline_size; }
        const uint32_t GetOutlineSize()                                 const { return m_outline_size; }

        const auto& GetAtlas()                                          const { return m_atlas; }
        void SetAtlas(const std::shared_ptr<RHI_Texture>& atlas)              { m_atlas = atlas; }

        const auto& GetAtlasOutline()                                   const { return m_atlas_outline; }
        void SetAtlasOutline(const std::shared_ptr<RHI_Texture>& atlas)       { m_atlas_outline = atlas; }

        RHI_IndexBuffer* GetIndexBuffer()                               const { return m_index_buffer.get(); }
        RHI_VertexBuffer* GetVertexBuffer()                             const { return m_vertex_buffer.get(); }
        uint32_t GetIndexCount()                                        const { return static_cast<uint32_t>(m_indices.size()); }
        uint32_t GetSize()                                              const { return m_font_size; }
        void SetGlyph(const uint32_t char_code, const Glyph& glyph)              { m_glyphs[char_code] = glyph; }
        Font_Hinting_Type GetHinting()                                  const { return m_hinting; }
        auto GetForceAutohint()                                         const { return m_force_autohint; }
            
    private:    
        bool UpdateBuffers(std::vector<RHI_Vertex_PosTex>& vertices, std::vector<uint32_t>& indices) const;

        uint32_t m_font_size            = 14;
        uint32_t m_outline_size         = 2;
        bool m_force_autohint           = false;
        Font_Hinting_Type m_hinting     = Font_Hinting_Normal;
        Font_Outline_Type m_outline     = Font_Outline_Positive;
        Math::Vector4 m_color           = Math::Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        Math::Vector4 m_color_outline   = Math::Vector4(0.0f, 0.0f, 0.0f, 1.0f);
        std::string m_current_text;
        uint32_t m_char_max_width;
        uint32_t m_char_max_height;
        std::shared_ptr<RHI_Texture> m_atlas;
        std::shared_ptr<RHI_Texture> m_atlas_outline;
        std::unordered_map<uint32_t, Glyph> m_glyphs;
        std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer;
        std::shared_ptr<RHI_IndexBuffer> m_index_buffer;
        std::vector<RHI_Vertex_PosTex> m_vertices;
        std::vector<uint32_t> m_indices;    
        std::shared_ptr<RHI_Device> m_rhi_device;
    };
}
