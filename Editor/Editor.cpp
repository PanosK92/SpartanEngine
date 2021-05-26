/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ===================================
#include "Editor.h"
#include "Core/Engine.h"
#include "Core/Settings.h"
#include "Core/Window.h"
#include "Core/EventSystem.h"
#include "SDL.h"
#include "Rendering/Model.h"
#include "Profiling/Profiler.h"
#include "ImGui_Extension.h"
#include "ImGui/Implementation/ImGui_RHI.h"
#include "ImGui/Implementation/imgui_impl_sdl.h"
#include "Widgets/Widget_Assets.h"
#include "Widgets/Widget_Console.h"
#include "Widgets/Widget_MenuBar.h"
#include "Widgets/Widget_ProgressDialog.h"
#include "Widgets/Widget_Properties.h"
#include "Widgets/Widget_Viewport.h"
#include "Widgets/Widget_World.h"
#include "Widgets/Widget_ShaderEditor.h"
#include "Widgets/Widget_ResourceCache.h"
#include "Widgets/Widget_Profiler.h"
#include "Widgets/Widget_RenderOptions.h"
//==============================================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

//= EDITOR OPTIONS ===========================================================================================
// Shapes
const float k_roundness = 2.0f;
// Font
const float k_font_size     = 24.0f;
const float k_font_scale    = 0.7f;
// Color
const ImVec4 k_color_text                   = ImVec4(192.0f / 255.0f, 192.0f / 255.0f, 192.0f / 255.0f, 1.0f);
const ImVec4 k_color_text_disabled          = ImVec4(54.0f / 255.0f, 54.0f / 255.0f, 54.0f / 255.0f, 1.0f);
const ImVec4 k_color_dark_very              = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 1.0f);
const ImVec4 k_color_dark                   = ImVec4(21.0f  / 255.0f, 21.0f  / 255.0f, 21.0f  / 255.0f, 1.0f);
const ImVec4 k_color_mid                    = ImVec4(36.0f  / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.0f);
const ImVec4 k_color_light                  = ImVec4(47.0f / 255.0f, 47.0f / 255.0f, 47.0f / 255.0f, 1.0f);
const ImVec4 k_color_shadow                 = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
const ImVec4 k_color_interactive            = ImVec4(56.0f / 255.0f, 56.0f / 255.0f, 56.0f / 255.0f, 1.0f);
const ImVec4 k_color_interactive_hovered    = ImVec4(0.450f, 0.450f, 0.450f, 1.000f);
const ImVec4 k_color_check                  = ImVec4(26.0f / 255.0f, 140.0f / 255.0f, 192.0f / 255.0f, 1.0f);
//============================================================================================================

namespace _editor
{
    Widget_MenuBar* widget_menu_bar = nullptr;
    Widget* widget_world            = nullptr;
    Renderer* renderer              = nullptr;
    RHI_SwapChain* swapchain        = nullptr;
    Profiler* profiler              = nullptr;
    Window* window                  = nullptr;
    shared_ptr<Spartan::RHI_Device> rhi_device;
}

static void ImGui_Initialise(Context* context)
{
    // Version validation
    IMGUI_CHECKVERSION();
    context->GetSubsystem<Settings>()->RegisterThirdPartyLib("Dear ImGui", IMGUI_VERSION, "https://github.com/ocornut/imgui");

    // Context creation
    ImGui::CreateContext();

    // Configuration
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags                  |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags                  |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags                  |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigWindowsResizeFromEdges = true;
    io.ConfigViewportsNoTaskBarIcon = true;

    // Font
    const string dir_fonts = context->GetSubsystem<ResourceCache>()->GetResourceDirectory(ResourceDirectory::Fonts) + "/";
    io.Fonts->AddFontFromFileTTF((dir_fonts + "Calibri.ttf").c_str(), k_font_size);
    io.FontGlobalScale = k_font_scale;

    // Initialise SDL (windows, input) and RHI (rendering)
    ImGui_ImplSDL2_Init(static_cast<SDL_Window*>(_editor::window->GetHandleSDL()));
    ImGui::RHI::Initialize(context);
}

