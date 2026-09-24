/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "ProgressDialog.h"
#include "Viewport.h"
#include "../Editor.h"
#include "../imgui/source/imgui_internal.h"
#include <algorithm>
#include <cmath>

using namespace spartan;
using namespace spartan::math;

namespace
{
    // Keep asset paths and imported names inside the card without treating them
    // as format strings. The full value is still available in the tracker.
    void clipped_text(const std::string& text, float width)
    {
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const float height = ImGui::GetTextLineHeight();
        ImGui::RenderTextEllipsis(ImGui::GetWindowDrawList(), pos,
            ImVec2(pos.x + width, pos.y + height), pos.x + width,
            text.c_str(), text.c_str() + text.size(), nullptr);
        ImGui::Dummy(ImVec2(width, height));
    }
}

ProgressDialog::ProgressDialog(Editor* editor) : Widget(editor)
{
    m_visible = false;
    m_show_in_view_menu = false;
    m_size_initial = Vector2(460.0f, 0.0f);
    m_flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_AlwaysAutoResize |
        ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoInputs;
}

void ProgressDialog::OnPreBegin()
{
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    float bottom = ImGui::GetMainViewport()->WorkPos.y + ImGui::GetMainViewport()->WorkSize.y;
    float available_width = ImGui::GetMainViewport()->WorkSize.x;
    if (Viewport* viewport = m_editor->GetWidget<Viewport>())
    {
        if (ImGuiWindow* window = viewport->GetWindow())
        {
            center = ImVec2(window->Pos.x + window->Size.x * 0.5f, window->Pos.y + window->Size.y * 0.5f);
            bottom = window->Pos.y + window->Size.y;
            available_width = window->Size.x;
        }
    }
    ImGui::SetNextWindowPos(ImVec2(center.x, bottom - 28.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(std::max(220.0f, std::min(460.0f, available_width - 40.0f)), 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.97f);
    PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f, 16.0f));
    PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
}

void ProgressDialog::OnTick()
{
    m_display = ProgressTracker::GetDisplay();
    if (m_display.count == 0)
    {
        m_visible_since = 0.0;
        SetVisible(false);
        return;
    }
    if (m_visible_since == 0.0) m_visible_since = ImGui::GetTime();
    // Tiny texture loads should not flash a card over the editor.
    SetVisible(ImGui::GetTime() - m_visible_since >= 0.15);
}

void ProgressDialog::OnTickVisible()
{
    // Stay above the viewport without taking keyboard focus away from the editor.
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    ImGui::TextDisabled("IN PROGRESS");
    if (m_display.active_count > 2)
    {
        char count[48];
        snprintf(count, sizeof(count), "+%u active", m_display.active_count - 2);
        ImGui::SameLine(ImGui::GetWindowWidth() - ImGui::GetStyle().WindowPadding.x - ImGui::CalcTextSize(count).x);
        ImGui::TextDisabled("%s", count);
    }
    ImGui::Dummy(ImVec2(0.0f, 5.0f));

    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_CheckMark);
    for (uint32_t i = 0; i < m_display.count; ++i)
    {
        const ProgressSnapshot& task = m_display.tasks[i];
        if (i != 0)
        {
            ImGui::Dummy(ImVec2(0.0f, 7.0f));
            ImGui::Separator();
            ImGui::Dummy(ImVec2(0.0f, 7.0f));
        }
        const float width = ImGui::GetContentRegionAvail().x;
        char elapsed[32];
        const auto seconds = static_cast<uint32_t>(task.elapsed_seconds);
        if (seconds >= 60) snprintf(elapsed, sizeof(elapsed), "%um %02us", seconds / 60, seconds % 60);
        else snprintf(elapsed, sizeof(elapsed), "%us", seconds);
        const float elapsed_width = ImGui::CalcTextSize(elapsed).x;
        const ImVec2 title_pos = ImGui::GetCursorScreenPos();
        clipped_text(task.title, width - elapsed_width - 18.0f);
        ImGui::GetWindowDrawList()->AddText(ImVec2(title_pos.x + width - elapsed_width, title_pos.y),
            ImGui::GetColorU32(ImGuiCol_TextDisabled), elapsed);

        const bool determinate = task.fraction >= 0.0f;
        const std::string step = task.step.empty() ? "Working" : task.step;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        clipped_text(step, width - 48.0f);
        ImGui::PopStyleColor();
        if (determinate)
        {
            char percent[16];
            snprintf(percent, sizeof(percent), "%.0f%%", task.fraction * 100.0f);
            ImGui::GetWindowDrawList()->AddText(ImVec2(title_pos.x + width - ImGui::CalcTextSize(percent).x,
                title_pos.y + ImGui::GetTextLineHeightWithSpacing()), ImGui::GetColorU32(ImGuiCol_TextDisabled), percent);
        }
        if (!task.detail.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            clipped_text(task.detail, width);
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::Dummy(ImVec2(width, ImGui::GetTextLineHeight()));
        }

        ImGui::Dummy(ImVec2(0.0f, 3.0f));
        const ImVec2 start = ImGui::GetCursorScreenPos();
        const ImVec2 end(start.x + width, start.y + 3.0f);
        ImDrawList* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(start, end, ImGui::GetColorU32(ImGuiCol_FrameBg), 2.0f);
        auto& bar = m_bars[i];
        if (bar.id != task.id || bar.step_id != task.step_id)
            bar = {task.id, task.step_id, 0.0f};
        if (determinate)
        {
            bar.fraction += (task.fraction - bar.fraction) * (1.0f - expf(-12.0f * ImGui::GetIO().DeltaTime));
            if (bar.fraction > 0.001f)
                draw->AddRectFilled(start, ImVec2(start.x + width * bar.fraction, end.y), ImGui::GetColorU32(accent), 2.0f);
        }
        else
        {
            // A bounded sweep communicates activity without inventing a percentage.
            const float t = static_cast<float>(fmod(ImGui::GetTime() * 0.55 + i * 0.2, 1.0));
            const float left = std::max(0.0f, t * 1.3f - 0.3f);
            const float right = std::min(1.0f, t * 1.3f);
            draw->AddRectFilled(ImVec2(start.x + width * left, start.y), ImVec2(start.x + width * right, end.y),
                ImGui::GetColorU32(accent), 2.0f);
        }
        ImGui::Dummy(ImVec2(width, 3.0f));
    }
}
