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

//= INCLUDES =====================
#include "../imgui/source/imgui.h"
//================================

struct ImGuiWindow;
class Editor;

constexpr float k_widget_default_property = -1.0f;

enum class WidgetDock
{
    None,
    Center,
    Right,
    RightDown,
    Down,
    DownRight
};

class Widget
{
public:
    Widget(Editor* editor);
    virtual ~Widget() = default;

    void Tick();

    virtual void OnTick() {};
    virtual void OnTickVisible() {};
    virtual void OnVisible() {};
    virtual void OnInvisible() {};
    virtual void OnPreBegin();

    template<typename T>
    void PushStyleVar(ImGuiStyleVar idx, T val)
    {
        ImGui::PushStyleVar(idx, val);
        m_var_push_count++;
    }

    spartan::math::Vector2 GetCenter() const;
    float GetHeight()                  const { return m_height; }
    ImGuiWindow* GetWindow()           const { return m_window; }
    const char* GetTitle()             const { return m_title; }
    bool& GetVisible()                       { return m_visible; }
    void SetVisible(const bool is_visible)   { m_visible = is_visible; }

    bool ShowInViewMenu()              const { return m_show_in_view_menu; }
    WidgetDock GetDock()               const { return m_dock; }
    int GetToolbarOrder()              const { return m_toolbar_order; }
    int GetToolbarIcon()               const { return m_toolbar_icon; }

protected:
    bool m_is_window                      = true;
    bool m_visible                        = true;
    bool m_show_in_view_menu              = true;
    int m_flags                           = ImGuiWindowFlags_NoCollapse;
    int m_toolbar_order                   = 0;
    int m_toolbar_icon                    = 0; // icontype value, 0 is none
    float m_height                        = 0;
    float m_alpha                         = -1.0f;
    WidgetDock m_dock                     = WidgetDock::None;
    spartan::math::Vector2 m_size_initial = k_widget_default_property;
    spartan::math::Vector2 m_size_min     = k_widget_default_property;
    spartan::math::Vector2 m_size_max     = FLT_MAX;
    spartan::math::Vector2 m_padding      = k_widget_default_property;
    const char* m_title                   = "Title";
    ImGuiWindow* m_window                 = nullptr;
    Editor* m_editor                      = nullptr;

private:
    uint8_t m_var_push_count = 0;
};
