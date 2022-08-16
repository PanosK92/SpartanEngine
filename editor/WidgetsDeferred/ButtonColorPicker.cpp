/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "ButtonColorPicker.h"
#include "Rendering/Model.h"
#include "../ImGui/Source/imgui.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

ButtonColorPicker::ButtonColorPicker(const string& window_title)
{
    m_window_title       = window_title;
    m_color_picker_label = "##" + window_title + "1";
}

void ButtonColorPicker::Update()
{
    if (ImGui::ColorButton("##color_button", m_color))
    {
        m_is_visible = true;
    }

    if (m_is_visible)
    {
        ShowColorPicker();
    }
}

void ButtonColorPicker::ShowColorPicker()
{
    ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
    ImGui::Begin(m_window_title.c_str(), &m_is_visible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDocking);
    ImGui::SetWindowFocus();

    ImGuiColorEditFlags       flags = ImGuiColorEditFlags_AlphaBar;
                              flags |= m_show_wheel ? ImGuiColorEditFlags_PickerHueWheel : ImGuiColorEditFlags_PickerHueBar;
    if (!m_show_preview)      flags |= ImGuiColorEditFlags_NoSidePreview;
    if (m_show_rgb)           flags |= ImGuiColorEditFlags_DisplayRGB;
    if (m_show_hsv)           flags |= ImGuiColorEditFlags_DisplayHSV;
    if (m_show_hex)           flags |= ImGuiColorEditFlags_DisplayHex;
    if (m_hdr)                flags |= ImGuiColorEditFlags_HDR;
    if (m_alpha_half_preview) flags |= ImGuiColorEditFlags_AlphaPreviewHalf;
    if (m_alpha_preview)      flags |= ImGuiColorEditFlags_AlphaPreview;
    if (!m_options_menu)      flags |= ImGuiColorEditFlags_NoOptions;

    ImGui::ColorPicker4(m_color_picker_label.c_str(), (float*)&m_color, flags);

    ImGui::Separator();

    // Note: Using hardcoded labels so the settings remain the same for all color pickers

    // WHEEL
    ImGui::Text("Wheel");
    ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerWheel", &m_show_wheel);

    // RGB
    ImGui::SameLine(); ImGui::Text("RGB");
    ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerRGB", &m_show_rgb);

    // HSV
    ImGui::SameLine(); ImGui::Text("HSV");
    ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerHSV", &m_show_hsv);

    // HEX
    ImGui::SameLine(); ImGui::Text("HEX");
    ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerHEX", &m_show_hex);

    ImGui::End();
}
