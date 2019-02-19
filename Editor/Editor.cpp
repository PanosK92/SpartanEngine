/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =====================================
#include "Editor.h"
#include "ImGui/Implementation/ImGui_RHI.h"
#include "ImGui/Implementation/imgui_impl_win32.h"
#include "Rendering/Renderer.h"
#include "RHI/RHI_Device.h"
#include "UI/EditorHelper.h"
#include "UI/IconProvider.h"
#include "UI/Widgets/Widget_Assets.h"
#include "UI/Widgets/Widget_Console.h"
#include "UI/Widgets/Widget_MenuBar.h"
#include "UI/Widgets/Widget_ProgressDialog.h"
#include "UI/Widgets/Widget_Properties.h"
#include "UI/Widgets/Widget_Toolbar.h"
#include "UI/Widgets/Widget_Viewport.h"
#include "UI/Widgets/Widget_World.h"
#include "Core/Timer.h"
//================================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

#define DOCKING_ENABLED ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable
namespace _Editor
{
	Widget* widget_menuBar		= nullptr;
	Widget* widget_toolbar		= nullptr;
	Widget* widget_world		= nullptr;
	const char* dockspaceName	= "EditorDockspace";
}

Editor::Editor(void* windowHandle, void* windowInstance, int windowWidth, int windowHeight)
{
	// Add console widget first so it picks up the engine's initialization output
	m_widgets.emplace_back(make_unique<Widget_Console>(nullptr));

	// Create engine
	Settings::Get().SetHandles(windowHandle, windowHandle, windowInstance);
	m_engine = make_unique<Engine>(make_shared<Context>());
	
	// Acquire useful engine subsystems
	m_context	= m_engine->GetContext();
	m_renderer	= m_context->GetSubsystem<Renderer>().get();
	m_timer		= m_context->GetSubsystem<Timer>().get();
	m_rhiDevice = m_renderer->GetRHIDevice();

	// ImGui version validation
	IMGUI_CHECKVERSION();
	Settings::Get().m_versionImGui = IMGUI_VERSION;

	// ImGui context creation
	ImGui::CreateContext();

	// ImGui configuration
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
	io.ConfigWindowsResizeFromEdges = true;
	io.ConfigViewportsNoTaskBarIcon = true;
	ApplyStyle();

	// ImGui backend setup
	ImGui_ImplWin32_Init(windowHandle);
	ImGui::RHI::Initialize(m_context);

	Resize(windowWidth, windowHeight);

	// Initialization of misc custom systems
	IconProvider::Get().Initialize(m_context);
	EditorHelper::Get().Initialize(m_context);

	// Create all ImGui widgets
	Widgets_Create();

	m_initialized = true;
}

Editor::~Editor()
{
	if (!m_initialized)
		return;

	m_widgets.clear();
	m_widgets.shrink_to_fit();

	// ImGui implementation - shutdown
	ImGui::RHI::Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Editor::Resize(unsigned int width, unsigned int height)
{
	ImGui::RHI::OnResize(width, height);
}

void Editor::Tick()
{	
	if (!m_initialized)
		return;

	// Update engine (will simulate and render)
	m_engine->Tick();

	// ImGui implementation - start frame
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	// Editor update
	Widgets_Tick();

	// ImGui implementation - end frame
	ImGui::Render();
	ImGui::RHI::RenderDrawData(ImGui::GetDrawData());

	// Update and Render additional Platform Windows
	if (DOCKING_ENABLED)
	{
		ImGui::UpdatePlatformWindows();
		ImGui::RenderPlatformWindowsDefault();
	}
}

void Editor::Widgets_Create()
{
	m_widgets.emplace_back(make_unique<Widget_ProgressDialog>(m_context));
	m_widgets.emplace_back(make_unique<Widget_Assets>(m_context));
	m_widgets.emplace_back(make_unique<Widget_Viewport>(m_context));
	m_widgets.emplace_back(make_unique<Widget_Properties>(m_context));

	m_widgets.emplace_back(make_unique<Widget_MenuBar>(m_context));
	_Editor::widget_menuBar = m_widgets.back().get();

	m_widgets.emplace_back(make_unique<Widget_Toolbar>(m_context));
	_Editor::widget_toolbar = m_widgets.back().get();

	m_widgets.emplace_back(make_unique<Widget_World>(m_context));
	_Editor::widget_world = m_widgets.back().get();
}

void Editor::Widgets_Tick()
{
	if (DOCKING_ENABLED) { DockSpace_Begin(); }

	for (auto& widget : m_widgets)
	{
		widget->Begin();
		widget->Tick(m_timer->GetDeltaTimeSec());
		widget->End();
	}

	if (DOCKING_ENABLED) { DockSpace_End(); }
}

void Editor::DockSpace_Begin()
{
	bool open = true;

	// Flags
	ImGuiWindowFlags window_flags =
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus;

	// Size, Pos
	float offsetY = _Editor::widget_menuBar->GetHeight() + _Editor::widget_toolbar->GetHeight();
	ImGuiViewport* viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + offsetY));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - offsetY));
	ImGui::SetNextWindowViewport(viewport->ID);

	// Style
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(_Editor::dockspaceName, &open, window_flags);
	ImGui::PopStyleVar();
	ImGui::PopStyleVar(2);

	// Dock space
	ImGuiID dockspace_id = ImGui::GetID(_Editor::dockspaceName);
	if (!ImGui::DockBuilderGetNode(dockspace_id))
	{
		// Reset/clear current docking state
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);

		// DockBuilderSplitNode(ImGuiID node_id, ImGuiDir split_dir, float size_ratio_for_node_at_dir, ImGuiID* out_id_dir, ImGuiID* out_id_other);
		ImGuiID dock_main_id		= dockspace_id;
		ImGuiID dock_right_id		= ImGui::DockBuilderSplitNode(dock_main_id,		ImGuiDir_Right, 0.2f, nullptr, &dock_main_id);
		ImGuiID dock_rightDown_id	= ImGui::DockBuilderSplitNode(dock_right_id,	ImGuiDir_Down,	0.6f, nullptr, &dock_right_id);
		ImGuiID dock_down_id		= ImGui::DockBuilderSplitNode(dock_main_id,		ImGuiDir_Down,	0.3f, nullptr, &dock_main_id);
		ImGuiID dock_downRight_id	= ImGui::DockBuilderSplitNode(dock_down_id,		ImGuiDir_Right, 0.6f, nullptr, &dock_down_id);

		// Dock windows	
		ImGui::DockBuilderDockWindow("World",		dock_right_id);
		ImGui::DockBuilderDockWindow("Properties",	dock_rightDown_id);
		ImGui::DockBuilderDockWindow("Console",		dock_down_id);
		ImGui::DockBuilderDockWindow("Assets",		dock_downRight_id);
		ImGui::DockBuilderDockWindow("Viewport",	dock_main_id);
		ImGui::DockBuilderFinish(dock_main_id);
	}
	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruDockspace);
}

