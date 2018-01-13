/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES =================
#include "ButtonColorPicker.h"
#include "../imgui/imgui.h"
#include "../EditorHelper.h"
//============================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

static string g_buttonLabel;
static string g_colorPickerLabel;
static bool showWheel = false;
static bool showPreview = true;

//= COLOR PICKER SETTINGS ==============
static bool hdr					= false;
static bool alpha_preview		= true;
static bool alpha_half_preview	= false;
static bool options_menu		= true;
static bool showRGB				= true;
static bool showHSV				= false;
static bool showHex				= true;
//======================================

ButtonColorPicker::ButtonColorPicker(const string& windowTitle)
{
	m_windowTitle = windowTitle;
	g_buttonLabel = "##" + windowTitle + "1";
	g_colorPickerLabel = "##" + windowTitle + "1";

	m_isVisible = false;
	m_color = Vector4(0, 0, 0, 1);
}

void ButtonColorPicker::Update()
{
	if (ImGui::ColorButton(g_buttonLabel.c_str(), ToImVec4(m_color)))
	{
		m_isVisible = true;
	}

	if (m_isVisible)
	{
		ShowColorPicker();
	}
}

void ButtonColorPicker::ShowColorPicker()
{
	ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
	ImGui::Begin(m_windowTitle.c_str(), &m_isVisible, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_ResizeFromAnySide);
	ImGui::SetWindowFocus();

	int misc_flags = (hdr ? ImGuiColorEditFlags_HDR : 0) | (alpha_half_preview ? ImGuiColorEditFlags_AlphaPreviewHalf : (alpha_preview ? ImGuiColorEditFlags_AlphaPreview : 0)) | (options_menu ? 0 : ImGuiColorEditFlags_NoOptions);
	ImGuiColorEditFlags flags = misc_flags;
						flags |= ImGuiColorEditFlags_AlphaBar;
	if (!showPreview)	flags |= ImGuiColorEditFlags_NoSidePreview;
						flags |= ImGuiColorEditFlags_PickerHueBar;
	if (showWheel)		flags |= ImGuiColorEditFlags_PickerHueWheel;
	if (showRGB)		flags |= ImGuiColorEditFlags_RGB;
	if (showHSV)		flags |= ImGuiColorEditFlags_HSV;
	if (showHex)		flags |= ImGuiColorEditFlags_HEX;

	ImGui::ColorPicker4(g_colorPickerLabel.c_str(), (float*)&m_color, flags);

	ImGui::Separator();

	// Note: Using hardcoded labels so the settings remaing the same for all color pickers

	// WHEEL
	ImGui::Text("Wheel");
	ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerWheel", &showWheel);

	// RGB
	ImGui::SameLine(); ImGui::Text("RGB");
	ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerRGB", &showRGB);

	// HSV
	ImGui::SameLine(); ImGui::Text("HSV");
	ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerHSV", &showHSV);

	// HEX
	ImGui::SameLine(); ImGui::Text("HEX");
	ImGui::SameLine(); ImGui::Checkbox("##ButtonColorPickerHEX", &showHex);

	ImGui::End();
}
