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

//= INCLUDES ============================
#include "Editor.h"
#include <memory>
#include "Core/Context.h"
#include "Core/Settings.h"
#include "UI/ImGui_Implementation.h"
#include "UI/Widgets/Widget.h"
#include "UI/Widgets/Widget_MenuBar.h"
#include "UI/Widgets/Widget_Properties.h"
#include "UI/Widgets/Widget_Console.h"
#include "UI/Widgets/Widget_Scene.h"
#include "UI/Widgets/Widget_Assets.h"
#include "UI/Widgets/Widget_Viewport.h"
#include "UI/Widgets/Widget_Toolbar.h"
#include "UI/ThumbnailProvider.h"
#include "UI/EditorHelper.h"
#include "Graphics/RI/Backend_Def.h"
#include "Graphics/RI/Backend_Imp.h"
//=======================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

Editor::Editor()
{
	m_context = nullptr;
	m_graphics = nullptr;
	m_widgets.emplace_back(make_unique<Widget_MenuBar>());
	m_widgets.emplace_back(make_unique<Widget_Toolbar>());
	m_widgets.emplace_back(make_unique<Widget_Properties>());
	m_widgets.emplace_back(make_unique<Widget_Console>());
	m_widgets.emplace_back(make_unique<Widget_Scene>());
	m_widgets.emplace_back(make_unique<Widget_Assets>());
	m_widgets.emplace_back(make_unique<Widget_Viewport>());
}

Editor::~Editor()
{
	Shutdown();
}

void Editor::Initialize(Context* context)
{
	m_context = context;
	m_graphics = context->GetSubsystem<Graphics>();

	ThumbnailProvider::Get().Initialize(context);
	EditorHelper::Get().Initialize(context);
	Settings::Get().g_versionImGui = IMGUI_VERSION;
	ImGui_ImplDX11_Init(context);

	ApplyStyle();

	for (auto& widget : m_widgets)
	{
		widget->Initialize(context);
	}
}

void Editor::Resize()
{
	ImGui_ImplDX11_InvalidateDeviceObjects();
	ImGui_ImplDX11_CreateDeviceObjects();
}

void Editor::Update()
{	
	// [ImGui] Start new frame
	ImGui_ImplDX11_NewFrame();

	// Do editor stuff
	DrawEditor();
	EditorHelper::Get().Update();

	// [ImGui] End frame
	ImGui::Render();
	m_graphics->EventBegin("Pass_ImGui");
	ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
	m_graphics->EventEnd();
}

void Editor::Shutdown()
{
	m_widgets.clear();
	m_widgets.shrink_to_fit();

	ImGui_ImplDX11_Shutdown();
}

void Editor::DrawEditor()
{
	for (auto& widget : m_widgets)
	{
		if (widget->GetIsWindow())
		{
			widget->Begin();
		}

		widget->Update();

		if (widget->GetIsWindow())
		{
			widget->End();
		}
	}
}

