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
#include "Core/Engine.h"
#include "Rendering/Model.h"
#include "ImGui_Extension.h"
#include "ImGui/Implementation/ImGui_RHI.h"
#include "ImGui/Implementation/imgui_impl_win32.h"
#include "Widgets/Widget_Assets.h"
#include "Widgets/Widget_Console.h"
#include "Widgets/Widget_MenuBar.h"
#include "Widgets/Widget_ProgressDialog.h"
#include "Widgets/Widget_Properties.h"
#include "Widgets/Widget_Toolbar.h"
#include "Widgets/Widget_Viewport.h"
#include "Widgets/Widget_World.h"
//================================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================
 
#define DOCKING_ENABLED ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable

namespace _Editor
{
	Widget* widget_menu_bar		= nullptr;
	Widget* widget_toolbar		= nullptr;
	Widget* widget_world		= nullptr;
	const char* dockspace_name	= "EditorDockspace";
}

Editor::~Editor()
{
	m_widgets.clear();
	m_widgets.shrink_to_fit();

	// ImGui implementation - shutdown
	ImGui::RHI::Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Editor::OnWindowMessage(WindowData& window_data)
{
    // During window creation, Windows fire off a couple of messages,
    // m_initializing is to prevent that spamming.
    if (!m_engine && !m_initializing)
    {
        m_initializing = true;

        // Create engine
        m_engine = make_unique<Engine>(window_data);

        // Acquire useful engine subsystems
        m_context       = m_engine->GetContext();
        m_renderer      = m_context->GetSubsystem<Renderer>().get();
        m_rhi_device    = m_renderer->GetRhiDevice();
        
        if (m_renderer->IsInitialized())
        {
            // ImGui version validation
            IMGUI_CHECKVERSION();
            m_context->GetSubsystem<Settings>()->RegisterThirdPartyLib("Dear ImGui", IMGUI_VERSION, "https://github.com/ocornut/imgui");

            // ImGui context creation
            ImGui::CreateContext();

            // ImGui configuration
            auto& io = ImGui::GetIO();
            io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
            io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
            io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
            io.ConfigWindowsResizeFromEdges = true;
            io.ConfigViewportsNoTaskBarIcon = true;
            ApplyStyle();

            // ImGui backend setup
            ImGui_ImplWin32_Init(window_data.handle);
            ImGui::RHI::Initialize(m_context, static_cast<float>(window_data.width), static_cast<float>(window_data.height));

            // Initialization of misc custom systems
            IconProvider::Get().Initialize(m_context);
            EditorHelper::Get().Initialize(m_context);

            // Create all ImGui widgets
            Widgets_Create();
        }
        else
        {
            LOG_ERROR("The engine failed to initialize the renderer subsystem, aborting editor creation.");
        }

        m_initializing = false;
    }
    else if (!m_initializing)
    {
        ImGui_ImplWin32_WndProcHandler(
            static_cast<HWND>(window_data.handle),
            static_cast<uint32_t>(window_data.message),
            static_cast<int64_t>(window_data.wparam),
            static_cast<uint64_t>(window_data.lparam)
        );

        if (m_engine->GetWindowData().width != window_data.width || m_engine->GetWindowData().height != window_data.height)
        {
            ImGui::RHI::OnResize(window_data.width, window_data.height);
        }

        m_engine->SetWindowData(window_data);
    }
}

void Editor::OnTick()
{	
	if (!m_engine)
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
    m_widgets.emplace_back(make_unique<Widget_Console>(m_context));
    m_widgets.emplace_back(make_unique<Widget_MenuBar>(m_context)); _Editor::widget_menu_bar = m_widgets.back().get();
    m_widgets.emplace_back(make_unique<Widget_Toolbar>(m_context)); _Editor::widget_toolbar = m_widgets.back().get();
    m_widgets.emplace_back(make_unique<Widget_Viewport>(m_context));	
	m_widgets.emplace_back(make_unique<Widget_Assets>(m_context));	
	m_widgets.emplace_back(make_unique<Widget_Properties>(m_context));
	m_widgets.emplace_back(make_unique<Widget_World>(m_context)); _Editor::widget_world = m_widgets.back().get();
    m_widgets.emplace_back(make_unique<Widget_ProgressDialog>(m_context));
}

void Editor::Widgets_Tick()
{
	if (DOCKING_ENABLED) { DockSpace_Begin(); }

	for (auto& widget : m_widgets)
	{
		widget->Begin();
		widget->Tick();
		widget->End();
	}

	if (DOCKING_ENABLED) { DockSpace_End(); }
}

void Editor::DockSpace_Begin()
{
	auto open = true;

	// Flags
	const auto window_flags =
		ImGuiWindowFlags_MenuBar |
		ImGuiWindowFlags_NoDocking |
		ImGuiWindowFlags_NoTitleBar |
		ImGuiWindowFlags_NoCollapse |
		ImGuiWindowFlags_NoResize |
		ImGuiWindowFlags_NoMove |
		ImGuiWindowFlags_NoBringToFrontOnFocus |
		ImGuiWindowFlags_NoNavFocus;

	// Size, Pos
	float offset_y = 0;
    offset_y += _Editor::widget_menu_bar ? _Editor::widget_menu_bar->GetHeight() : 0;
    offset_y += _Editor::widget_toolbar ? _Editor::widget_toolbar->GetHeight() : 0;
	const auto viewport = ImGui::GetMainViewport();
	ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + offset_y));
	ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - offset_y));
	ImGui::SetNextWindowViewport(viewport->ID);

	// Style
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
	ImGui::SetNextWindowBgAlpha(0.0f);
	ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
	ImGui::Begin(_Editor::dockspace_name, &open, window_flags);
	ImGui::PopStyleVar();
	ImGui::PopStyleVar(2);

	// Dock space
	const auto dockspace_id = ImGui::GetID(_Editor::dockspace_name);
	if (!ImGui::DockBuilderGetNode(dockspace_id))
	{
		// Reset current docking state
		ImGui::DockBuilderRemoveNode(dockspace_id);
		ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_None);
        ImGui::DockBuilderSetNodeSize(dockspace_id, ImGui::GetMainViewport()->Size);

		// DockBuilderSplitNode(ImGuiID node_id, ImGuiDir split_dir, float size_ratio_for_node_at_dir, ImGuiID* out_id_dir, ImGuiID* out_id_other);
        ImGuiID dock_main_id		= dockspace_id;
        ImGuiID dock_right_id		= ImGui::DockBuilderSplitNode(dock_main_id,		ImGuiDir_Right, 0.2f,   nullptr, &dock_main_id);
        ImGuiID dock_right_down_id	= ImGui::DockBuilderSplitNode(dock_right_id,	ImGuiDir_Down,	0.6f,   nullptr, &dock_right_id);
        ImGuiID dock_down_id		= ImGui::DockBuilderSplitNode(dock_main_id,		ImGuiDir_Down,	0.25f,  nullptr, &dock_main_id);
        ImGuiID dock_down_right_id	= ImGui::DockBuilderSplitNode(dock_down_id,		ImGuiDir_Right, 0.6f,   nullptr, &dock_down_id);

		// Dock windows	
		ImGui::DockBuilderDockWindow("World",		dock_right_id);
		ImGui::DockBuilderDockWindow("Properties",	dock_right_down_id);
		ImGui::DockBuilderDockWindow("Console",		dock_down_id);
		ImGui::DockBuilderDockWindow("Assets",		dock_down_right_id);
		ImGui::DockBuilderDockWindow("Viewport",	dock_main_id);
		ImGui::DockBuilderFinish(dock_main_id);
	}

	ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
}

