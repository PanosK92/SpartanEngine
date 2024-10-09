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

//= INCLUDES ============================================
#include "Style.h"
#include "../ImGui/Implementation/ImGui_Style.h"
#include "../Logging/Log.h"
#include "Core/FileSystem.h"
#include "Core/FileSystem.h"
#include <Window.h>
#include <fstream>
#include <iostream>
//==================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
using namespace Math;
using namespace ImGui::Style;
//======================

Style::Style(Editor* editor) : Widget(editor)
{
    m_title         = "Style";
    m_size_initial  = Vector2(424, 600);
    m_flags        |= ImGuiWindowFlags_NoScrollbar;
    m_padding       = Vector2(8.0f);
    m_visible       = false;

    ImGuiStyle& style = ImGui::GetStyle();

    ImGui::Style::SetupImGuiBase();

    if(Spartan::FileSystem::Exists("imgui_style_user.bin")){
        LoadStyleColors("imgui_style_user.bin");
    }else{
        ImGui::Style::StyleSpartan();
        // ImGui::Style::StyleDark();
        // ImGui::Style::StyleLight();
        ImGui::Style::SetupImGuiColors();
    }

    style.ScaleAllSizes(Spartan::Window::GetDpiScale());

}

void Style::SaveStyleColors(const char* path)
{
    ImGuiStyle& style = ImGui::GetStyle();

    std::ofstream file(path, std::ios::binary);

    if (!file)
    {
        SP_LOG_ERROR("Failed to open imgui style file %s", path);
        return;
    }

    file.write(reinterpret_cast<const char*>(style.Colors), ImGuiCol_COUNT * sizeof(float) * 4);

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
        SP_LOG_ERROR("Failed to save imgui style to file %s", path);
    }

    file.close();
}

void Style::LoadStyleColors(const char* path)
{
    ImGuiStyle& style = ImGui::GetStyle();

    std::ifstream file(path, std::ios::binary);

    if (!file)
    {
        SP_LOG_ERROR("Failed to open imgui style file %s", path);
        return;
    }

    size_t imgui_color_size = ImGuiCol_COUNT * sizeof(ImVec4);
    size_t custom_color_size = sizeof(ImVec4) * 10; // count of custom color variables

    size_t expected_size =  imgui_color_size + custom_color_size;

    file.seekg(0, std::ios::end);
    std::streamsize file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if(expected_size != file_size)
    {
        file.close();
        SP_LOG_ERROR("Failed to read imgui style file %s file size does not match expected size", path);
        return;
    }

    file.read(reinterpret_cast<char*>(style.Colors), imgui_color_size); // load all imgui color variables

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
        SP_LOG_ERROR("Failed to read imgui style file %s", path);
    }

    file.close();
}

void Style::OnTickVisible()
{
    if (ImGui::Combo("Load Preset", &m_style_preset_id, "Spartan\0Dark\0Light\0ImGui Classic\0ImGui Dark\0ImGui Light\0"))
    {
        switch (m_style_preset_id)
        {
            case 0: ImGui::Style::StyleSpartan(); ImGui::Style::SetupImGuiColors(); break;
            case 1: ImGui::Style::StyleDark();    ImGui::Style::SetupImGuiColors(); break;
            case 2: ImGui::Style::StyleLight();   ImGui::Style::SetupImGuiColors(); break;
            case 3: ImGui::StyleColorsClassic(); break;
            case 4: ImGui::StyleColorsDark(); break;
            case 5: ImGui::StyleColorsLight(); break;
        }
    }

    if (ImGui::BeginChild("StyleColorSelectChild", ImVec2(-FLT_MIN, 0.0f), ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY)){
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {8,8});

        if(ImGui::ColorEdit4("Background 1",&ImGui::Style::bg_color_1.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Background 2",&ImGui::Style::bg_color_2.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Highlight 1",&ImGui::Style::h_color_1.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Highlight 2",&ImGui::Style::h_color_2.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Accent 1",&ImGui::Style::color_accent_1.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Accent 2",&ImGui::Style::color_accent_2.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Ok",&ImGui::Style::color_ok.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Info",&ImGui::Style::color_info.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Warning",&ImGui::Style::color_warning.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }
        if(ImGui::ColorEdit4("Error",&ImGui::Style::color_error.x)){
            m_unsaved_changes = true;
            ImGui::Style::SetupImGuiColors();
        }

        ImGui::PopStyleVar(1);
        ImGui::EndChild();
    }

    if(ImGui::Button("Save as User Theme")){
        m_unsaved_changes = false;
        SaveStyleColors("imgui_style_user.bin");
    }
    if(ImGui::Button("Reset User Theme")){
        ImGui::Style::StyleSpartan();
        ImGui::Style::SetupImGuiColors();
        SaveStyleColors("imgui_style_user.bin");
    }
    ImGui::Text("You can fine tune your theme colors with imgui style editor.");
    ImGui::Text("Only color changes will be saved");
    if(ImGui::Button("ImGui Style Editor")){
        m_show_imgui_style_editor = !m_show_imgui_style_editor;
    }
    if(m_show_imgui_style_editor){
        ImGui::Begin("ImGui Style Editor", nullptr, ImGuiWindowFlags_NoDocking);
        ImGui::ShowStyleEditor();
        ImGui::End();
    }
}