void Editor::ApplyStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();

	float fontSize				= 15.0f;
	float roundness				= 2.0f;
	ImVec4 white				= ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 text					= ImVec4(0.76f, 0.77f, 0.8f, 1.0f);
	ImVec4 black				= ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	ImVec4 backgroundVeryDark	= ImVec4(0.08f, 0.086f, 0.094f, 1.00f);
	ImVec4 backgroundDark		= ImVec4(0.117f, 0.121f, 0.145f, 1.00f);
	ImVec4 backgroundMedium		= ImVec4(0.26f, 0.26f, 0.27f, 1.0f);
	ImVec4 backgroundLight		= ImVec4(0.37f, 0.38f, 0.39f, 1.0f);
	ImVec4 highlightBlue		= ImVec4(0.172f, 0.239f, 0.341f, 1.0f);	
	ImVec4 highlightBlueActive	= ImVec4(0.182f, 0.249f, 0.361f, 1.0f);
	ImVec4 highlightBlueHovered	= ImVec4(0.202f, 0.269f, 0.391f, 1.0f); 
	ImVec4 barBackground		= ImVec4(0.078f, 0.082f, 0.09f, 1.0f);
	ImVec4 bar					= ImVec4(0.164f, 0.180f, 0.231f, 1.0f);
	ImVec4 barHovered			= ImVec4(0.411f, 0.411f, 0.411f, 1.0f);
	ImVec4 barActive			= ImVec4(0.337f, 0.337f, 0.368f, 1.0f);

	// Spatial
	style.WindowBorderSize		= 1.0f;
	style.FrameBorderSize		= 1.0f;
	//style.WindowMinSize		= ImVec2(160, 20);
	style.FramePadding			= ImVec2(5, 5);
	style.ItemSpacing			= ImVec2(6, 5);
	//style.ItemInnerSpacing	= ImVec2(6, 4);
	style.Alpha					= 1.0f;
	style.WindowRounding		= roundness;
	style.FrameRounding			= roundness;
	style.PopupRounding			= roundness;
	//style.IndentSpacing		= 6.0f;
	//style.ItemInnerSpacing	= ImVec2(2, 4);
	//style.ColumnsMinSpacing	= 50.0f;
	//style.GrabMinSize			= 14.0f;
	style.GrabRounding			= roundness;
	//style.ScrollbarSize		= 12.0f;
	style.ScrollbarRounding		= roundness;	

	// Colors
	style.Colors[ImGuiCol_Text]						= text;
	//style.Colors[ImGuiCol_TextDisabled]			= ImVec4(0.86f, 0.93f, 0.89f, 0.28f);
	style.Colors[ImGuiCol_WindowBg]					= backgroundDark;
	//style.Colors[ImGuiCol_ChildBg]				= ImVec4(0.20f, 0.22f, 0.27f, 0.58f);
	style.Colors[ImGuiCol_Border]					= black;
	//style.Colors[ImGuiCol_BorderShadow]			= ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
	style.Colors[ImGuiCol_FrameBg]					= bar;
	style.Colors[ImGuiCol_FrameBgHovered]			= highlightBlue;
	style.Colors[ImGuiCol_FrameBgActive]			= highlightBlueHovered;
	style.Colors[ImGuiCol_TitleBg]					= backgroundVeryDark;
	//style.Colors[ImGuiCol_TitleBgCollapsed]		= ImVec4(0.20f, 0.22f, 0.27f, 0.75f);
	style.Colors[ImGuiCol_TitleBgActive]			= bar;
	style.Colors[ImGuiCol_MenuBarBg]				= backgroundVeryDark;
	style.Colors[ImGuiCol_ScrollbarBg]				= barBackground;
	style.Colors[ImGuiCol_ScrollbarGrab]			= bar;
	style.Colors[ImGuiCol_ScrollbarGrabHovered]		= barHovered;
	style.Colors[ImGuiCol_ScrollbarGrabActive]		= barActive;
	style.Colors[ImGuiCol_CheckMark]				= white;
	style.Colors[ImGuiCol_SliderGrab]				= bar;
	style.Colors[ImGuiCol_SliderGrabActive]			= barActive;
	style.Colors[ImGuiCol_Button]					= barActive;
	style.Colors[ImGuiCol_ButtonHovered]			= highlightBlue;
	style.Colors[ImGuiCol_ButtonActive]				= highlightBlueHovered;
	style.Colors[ImGuiCol_Header]					= highlightBlue; // selected items (tree, menu bar etc.)
	style.Colors[ImGuiCol_HeaderHovered]			= highlightBlueHovered; // hovered items (tree, menu bar etc.)
	style.Colors[ImGuiCol_HeaderActive]				= highlightBlueActive;
	style.Colors[ImGuiCol_Separator]				= backgroundLight;
	//style.Colors[ImGuiCol_SeparatorHovered]		= ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
	//style.Colors[ImGuiCol_SeparatorActive]		= ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style.Colors[ImGuiCol_ResizeGrip]				= backgroundMedium;
	style.Colors[ImGuiCol_ResizeGripHovered]		= highlightBlue;
	style.Colors[ImGuiCol_ResizeGripActive]			= highlightBlueHovered;
	//style.Colors[ImGuiCol_PlotLines]				= ImVec4(0.86f, 0.93f, 0.89f, 0.63f);
	//style.Colors[ImGuiCol_PlotLinesHovered]		= ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogram]			= highlightBlue; // Also used for progress bar
	style.Colors[ImGuiCol_PlotHistogramHovered]		= highlightBlueHovered;
	style.Colors[ImGuiCol_TextSelectedBg]			= highlightBlue;
	style.Colors[ImGuiCol_PopupBg]					= backgroundVeryDark;
	style.Colors[ImGuiCol_DragDropTarget]			= backgroundLight;
	//style.Colors[ImGuiCol_ModalWindowDarkening]	= ImVec4(0.20f, 0.22f, 0.27f, 0.73f);

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("Standard Assets\\Editor\\CalibriBold.ttf", fontSize);
}