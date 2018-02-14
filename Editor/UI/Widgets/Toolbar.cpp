/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES =======================
#include "Toolbar.h"
#include "../ImGui/imgui.h"
#include "../ImGui/imgui_internal.h"
#include "../IconProvider.h"
#include "../EditorHelper.h"
//==================================

//= NAMESPACES ==========
using namespace Directus;
//=======================

static float g_buttonSize			= 15.0f;
static ImVec4 g_colorButtonReleased = ImVec4(0, 1, 0, 1);
static ImVec4 g_colorButtonPressed	= ImVec4(0, 0.5f, 0.5f, 1);

Toolbar::Toolbar()
{
	
}

void Toolbar::Initialize(Context* context)
{
	Widget::Initialize(context);
	m_title = "Toolbar";
	m_windowFlags = ImGuiWindowFlags_NoCollapse | 
		ImGuiWindowFlags_NoResize				| 
		ImGuiWindowFlags_NoMove					| 
		ImGuiWindowFlags_NoSavedSettings		| 
		ImGuiWindowFlags_NoScrollbar			|
		ImGuiWindowFlags_NoTitleBar;

	Engine::EngineMode_Disable(Engine_Game);
}

void Toolbar::Begin()
{
	ImGuiContext& g = *GImGui;
	float width		= g.IO.DisplaySize.x;
	float height	= g.FontBaseSize + g.Style.FramePadding.y * 2.0f - 1.0f;
	ImGui::SetNextWindowPos(ImVec2(0.0f, height - 1.0f));
	ImGui::SetNextWindowSize(ImVec2(width, height + 16));
	ImGui::Begin(m_title.c_str(), &m_isVisible, m_windowFlags);
}

void Toolbar::Update()
{
	bool editorMode = !Engine::EngineMode_IsSet(Engine_Game);
	if (ImGui::ImageButton(ICON_PROVIDER_BY_ENUM(Icon_Button_Play), ImVec2(g_buttonSize, g_buttonSize), ImVec2(0, 0), ImVec2(1, 1), -1, ImVec4(0, 0, 0, 0), editorMode ? g_colorButtonReleased : g_colorButtonPressed))
	{
		Engine::EngineMode_Toggle(Engine_Game);
	}

	ImGui::SameLine(); ImGui::Text(editorMode ? "Editor" : "Game");
}
