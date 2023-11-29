/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =====================
#include <string>
#include <functional>
#include "../ImGui/Source/imgui.h"
#include "SDL_mouse.h"
//================================

//==================================
struct ImGuiWindow;
class Editor;
namespace Spartan { class Context; }
//==================================

constexpr float k_widget_default_property = -1.0f;

enum class BorderDirection
{
    None,
    Left,
    Right,
    Top,
    Bottom
};

class Widget
{
public:
    Widget(Editor* editor);
    virtual ~Widget() = default;

    void Tick();

    // Called always
    virtual void OnTick() {};

    // Called only when the widget is visible
    virtual void OnTickVisible() {};

    // Called when the window becomes visible
    virtual void OnVisible() {};

    // Called when the window becomes invisible
    virtual void OnHidden() {};

    // Called just before ImGui::Begin()
    virtual void OnPreBegin();


    // Use this to push style variables. They will be automatically popped.
    template<typename T>
    void PushStyleVar(ImGuiStyleVar idx, T val) { ImGui::PushStyleVar(idx, val); m_var_push_count++; }

    // Properties
    Spartan::Math::Vector2 GetCenter() const;
    float GetHeight()                  const { return m_height; }
    ImGuiWindow* GetWindow()           const { return m_window; }
    const auto& GetTitle()             const { return m_title; }
    bool& GetVisible()                       { return m_visible; }
    void SetVisible(bool is_visible)         { m_visible = is_visible; }


protected:
    // Properties
    bool m_is_window                      = true;
    bool m_visible                        = true;
    int m_flags                           = ImGuiWindowFlags_NoCollapse;
    float m_height                        = 0;
    float m_alpha                         = -1.0f;
    Spartan::Math::Vector2 m_size_initial = k_widget_default_property;
    Spartan::Math::Vector2 m_size_min     = k_widget_default_property;
    Spartan::Math::Vector2 m_size_max     = FLT_MAX;
    Spartan::Math::Vector2 m_padding      = k_widget_default_property;
    std::string m_title                   = "Title";

    bool m_change_cursor_on_border        = true;

    // Called when cursor gets near a border and returns border's direction (in order to change cursor)
    BorderDirection HoveredBorderDirection();

    // The ImGui window this widget corresponds to
    ImGuiWindow* m_window = nullptr;

    // Dependencies
    Editor* m_editor = nullptr;

private:
    uint8_t m_var_push_count = 0;

    SDL_Cursor* cursor = nullptr;
};