void Editor::DockSpace_End()
{
	ImGui::End();
}

void Editor::ApplyStyle()
{
	ImGui::StyleColorsDark();
	ImGuiStyle& style = ImGui::GetStyle();

	float fontSize				= 15.0f;
	float roundness				= 2.0f;
	
	ImVec4 text					= ImVec4(0.76f, 0.77f, 0.8f, 1.0f);
	ImVec4 white				= ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	ImVec4 black				= ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	ImVec4 backgroundVeryDark	= ImVec4(0.08f, 0.086f, 0.094f, 1.00f);
	ImVec4 backgroundDark		= ImVec4(0.117f, 0.121f, 0.145f, 1.00f);
	ImVec4 backgroundMedium		= ImVec4(0.26f, 0.26f, 0.27f, 1.0f);
	ImVec4 backgroundLight		= ImVec4(0.37f, 0.38f, 0.39f, 1.0f);
	ImVec4 highlightBlue		= ImVec4(0.172f, 0.239f, 0.341f, 1.0f);	
	ImVec4 highlightBlueHovered	= ImVec4(0.202f, 0.269f, 0.391f, 1.0f); 
	ImVec4 highlightBlueActive	= ImVec4(0.382f, 0.449f, 0.561f, 1.0f);
	ImVec4 barBackground		= ImVec4(0.078f, 0.082f, 0.09f, 1.0f);
	ImVec4 bar					= ImVec4(0.164f, 0.180f, 0.231f, 1.0f);
	ImVec4 barHovered			= ImVec4(0.411f, 0.411f, 0.411f, 1.0f);
	ImVec4 barActive			= ImVec4(0.337f, 0.337f, 0.368f, 1.0f);

	// Spatial
	style.WindowBorderSize		= 1.0f;
	style.FrameBorderSize		= 0.0f;
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
	style.ScrollbarSize			= 20.0f;
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
	style.Colors[ImGuiCol_CheckMark]				= highlightBlueHovered;
	style.Colors[ImGuiCol_SliderGrab]				= highlightBlueHovered;
	style.Colors[ImGuiCol_SliderGrabActive]			= highlightBlueActive;
	style.Colors[ImGuiCol_Button]					= barActive;
	style.Colors[ImGuiCol_ButtonHovered]			= highlightBlue;
	style.Colors[ImGuiCol_ButtonActive]				= highlightBlueActive;
	style.Colors[ImGuiCol_Header]					= highlightBlue; // selected items (tree, menu bar etc.)
	style.Colors[ImGuiCol_HeaderHovered]			= highlightBlueHovered; // hovered items (tree, menu bar etc.)
	style.Colors[ImGuiCol_HeaderActive]				= highlightBlueActive;
	style.Colors[ImGuiCol_Separator]				= backgroundLight;
	//style.Colors[ImGuiCol_SeparatorHovered]		= ImVec4(0.92f, 0.18f, 0.29f, 0.78f);
	//style.Colors[ImGuiCol_SeparatorActive]		= ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style.Colors[ImGuiCol_ResizeGrip]				= backgroundMedium;
	style.Colors[ImGuiCol_ResizeGripHovered]		= highlightBlue;
	style.Colors[ImGuiCol_ResizeGripActive]			= highlightBlueHovered;
	style.Colors[ImGuiCol_PlotLines]				= ImVec4(0.0f, 0.7f, 0.77f, 1.0f);
	//style.Colors[ImGuiCol_PlotLinesHovered]		= ImVec4(0.92f, 0.18f, 0.29f, 1.00f);
	style.Colors[ImGuiCol_PlotHistogram]			= highlightBlue; // Also used for progress bar
	style.Colors[ImGuiCol_PlotHistogramHovered]		= highlightBlueHovered;
	style.Colors[ImGuiCol_TextSelectedBg]			= highlightBlue;
	style.Colors[ImGuiCol_PopupBg]					= backgroundDark;
	style.Colors[ImGuiCol_DragDropTarget]			= backgroundLight;
	//style.Colors[ImGuiCol_ModalWindowDarkening]	= ImVec4(0.20f, 0.22f, 0.27f, 0.73f);

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->AddFontFromFileTTF("Standard Assets\\Fonts\\CalibriBold.ttf", fontSize);
}