static void ImGui_ProcessEvent(const Variant& event_variant)
{
    SDL_Event* event_sdl = event_variant.Get<SDL_Event*>();
    ImGui_ImplSDL2_ProcessEvent(event_sdl);
}

static void ImGui_Shutdown()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui::RHI::Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }
}

static void ImGui_ApplyColors()
{
    // Use default dark style as a base
    ImGui::StyleColorsDark();
    ImVec4* colors = ImGui::GetStyle().Colors;

    // Colors
    colors[ImGuiCol_Text]                   = k_color_text;
    colors[ImGuiCol_TextDisabled]           = k_color_text_disabled;
    colors[ImGuiCol_WindowBg]               = k_color_dark;             // Background of normal windows
    colors[ImGuiCol_ChildBg]                = k_color_mid;              // Background of child windows
    colors[ImGuiCol_PopupBg]                = k_color_dark;             // Background of popups, menus, tooltips windows
    colors[ImGuiCol_Border]                 = k_color_interactive;
    colors[ImGuiCol_BorderShadow]           = k_color_shadow;
    colors[ImGuiCol_FrameBg]                = k_color_dark_very;     // Background of checkbox, radio button, plot, slider, text input
    colors[ImGuiCol_FrameBgHovered]         = k_color_interactive;
    colors[ImGuiCol_FrameBgActive]          = k_color_dark_very;
    colors[ImGuiCol_TitleBg]                = k_color_mid;
    colors[ImGuiCol_TitleBgActive]          = k_color_interactive;
    colors[ImGuiCol_TitleBgCollapsed]       = k_color_dark;
    colors[ImGuiCol_MenuBarBg]              = k_color_mid;
    colors[ImGuiCol_ScrollbarBg]            = k_color_mid;
    colors[ImGuiCol_ScrollbarGrab]          = k_color_interactive;
    colors[ImGuiCol_ScrollbarGrabHovered]   = k_color_interactive_hovered;
    colors[ImGuiCol_ScrollbarGrabActive]    = k_color_dark_very;
    colors[ImGuiCol_CheckMark]              = k_color_check;
    colors[ImGuiCol_SliderGrab]             = k_color_interactive;
    colors[ImGuiCol_SliderGrabActive]       = k_color_dark_very;
    colors[ImGuiCol_Button]                 = k_color_interactive;
    colors[ImGuiCol_ButtonHovered]          = k_color_interactive_hovered;
    colors[ImGuiCol_ButtonActive]           = k_color_dark_very;
    colors[ImGuiCol_Header]                 = k_color_light;            // Header* colors are used for CollapsingHeader, TreeNode, Selectable, MenuItem
    colors[ImGuiCol_HeaderHovered]          = k_color_interactive_hovered;
    colors[ImGuiCol_HeaderActive]           = k_color_dark_very;
    colors[ImGuiCol_Separator]              = k_color_interactive;
    colors[ImGuiCol_SeparatorHovered]       = k_color_interactive_hovered;
    colors[ImGuiCol_SeparatorActive]        = k_color_dark_very;
    colors[ImGuiCol_ResizeGrip]             = k_color_interactive;
    colors[ImGuiCol_ResizeGripHovered]      = k_color_interactive_hovered;
    colors[ImGuiCol_ResizeGripActive]       = k_color_dark_very;
    colors[ImGuiCol_Tab]                    = k_color_light;
    colors[ImGuiCol_TabHovered]             = k_color_interactive_hovered;
    colors[ImGuiCol_TabActive]              = k_color_dark_very;
    colors[ImGuiCol_TabUnfocused]           = k_color_light;
    colors[ImGuiCol_TabUnfocusedActive]     = k_color_light;        // Might be called active, but it's active only because it's it's the only tab available, the user didn't really activate it
    colors[ImGuiCol_DockingPreview]         = k_color_dark_very;    // Preview overlay color when about to docking something
    colors[ImGuiCol_DockingEmptyBg]         = k_color_interactive;  // Background color for empty node (e.g. CentralNode with no window docked into it)
    colors[ImGuiCol_PlotLines]              = k_color_interactive;
    colors[ImGuiCol_PlotLinesHovered]       = k_color_interactive_hovered;
    colors[ImGuiCol_PlotHistogram]          = k_color_interactive;
    colors[ImGuiCol_PlotHistogramHovered]   = k_color_interactive_hovered;
    colors[ImGuiCol_TextSelectedBg]         = k_color_dark;
    colors[ImGuiCol_DragDropTarget]         = k_color_interactive_hovered;    // Color when hovering over target
    colors[ImGuiCol_NavHighlight]           = k_color_dark;             // Gamepad/keyboard: current highlighted item
    colors[ImGuiCol_NavWindowingHighlight]  = k_color_dark;             // Highlight window when using CTRL+TAB
    colors[ImGuiCol_NavWindowingDimBg]      = k_color_dark;             // Darken/colorize entire screen behind the CTRL+TAB window list, when active
    colors[ImGuiCol_ModalWindowDimBg]       = k_color_dark;             // Darken/colorize entire screen behind a modal window, when one is active
}

