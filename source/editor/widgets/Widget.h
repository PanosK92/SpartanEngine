/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
