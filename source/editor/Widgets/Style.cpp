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
#include "Style.h"
#include "../ImGui/ImGui_Style.h"
#include "../Logging/Log.h"
#include "FileSystem/FileSystem.h"
#include <Window.h>
#include <fstream>
//================================

//= NAMESPACES ==============
using namespace std;
using namespace spartan;
using namespace math;
using namespace ImGui::Style;
//===========================

Style::Style(Editor* editor) : Widget(editor)
{
    m_title        = "Style";
    m_size_initial = Vector2(424, 600);
    m_flags       |= ImGuiWindowFlags_NoScrollbar;
    m_padding      = Vector2(8.0f);
    m_visible      = false;

    ImGui::Style::SetupImGuiBase();

    if (spartan::FileSystem::Exists("imgui_style_user.bin"))
    {
        LoadStyleColors("imgui_style_user.bin");
    }
    else
    {
        ImGui::Style::StyleSpartan();
        ImGui::Style::SetupImGuiColors();
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

    // write imgui colors followed by custom palette
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

void Style::LoadStyleColors(const char* path)
{
    ifstream file(path, ios::binary);
    if (!file)
    {
        SP_LOG_ERROR("failed to open style file for reading: %s", path);
        return;
    }

    // validate file size
    const size_t expected_size = ImGuiCol_COUNT * sizeof(ImVec4) + 10 * sizeof(ImVec4);
    file.seekg(0, ios::end);
    const streamsize file_size = file.tellg();
    file.seekg(0, ios::beg);

    if (static_cast<size_t>(file_size) != expected_size)
    {
        SP_LOG_ERROR("style file size mismatch: %s", path);
        return;
    }

    // read imgui colors followed by custom palette
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
    }
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
            case 3: ImGui::StyleColorsClassic(); break;
            case 4: ImGui::StyleColorsDark();    break;
            case 5: ImGui::StyleColorsLight();   break;
        }
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
        ImGui::EndChild();
    }

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