static void ImGui_ApplyStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.WindowBorderSize          = 1.0f;
    style.FrameBorderSize           = 1.0f;
    style.ScrollbarSize             = 20.0f;
    style.FramePadding              = ImVec2(5, 5);
    style.ItemSpacing               = ImVec2(6, 5);
    style.WindowMenuButtonPosition  = ImGuiDir_Right;
    style.WindowRounding            = k_roundness;
    style.FrameRounding             = k_roundness;
    style.PopupRounding             = k_roundness;
    style.GrabRounding              = k_roundness;
    style.ScrollbarRounding         = k_roundness;
    style.Alpha                     = 1.0f;
}

Editor::Editor()
{
    // Create engine
    m_engine = make_unique<Engine>();

    // Acquire useful engine subsystems
    m_context           = m_engine->GetContext();
    _editor::profiler   = m_context->GetSubsystem<Profiler>();
    _editor::renderer   = m_context->GetSubsystem<Renderer>();
    _editor::window     = m_context->GetSubsystem<Window>();
    _editor::rhi_device = _editor::renderer->GetRhiDevice();
    _editor::swapchain  = _editor::renderer->GetSwapChain();
    
    // Initialise Editor/ImGui
    if (_editor::renderer->IsInitialised())
    {
        Initialise();
    }
    else
    {
        LOG_ERROR("Editor failed to initialise, renderer subsystem is required but it has also failed to initialise.");
    }

    // Allow ImGui get event's from the engine's event processing loop
    SP_SUBSCRIBE_TO_EVENT(EventType::EventSDL, SP_EVENT_HANDLER_VARIANT_STATIC(ImGui_ProcessEvent));
}

Editor::~Editor()
{
    ImGui_Shutdown();
}

