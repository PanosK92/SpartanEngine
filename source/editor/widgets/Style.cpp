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

//= INCLUDES =====================
#include "pch.h"
#include <fstream>
#include "Style.h"
#include "../imgui/ImGui_Style.h"
#include <Window.h>
//================================

//= NAMESPACES ==============
using namespace std;
using namespace spartan;
using namespace math;
using namespace ImGui::Style;
//===========================

namespace
{
    constexpr uint32_t style_magic   = 0x53505448;
    constexpr uint32_t style_version = 3;
}

Style::Style(Editor* editor) : Widget(editor)
{
    m_title              = "Style";
    m_size_initial       = Vector2(460, 760) * Window::GetDpiScale();
    m_padding            = Vector2(12.0f) * Window::GetDpiScale();
    m_visible            = false;
    m_show_in_view_menu  = false;

    ImGui::Style::SetupImGuiBase();

    const bool loaded =
        spartan::FileSystem::Exists("imgui_style_user.bin") &&
        LoadStyleColors("imgui_style_user.bin");

    if (!loaded)
    {
        ImGui::Style::StyleSpartan();
        ImGui::Style::SetupImGuiColors();
        SaveStyleColors("imgui_style_user.bin");
    }
    else
    {
        // Upgrade the previous factory palette in memory. Keep the saved file intact
        // until the user explicitly saves the new theme, including any fine-tuned colors.
        const ImVec4 legacy[] = {
            {0.082f, 0.090f, 0.102f, 1}, {0.137f, 0.153f, 0.176f, 1},
            {0.945f, 0.953f, 0.961f, 1}, {0.588f, 0.627f, 0.678f, 1},
            {0.208f, 0.725f, 0.914f, 1}, {0.129f, 0.494f, 0.667f, 1},
            {0.353f, 0.769f, 0.514f, 1}, {0.588f, 0.753f, 0.933f, 1},
            {0.941f, 0.678f, 0.306f, 1}, {0.925f, 0.361f, 0.373f, 1}
        };
        const ImVec4 current[] = {bg_color_1, bg_color_2, h_color_1, h_color_2,
            color_accent_1, color_accent_2, color_ok, color_info, color_warning, color_error};
        ImVec4 previous_blue[IM_ARRAYSIZE(legacy)];
        memcpy(previous_blue, legacy, sizeof(legacy));
        previous_blue[0] = {0.067f, 0.075f, 0.090f, 1};
        previous_blue[1] = {0.125f, 0.141f, 0.169f, 1};
        previous_blue[4] = {0.365f, 0.620f, 1.000f, 1};
        previous_blue[5] = {0.153f, 0.365f, 0.745f, 1};
        if (memcmp(legacy, current, sizeof(legacy)) == 0 || memcmp(previous_blue, current, sizeof(previous_blue)) == 0)
        {
            ImGui::Style::StyleSpartan();
            ImGui::Style::SetupImGuiColors();
            m_style_preset_id = 0;
        }
    }

    ImGui::GetStyle().ScaleAllSizes(spartan::Window::GetDpiScale());
}

void Style::SaveStyleColors(const char* path)
{
    ofstream file(path, ios::binary);
    if (!file)
    {
        SP_LOG_ERROR("failed to open style file for writing: %s", path);
        return;
    }

    file.write(
        reinterpret_cast<const char*>(&style_magic),
        sizeof(style_magic)
    );
    file.write(
        reinterpret_cast<const char*>(&style_version),
        sizeof(style_version)
    );

    ImGuiStyle& style = ImGui::GetStyle();
    file.write(reinterpret_cast<const char*>(style.Colors), ImGuiCol_COUNT * sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&bg_color_1), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&bg_color_2), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&h_color_1), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&h_color_2), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&color_accent_1), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&color_accent_2), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&color_ok), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&color_info), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&color_warning), sizeof(ImVec4));
    file.write(reinterpret_cast<const char*>(&color_error), sizeof(ImVec4));

    if (!file)
    {
        SP_LOG_ERROR("failed to write style file: %s", path);
    }
}