void Editor::DockSpace_End()
{
	ImGui::End();
}

void Editor::ApplyStyle() const
{
	// Color settings
	const auto text						= ImVec4(0.76f, 0.77f, 0.8f, 1.0f);
	const auto white					= ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
	const auto black					= ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
	const auto background_very_dark		= ImVec4(0.08f, 0.086f, 0.094f, 1.00f);
	const auto background_dark			= ImVec4(0.117f, 0.121f, 0.145f, 1.00f);
	const auto background_medium		= ImVec4(0.26f, 0.26f, 0.27f, 1.0f);
	const auto background_light			= ImVec4(0.37f, 0.38f, 0.39f, 1.0f);
	const auto highlight_blue			= ImVec4(0.172f, 0.239f, 0.341f, 1.0f);	
	const auto highlight_blue_hovered	= ImVec4(0.29f, 0.42f, 0.65f, 1.0f); 
	const auto highlight_blue_active	= ImVec4(0.382f, 0.449f, 0.561f, 1.0f);
	const auto bar_background			= ImVec4(0.078f, 0.082f, 0.09f, 1.0f);
	const auto bar						= ImVec4(0.164f, 0.180f, 0.231f, 1.0f);
	const auto bar_hovered				= ImVec4(0.411f, 0.411f, 0.411f, 1.0f);
	const auto bar_active				= ImVec4(0.337f, 0.337f, 0.368f, 1.0f);

    // Spatial settings
    const auto font_size    = 24.0f;
    const auto font_scale   = 0.7f;
    const auto roundness    = 2.0f;

    // Use default black style as a base
    ImGui::StyleColorsDark();

    ImGuiStyle& style   = ImGui::GetStyle();
    auto& io            = ImGui::GetIO();

	// Spatial
	style.WindowBorderSize	        = 1.0f;
	style.FrameBorderSize	        = 0.0f;
    style.ScrollbarSize             = 20.0f;
	style.FramePadding		        = ImVec2(5, 5);
	style.ItemSpacing		        = ImVec2(6, 5);
    style.WindowMenuButtonPosition  = ImGuiDir_Right;
	style.WindowRounding	        = roundness;
	style.FrameRounding		        = roundness;
	style.PopupRounding		        = roundness;
	style.GrabRounding		        = roundness;
    style.ScrollbarRounding         = roundness;
    style.Alpha                     = 1.0f;	

	// Colors
	style.Colors[ImGuiCol_Text]						= text;
	style.Colors[ImGuiCol_WindowBg]					= background_dark;
	style.Colors[ImGuiCol_Border]					= black;
	style.Colors[ImGuiCol_FrameBg]					= bar;
	style.Colors[ImGuiCol_FrameBgHovered]			= highlight_blue;
	style.Colors[ImGuiCol_FrameBgActive]			= highlight_blue_hovered;
	style.Colors[ImGuiCol_TitleBg]					= background_very_dark;
	style.Colors[ImGuiCol_TitleBgActive]			= bar;
	style.Colors[ImGuiCol_MenuBarBg]				= background_very_dark;
	style.Colors[ImGuiCol_ScrollbarBg]				= bar_background;
	style.Colors[ImGuiCol_ScrollbarGrab]			= bar;
	style.Colors[ImGuiCol_ScrollbarGrabHovered]		= bar_hovered;
	style.Colors[ImGuiCol_ScrollbarGrabActive]		= bar_active;
	style.Colors[ImGuiCol_CheckMark]				= highlight_blue_hovered;
	style.Colors[ImGuiCol_SliderGrab]				= highlight_blue_hovered;
	style.Colors[ImGuiCol_SliderGrabActive]			= highlight_blue_active;
	style.Colors[ImGuiCol_Button]					= bar_active;
	style.Colors[ImGuiCol_ButtonHovered]			= highlight_blue;
	style.Colors[ImGuiCol_ButtonActive]				= highlight_blue_active;
	style.Colors[ImGuiCol_Header]					= highlight_blue; // selected items (tree, menu bar etc.)
	style.Colors[ImGuiCol_HeaderHovered]			= highlight_blue_hovered; // hovered items (tree, menu bar etc.)
	style.Colors[ImGuiCol_HeaderActive]				= highlight_blue_active;
	style.Colors[ImGuiCol_Separator]				= background_light;
	style.Colors[ImGuiCol_ResizeGrip]				= background_medium;
	style.Colors[ImGuiCol_ResizeGripHovered]		= highlight_blue;
	style.Colors[ImGuiCol_ResizeGripActive]			= highlight_blue_hovered;
	style.Colors[ImGuiCol_PlotLines]				= ImVec4(0.0f, 0.7f, 0.77f, 1.0f);
	style.Colors[ImGuiCol_PlotHistogram]			= highlight_blue; // Also used for progress bar
	style.Colors[ImGuiCol_PlotHistogramHovered]		= highlight_blue_hovered;
	style.Colors[ImGuiCol_TextSelectedBg]			= highlight_blue;
	style.Colors[ImGuiCol_PopupBg]					= background_dark;
	style.Colors[ImGuiCol_DragDropTarget]			= background_light;

    // Font
	string dir_fonts = m_context->GetSubsystem<ResourceCache>()->GetDataDirectory(Asset_Fonts) + "/";
	io.Fonts->AddFontFromFileTTF((dir_fonts + "CalibriBold.ttf").c_str(), font_size);
    io.FontGlobalScale = font_scale;
}