void Editor::Tick()
{
    while (!_editor::window->WantsToClose())
    {
        // Engine - Tick
        m_engine->Tick();

        if (!_editor::renderer || !_editor::renderer->IsInitialised())
            continue;

        if (_editor::window->IsFullScreen())
        {
            _editor::renderer->Pass_CopyToBackbuffer(_editor::swapchain->GetCmdList());
        }
        else
        {
            // ImGui - Begin
            ImGui_ImplSDL2_NewFrame(m_context);
            ImGui::NewFrame();

            // Editor - Begin
            BeginWindow();

            // Editor - Tick
            for (shared_ptr<Widget>& widget : m_widgets)
            {
                widget->Tick();
            }

            // Editor - End
            if (m_editor_begun)
            {
                ImGui::End();
            }

            // ImGui - End/Render
            ImGui::Render();
            ImGui::RHI::Render(ImGui::GetDrawData());
        }

        // Present
        _editor::renderer->Present();

        // ImGui - child windows
        if (!_editor::window->IsFullScreen() && ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}

void Editor::Initialise()
{
    ImGui_Initialise(m_context);
    ImGui_ApplyColors();
    ImGui_ApplyStyle();

    // Initialization of misc custom systems
    IconProvider::Get().Initialize(m_context);
    EditorHelper::Get().Initialize(m_context);

    // Create all ImGui widgets
    m_widgets.emplace_back(make_shared<Widget_Console>(this));
    m_widgets.emplace_back(make_shared<Widget_Profiler>(this));
    m_widgets.emplace_back(make_shared<Widget_ResourceCache>(this));
    m_widgets.emplace_back(make_shared<Widget_ShaderEditor>(this));
    m_widgets.emplace_back(make_shared<Widget_RenderOptions>(this));
    m_widgets.emplace_back(make_shared<Widget_MenuBar>(this)); _editor::widget_menu_bar = static_cast<Widget_MenuBar*>(m_widgets.back().get());
    m_widgets.emplace_back(make_shared<Widget_Viewport>(this));
    m_widgets.emplace_back(make_shared<Widget_Assets>(this));
    m_widgets.emplace_back(make_shared<Widget_Properties>(this));
    m_widgets.emplace_back(make_shared<Widget_World>(this)); _editor::widget_world = m_widgets.back().get();
    m_widgets.emplace_back(make_shared<Widget_ProgressDialog>(this));
}

void Editor::BeginWindow()
{
    // Set window flags
    const auto window_flags =
        ImGuiWindowFlags_MenuBar                |
        ImGuiWindowFlags_NoDocking              |
        ImGuiWindowFlags_NoTitleBar             |
        ImGuiWindowFlags_NoCollapse             |
        ImGuiWindowFlags_NoResize               |
        ImGuiWindowFlags_NoMove                 |
        ImGuiWindowFlags_NoBringToFrontOnFocus  |
        ImGuiWindowFlags_NoNavFocus;

    // Set window position and size
    float offset_y = _editor::widget_menu_bar ? (_editor::widget_menu_bar->GetHeight() + _editor::widget_menu_bar->GetPadding()) : 0;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y + offset_y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - offset_y));
    ImGui::SetNextWindowViewport(viewport->ID);

    // Set window style
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);

    // Begin window
    std::string name = "##main_window";
    bool open = true;
    m_editor_begun = ImGui::Begin(name.c_str(), &open, window_flags);
    ImGui::PopStyleVar(3);

    // Begin dock space
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable && m_editor_begun)
    {
        // Dock space
        const auto window_id = ImGui::GetID(name.c_str());
        if (!ImGui::DockBuilderGetNode(window_id))
        {
            // Reset current docking state
            ImGui::DockBuilderRemoveNode(window_id);
            ImGui::DockBuilderAddNode(window_id, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodeSize(window_id, ImGui::GetMainViewport()->Size);

            // DockBuilderSplitNode(ImGuiID node_id, ImGuiDir split_dir, float size_ratio_for_node_at_dir, ImGuiID* out_id_dir, ImGuiID* out_id_other);
            ImGuiID dock_main_id                = window_id;
            ImGuiID dock_right_id               = ImGui::DockBuilderSplitNode(dock_main_id,     ImGuiDir_Right, 0.2f,   nullptr, &dock_main_id);
            const ImGuiID dock_right_down_id    = ImGui::DockBuilderSplitNode(dock_right_id,    ImGuiDir_Down,  0.6f,   nullptr, &dock_right_id);
            ImGuiID dock_down_id                = ImGui::DockBuilderSplitNode(dock_main_id,     ImGuiDir_Down,  0.25f,  nullptr, &dock_main_id);
            const ImGuiID dock_down_right_id    = ImGui::DockBuilderSplitNode(dock_down_id,     ImGuiDir_Right, 0.6f,   nullptr, &dock_down_id);

            // Dock windows
            ImGui::DockBuilderDockWindow("World",       dock_right_id);
            ImGui::DockBuilderDockWindow("Properties",  dock_right_down_id);
            ImGui::DockBuilderDockWindow("Console",     dock_down_id);
            ImGui::DockBuilderDockWindow("Assets",      dock_down_right_id);
            ImGui::DockBuilderDockWindow("Viewport",    dock_main_id);

            ImGui::DockBuilderFinish(dock_main_id);
        }

        ImGui::DockSpace(window_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
    }
}
