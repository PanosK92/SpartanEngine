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

//= INCLUDES ==================
#include "Component.h"
#include <memory>
#include <string>
//=============================

namespace spartan
{
    class Mesh;

    enum class Text3DAlignment : uint8_t
    {
        Left,
        Center,
        Right
    };

    class Text3D : public Component
    {
    public:
        Text3D(Entity* entity);
        ~Text3D();

        void Initialize() override;
        void Tick() override;
        void Remove() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        static void RegisterForScripting(sol::state_view state);
        sol::reference AsLua(sol::state_view state) override;

        const std::string& GetText() const { return m_text; }
        void SetText(const std::string& text);

        const std::string& GetFontPath() const { return m_font_path; }
        void SetFontPath(const std::string& font_path);

        float GetSize() const { return m_size; }
        void SetSize(float size);

        float GetDepth() const { return m_depth; }
        void SetDepth(float depth);

        float GetWeight() const { return m_weight; }
        void SetWeight(float weight);

        float GetLetterSpacing() const { return m_letter_spacing; }
        void SetLetterSpacing(float spacing);

        float GetLineSpacing() const { return m_line_spacing; }
        void SetLineSpacing(float spacing);

        uint32_t GetResolution() const { return m_resolution; }
        void SetResolution(uint32_t resolution);

        Text3DAlignment GetAlignment() const { return m_alignment; }
        void SetAlignment(Text3DAlignment alignment);

        bool HasMesh() const { return m_mesh != nullptr; }
        bool GenerateMesh();
        void ClearMesh();

    private:
        void SetDirty();

        std::string m_text      = "Text";
        std::string m_font_path;
        float m_size            = 1.0f;
        float m_depth           = 0.1f;
        float m_weight          = 0.0f;
        float m_letter_spacing  = 0.0f;
        float m_line_spacing    = 1.2f;
        uint32_t m_resolution   = 128;
        Text3DAlignment m_alignment = Text3DAlignment::Left;
        bool m_dirty            = true;
        bool m_render_bound     = false;
        bool m_created_render   = false;
        double m_dirty_time     = 0.0;
        uint64_t m_bound_render_id = 0;
        std::shared_ptr<Mesh> m_mesh;
    };
}