bool Style::LoadStyleColors(const char* path)
{
    ifstream file(path, ios::binary);
    if (!file)
    {
        SP_LOG_ERROR("failed to open style file for reading: %s", path);
        return false;
    }

    const size_t expected_size =
        sizeof(style_magic) +
        sizeof(style_version) +
        ImGuiCol_COUNT * sizeof(ImVec4) +
        10 * sizeof(ImVec4);
    file.seekg(0, ios::end);
    const streamsize file_size = file.tellg();
    file.seekg(0, ios::beg);

    if (static_cast<size_t>(file_size) != expected_size)
    {
        SP_LOG_INFO("upgrading editor theme: %s", path);
        return false;
    }

    uint32_t magic   = 0;
    uint32_t version = 0;
    file.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    file.read(reinterpret_cast<char*>(&version), sizeof(version));
    if (magic != style_magic || version != style_version)
    {
        SP_LOG_INFO("upgrading editor theme: %s", path);
        return false;
    }

    ImGuiStyle& style = ImGui::GetStyle();
    file.read(reinterpret_cast<char*>(style.Colors), ImGuiCol_COUNT * sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&bg_color_1), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&bg_color_2), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&h_color_1), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&h_color_2), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&color_accent_1), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&color_accent_2), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&color_ok), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&color_info), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&color_warning), sizeof(ImVec4));
    file.read(reinterpret_cast<char*>(&color_error), sizeof(ImVec4));

    if (!file)
    {
        SP_LOG_ERROR("failed to read style file: %s", path);
        return false;
    }

    ImGui::Style::UpdateSemanticColors();
    return true;
}

void Style::OnTickVisible()
{
    // preset selector
    if (ImGui::Combo("Load Preset", &m_style_preset_id, "Spartan\0Dark\0Light\0ImGui Classic\0ImGui Dark\0ImGui Light\0"))
    {
        ImGui::Style::SetupImGuiBase();

        switch (m_style_preset_id)
        {
            case 0: ImGui::Style::StyleSpartan(); ImGui::Style::SetupImGuiColors(); break;
            case 1: ImGui::Style::StyleDark();    ImGui::Style::SetupImGuiColors(); break;
            case 2: ImGui::Style::StyleLight();   ImGui::Style::SetupImGuiColors(); break;
            case 3:
                ImGui::StyleColorsClassic();
                ImGui::Style::SyncSemanticColorsFromImGui();
                break;
            case 4:
                ImGui::StyleColorsDark();
                ImGui::Style::SyncSemanticColorsFromImGui();
                break;
            case 5:
                ImGui::StyleColorsLight();
                ImGui::Style::SyncSemanticColorsFromImGui();
                break;
        }
        ImGui::GetStyle().ScaleAllSizes(spartan::Window::GetDpiScale());
        m_unsaved_changes = true;
    }

    // color editors
    if (ImGui::BeginChild("StyleColorSelectChild", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY))
    {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8, 8});

        auto color_edit = [this](const char* label, ImVec4& color)
        {
            if (ImGui::ColorEdit4(label, &color.x))
            {
                m_unsaved_changes = true;
                ImGui::Style::SetupImGuiColors();
            }
        };

        color_edit("Background 1", bg_color_1);
        color_edit("Background 2", bg_color_2);
        color_edit("Highlight 1",  h_color_1);
        color_edit("Highlight 2",  h_color_2);
        color_edit("Accent 1",     color_accent_1);
        color_edit("Accent 2",     color_accent_2);
        color_edit("Ok",           color_ok);
        color_edit("Info",         color_info);
        color_edit("Warning",      color_warning);
        color_edit("Error",        color_error);

        ImGui::PopStyleVar();
    }
    ImGui::EndChild();

    // save/reset buttons
    if (ImGui::Button("Save as User Theme"))
    {
        m_unsaved_changes = false;
        SaveStyleColors("imgui_style_user.bin");
    }

    if (ImGui::Button("Reset User Theme"))
    {
        ImGui::Style::StyleSpartan();
        ImGui::Style::SetupImGuiColors();
        ImGui::GetStyle().ScaleAllSizes(spartan::Window::GetDpiScale());
        m_style_preset_id = 0;
        m_unsaved_changes = false;
        SaveStyleColors("imgui_style_user.bin");
    }

    ImGui::Text("Fine tune colors with the imgui style editor below.");
    ImGui::Text("Only color changes will be saved.");

    if (ImGui::Button("ImGui Style Editor"))
    {
        m_show_imgui_style_editor = !m_show_imgui_style_editor;
    }

    if (m_show_imgui_style_editor)
    {
        ImGui::Begin("ImGui Style Editor", nullptr, ImGuiWindowFlags_NoDocking);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }
}
