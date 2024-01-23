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

#pragma once

//= INCLUDES =========================
#include <memory>
#include <unordered_map>
#include "Glyph.h"
#include "../Color.h"
#include "../../RHI/RHI_Definitions.h"
#include "../../Resource/IResource.h"
#include "../../Core/Definitions.h"
//====================================

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

    struct TextData
    {
        std::vector<RHI_Vertex_PosTex> vertices;
        std::vector<uint32_t> indices;
        Math::Vector2 position;
    };

    class SP_CLASS Font : public IResource
    {
    public:
        Font(const std::string& file_path, const uint32_t font_size, const Color& color);
        ~Font() = default;

        // iresource
        bool SaveToFile(const std::string& file_path) override;
        bool LoadFromFile(const std::string& file_path) override;

        // text
        void AddText(const std::string& text, const Math::Vector2& position_screen_percentage);
        bool HasText() const;

        // color
        const Color& GetColor() const     { return m_color; }
        void SetColor(const Color& color) { m_color = color; }

        // color outline
        const Color& GetColorOutline() const     { return m_color_outline; }
        void SetColorOutline(const Color& color) { m_color_outline = color; }

        // outline
        void SetOutline(const Font_Outline_Type outline) { m_outline = outline; }
        const Font_Outline_Type GetOutline() const       { return m_outline; }

        // outline size
        void SetOutlineSize(const uint32_t outline_size) { m_outline_size = outline_size; }
        const uint32_t GetOutlineSize() const            { return m_outline_size; }

        // atlas
        const auto& GetAtlas() const                                    { return m_atlas; }
        void SetAtlas(const std::shared_ptr<RHI_Texture>& atlas)        { m_atlas = atlas; }
        const auto& GetAtlasOutline() const                             { return m_atlas_outline; }
        void SetAtlasOutline(const std::shared_ptr<RHI_Texture>& atlas) { m_atlas_outline = atlas; }

        // misc
        void UpdateVertexAndIndexBuffers();
        uint32_t GetIndexCount();

        // properties
        void SetSize(uint32_t size);
        RHI_IndexBuffer* GetIndexBuffer()                     const { return m_index_buffer.get(); }
        RHI_VertexBuffer* GetVertexBuffer()                   const { return m_vertex_buffer.get(); }
        uint32_t GetSize()                                    const { return m_font_size; }
        Font_Hinting_Type GetHinting()                        const { return m_hinting; }
        auto GetForceAutohint()                               const { return m_force_autohint; }
        void SetGlyph(const uint32_t char_code, const Glyph& glyph) { m_glyphs[char_code] = glyph; }

    private:
        uint32_t m_font_size          = 14;
        uint32_t m_outline_size       = 2;
        bool m_force_autohint         = false;
        Font_Hinting_Type m_hinting   = Font_Hinting_Normal;
        Font_Outline_Type m_outline   = Font_Outline_Positive;
        Color m_color                 = Color(1.0f, 1.0f, 1.0f, 1.0f);
        Color m_color_outline         = Color(0.0f, 0.0f, 0.0f, 1.0f);
        uint32_t m_char_max_width;
        uint32_t m_char_max_height;
        std::unordered_map<uint32_t, Glyph> m_glyphs;
        std::vector<TextData> m_text_data;
        std::shared_ptr<RHI_VertexBuffer> m_vertex_buffer;
        std::shared_ptr<RHI_IndexBuffer> m_index_buffer;
        std::shared_ptr<RHI_Texture> m_atlas;
        std::shared_ptr<RHI_Texture> m_atlas_outline;
    };
}
