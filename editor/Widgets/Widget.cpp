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

//= INCLUDES ==============================
#include "Widget.h"
#include "../Editor.h"
#include "../ImGui/Source/imgui_internal.h"
#include "Profiling/Profiler.h"
#include "Display/Display.h"
#include "Viewport.h"
//=========================================

Widget::Widget(Editor* editor)
{
    m_editor = editor;
    m_window = nullptr;
}

void Widget::Tick()
{
    OnTick();

    if (!m_is_window || !m_visible)
        return;

    // Begin
    {
        SP_PROFILE_CPU_START(m_title.c_str());

        // Size initial
        if (m_size_initial != k_widget_default_property)
        {
            ImGui::SetNextWindowSize(m_size_initial, ImGuiCond_FirstUseEver);
        }

        // Size min max
        if (m_size_min != k_widget_default_property || m_size_max != FLT_MAX)
        {
            ImGui::SetNextWindowSizeConstraints(m_size_min, m_size_max);
        }

        // Padding
        if (m_padding != k_widget_default_property)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, m_padding);
            m_var_push_count++;
        }

        // Alpha
        if (m_alpha != k_widget_default_property)
        {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, m_alpha);
            m_var_push_count++;
        }

        // Callback
        OnPreBegin();

        // Begin
        if (ImGui::Begin(m_title.c_str(), &m_visible, m_flags))
        {
            m_window = ImGui::GetCurrentWindow();
            m_height = ImGui::GetWindowHeight();
        }

        // Callbacks
        if (m_window && m_window->Appearing)
        {
            OnVisible();
        }
        else if (!m_visible)
        {
            OnInvisible();
        }
    }

    OnTickVisible();

    // End
    {
        // End
        ImGui::End();

        // Pop style variables
        ImGui::PopStyleVar(m_var_push_count);
        m_var_push_count = 0;

        // End profiling
        SP_PROFILE_CPU_END();
    }
}

void Widget::OnPreBegin()
{
    // Set the position to the viewport's center
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

Spartan::Math::Vector2 Widget::GetCenter() const
{
    ImVec2 pos    = m_window->Pos;
    ImVec2 sze    = m_window->Size;
    ImVec2 center = ImVec2(pos.x + sze.x * 0.5f, pos.y + sze.y * 0.5f);

    return center;
}
