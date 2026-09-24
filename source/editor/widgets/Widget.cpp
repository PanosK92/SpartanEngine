/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ==============================
#include "pch.h"
#include "Widget.h"
#include "Viewport.h"
#include "../Editor.h"
#include "../imgui/source/imgui_internal.h"
#include "profiling/Profiler.h"
#include "display/Display.h"
//=========================================

//= NAMESPACES =========
using namespace std;
using namespace spartan;
using namespace math;
//======================

Widget::Widget(Editor* editor)
{
    m_editor = editor;
    m_window = nullptr;
}

void Widget::Tick()
{
    OnTick();

    if (!m_is_window || !m_visible)
    {
        return;
    }

    bool draw_contents = false;
    bool window_appearing = false;

    SP_PROFILE_CPU_START(m_title);

    m_size_initial = m_size_initial == k_widget_default_property
        ? Vector2(Display::GetWidth() * 0.5f, Display::GetHeight() * 0.5f)
        : m_size_initial;
    ImGui::SetNextWindowSize(m_size_initial, ImGuiCond_FirstUseEver);

    if (m_size_min != k_widget_default_property || m_size_max != FLT_MAX)
    {
        ImGui::SetNextWindowSizeConstraints(m_size_min, m_size_max);
    }

    if (m_padding != k_widget_default_property)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_padding);
        m_var_push_count++;
    }

    if (m_alpha != k_widget_default_property)
    {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_alpha);
        m_var_push_count++;
    }

    OnPreBegin();

    draw_contents = ImGui::Begin(m_title, &m_visible, m_flags);
    ImGuiWindow* current_window = ImGui::GetCurrentWindow();
    window_appearing = current_window && current_window->Appearing;
    if (draw_contents)
    {
        m_window = current_window;
        m_height = ImGui::GetWindowHeight();
    }
    else
    {
        m_window = nullptr;
    }

    if (!m_visible)
    {
        OnInvisible();
    }
    else if (window_appearing)
    {
        OnVisible();
    }

    if (draw_contents)
    {
        OnTickVisible();
    }

    ImGui::End();
    ImGui::PopStyleVar(m_var_push_count);
    m_var_push_count = 0;
    SP_PROFILE_CPU_END();
}

void Widget::OnPreBegin()
{
    if (Viewport* viewport = m_editor->GetWidget<Viewport>())
    {
        if (ImGuiWindow* window = viewport->GetWindow())
        {
            ImVec2 pos    = window->Pos;
            ImVec2 sze    = window->Size;
            ImVec2 center = ImVec2(pos.x + sze.x * 0.5f, pos.y + sze.y * 0.5f);
            ImVec2 pivot  = ImVec2(0.5f, 0.5f);

            ImGui::SetNextWindowPos(center, ImGuiCond_FirstUseEver, pivot);
        }
    }
}

spartan::math::Vector2 Widget::GetCenter() const
{
    if (!m_window || m_window->Size.x < 1.0f || m_window->Size.y < 1.0f)
    {
        return ImGui::GetMainViewport()->GetCenter();
    }

    ImVec2 pos    = m_window->Pos;
    ImVec2 sze    = m_window->Size;
    ImVec2 center = ImVec2(pos.x + sze.x * 0.5f, pos.y + sze.y * 0.5f);

    return center;
}
