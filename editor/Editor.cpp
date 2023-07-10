/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES ====================================
#include "Editor.h"
#include "Core/Event.h"
#include "Core/Engine.h"
#include "Core/Settings.h"
#include "Core/Window.h"
#include "ImGui/ImGuiExtension.h"
#include "ImGui/Implementation/ImGui_RHI.h"
#include "ImGui/Implementation/imgui_impl_sdl2.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/Console.h"
#include "Widgets/MenuBar.h"
#include "Widgets/ProgressDialog.h"
#include "Widgets/Properties.h"
#include "Widgets/Viewport.h"
#include "Widgets/WorldViewer.h"
#include "Widgets/ShaderEditor.h"
#include "Widgets/ResourceViewer.h"
#include "Widgets/Profiler.h"
#include "Widgets/RenderOptions.h"
#include "Widgets/TextureViewer.h"
//===============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    // Shapes
    static const float k_roundness = 2.0f;

    // Font
    static const float k_font_size  = 16.0f;
    static const float k_font_scale = 1.0f;

    // Color
    static const ImVec4 k_color_text                = ImVec4(192.0f / 255.0f, 192.0f / 255.0f, 192.0f / 255.0f, 1.0f);
    static const ImVec4 k_color_text_disabled       = ImVec4(54.0f / 255.0f, 54.0f / 255.0f, 54.0f / 255.0f, 1.0f);
    static const ImVec4 k_color_dark_very           = ImVec4(15.0f / 255.0f, 15.0f / 255.0f, 15.0f / 255.0f, 1.0f);
    static const ImVec4 k_color_dark                = ImVec4(21.0f  / 255.0f, 21.0f  / 255.0f, 21.0f  / 255.0f, 1.0f);
    static const ImVec4 k_color_mid                 = ImVec4(36.0f  / 255.0f, 36.0f / 255.0f, 36.0f / 255.0f, 1.0f);
    static const ImVec4 k_color_light               = ImVec4(47.0f / 255.0f, 47.0f / 255.0f, 47.0f / 255.0f, 1.0f);
    static const ImVec4 k_color_shadow              = ImVec4(0.0f, 0.0f, 0.0f, 0.5f);
    static const ImVec4 k_color_interactive         = ImVec4(56.0f / 255.0f, 56.0f / 255.0f, 56.0f / 255.0f, 1.0f);
    static const ImVec4 k_color_interactive_hovered = ImVec4(0.450f, 0.450f, 0.450f, 1.000f);
    static const ImVec4 k_color_check               = ImVec4(26.0f / 255.0f, 140.0f / 255.0f, 192.0f / 255.0f, 1.0f);
    
    MenuBar* widget_menu_bar = nullptr;
    Widget* widget_world     = nullptr;

    static void process_event(Spartan::sp_variant data)
    {
        SDL_Event* event_sdl = static_cast<SDL_Event*>(get<void*>(data));
        ImGui_ImplSDL2_ProcessEvent(event_sdl);
    }
    
    static void apply_colors()
{
    // Use default dark style as a base
    ImGui::StyleColorsDark();
    ImVec4* colors = ImGui::GetStyle().Colors;

    // Colors
    colors[ImGuiCol_Text]                  = k_color_text;
    colors[ImGuiCol_TextDisabled]          = k_color_text_disabled;
    colors[ImGuiCol_WindowBg]              = k_color_mid;                   // Background of normal windows
    colors[ImGuiCol_ChildBg]               = k_color_mid;                   // Background of child windows
    colors[ImGuiCol_PopupBg]               = k_color_dark;                  // Background of popups, menus, tooltips windows
    colors[ImGuiCol_Border]                = k_color_interactive;
    colors[ImGuiCol_BorderShadow]          = k_color_shadow;
    colors[ImGuiCol_FrameBg]               = k_color_dark_very;             // Background of checkbox, radio button, plot, slider, text input
    colors[ImGuiCol_FrameBgHovered]        = k_color_interactive;
    colors[ImGuiCol_FrameBgActive]         = k_color_dark_very;
    colors[ImGuiCol_TitleBg]               = k_color_dark;
    colors[ImGuiCol_TitleBgActive]         = k_color_dark;
    colors[ImGuiCol_TitleBgCollapsed]      = k_color_light;
    colors[ImGuiCol_MenuBarBg]             = k_color_dark;
    colors[ImGuiCol_ScrollbarBg]           = k_color_mid;
    colors[ImGuiCol_ScrollbarGrab]         = k_color_interactive;
    colors[ImGuiCol_ScrollbarGrabHovered]  = k_color_interactive_hovered;
    colors[ImGuiCol_ScrollbarGrabActive]   = k_color_dark_very;
    colors[ImGuiCol_CheckMark]             = k_color_check;
    colors[ImGuiCol_SliderGrab]            = k_color_interactive;
    colors[ImGuiCol_SliderGrabActive]      = k_color_dark_very;
    colors[ImGuiCol_Button]                = k_color_interactive;
    colors[ImGuiCol_ButtonHovered]         = k_color_interactive_hovered;
    colors[ImGuiCol_ButtonActive]          = k_color_dark_very;
    colors[ImGuiCol_Header]                = k_color_light;                 // Header colors are used for CollapsingHeader, TreeNode, Selectable, MenuItem
    colors[ImGuiCol_HeaderHovered]         = k_color_interactive_hovered;
    colors[ImGuiCol_HeaderActive]          = k_color_dark_very;
    colors[ImGuiCol_Separator]             = k_color_dark_very;
    colors[ImGuiCol_SeparatorHovered]      = k_color_light;
    colors[ImGuiCol_SeparatorActive]       = k_color_light;
    colors[ImGuiCol_ResizeGrip]            = k_color_interactive;
    colors[ImGuiCol_ResizeGripHovered]     = k_color_interactive_hovered;
    colors[ImGuiCol_ResizeGripActive]      = k_color_dark_very;
    colors[ImGuiCol_Tab]                   = k_color_light;
    colors[ImGuiCol_TabHovered]            = k_color_interactive_hovered;
    colors[ImGuiCol_TabActive]             = k_color_dark_very;
    colors[ImGuiCol_TabUnfocused]          = k_color_light;
    colors[ImGuiCol_TabUnfocusedActive]    = k_color_light;                 // Might be called active, but it's active only because it's it's the only tab available, the user didn't really activate it
    colors[ImGuiCol_DockingPreview]        = k_color_dark_very;             // Preview overlay color when about to docking something
    colors[ImGuiCol_DockingEmptyBg]        = k_color_interactive;           // Background color for empty node (e.g. CentralNode with no window docked into it)
    colors[ImGuiCol_PlotLines]             = k_color_interactive;
    colors[ImGuiCol_PlotLinesHovered]      = k_color_interactive_hovered;
    colors[ImGuiCol_PlotHistogram]         = k_color_interactive;
    colors[ImGuiCol_PlotHistogramHovered]  = k_color_interactive_hovered;
    colors[ImGuiCol_TextSelectedBg]        = k_color_dark;
    colors[ImGuiCol_DragDropTarget]        = k_color_interactive_hovered;   // Color when hovering over target
    colors[ImGuiCol_NavHighlight]          = k_color_dark;                  // Gamepad/keyboard: current highlighted item
    colors[ImGuiCol_NavWindowingHighlight] = k_color_dark;                  // Highlight window when using CTRL+TAB
    colors[ImGuiCol_NavWindowingDimBg]     = k_color_dark;                  // Darken/colorize entire screen behind the CTRL+TAB window list, when active
    colors[ImGuiCol_ModalWindowDimBg]      = k_color_dark;                  // Darken/colorize entire screen behind a modal window, when one is active
}
    
    static void apply_style()
    {
        ImGuiStyle& style = ImGui::GetStyle();
    
        style.WindowBorderSize         = 1.0f;
        style.FrameBorderSize          = 1.0f;
        style.ScrollbarSize            = 20.0f;
        style.FramePadding             = ImVec2(5, 5);
        style.ItemSpacing              = ImVec2(6, 5);
        style.WindowMenuButtonPosition = ImGuiDir_Right;
        style.WindowRounding           = k_roundness;
        style.FrameRounding            = k_roundness;
        style.PopupRounding            = k_roundness;
        style.GrabRounding             = k_roundness;
        style.ScrollbarRounding        = k_roundness;
        style.Alpha                    = 1.0f;
    
        style.ScaleAllSizes(Spartan::Window::GetDpiScale());
    }
}

Editor::Editor()
{
    Spartan::Engine::Initialize();

    // Initialise Editor/ImGui
    {
        SP_ASSERT_MSG(IMGUI_CHECKVERSION(), "Version mismatch between source and caller");
        Spartan::Settings::RegisterThirdPartyLib("Dear ImGui", IMGUI_VERSION, "https://github.com/ocornut/imgui");

        // Create context
        ImGui::CreateContext();

        // Configuration
        ImGuiIO& io                      = ImGui::GetIO();
        io.ConfigFlags                  |= ImGuiConfigFlags_NavEnableKeyboard;
        io.ConfigFlags                  |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags                  |= ImGuiConfigFlags_ViewportsEnable;
        io.ConfigWindowsResizeFromEdges = true;
        io.ConfigViewportsNoTaskBarIcon = true;
        io.ConfigViewportsNoDecoration  = true; // aka borderless but with ImGui min, max and close buttons
        io.IniFilename                  = "editor.ini";

        // Load font
        string dir_fonts = Spartan::ResourceCache::GetResourceDirectory(Spartan::ResourceDirectory::Fonts) + "/";
        io.Fonts->AddFontFromFileTTF((dir_fonts + "Calibri.ttf").c_str(), k_font_size * Spartan::Window::GetDpiScale());
        io.FontGlobalScale = k_font_scale;

        // Initialise ImGui backends
        SP_ASSERT_MSG(ImGui_ImplSDL2_Init(), "Failed to initialize ImGui's SDL backend");
        ImGui::RHI::Initialize();

        // Apply colors and style
        apply_colors();
        apply_style();

        // Initialization of some helper static classes
        IconLoader::Initialize();
        EditorHelper::Initialize(this);

        // Create all ImGui widgets
        m_widgets.emplace_back(make_shared<Console>(this));
        m_widgets.emplace_back(make_shared<Profiler>(this));
        m_widgets.emplace_back(make_shared<ResourceViewer>(this));
        m_widgets.emplace_back(make_shared<ShaderEditor>(this));
        m_widgets.emplace_back(make_shared<RenderOptions>(this));
        m_widgets.emplace_back(make_shared<TextureViewer>(this));
        m_widgets.emplace_back(make_shared<MenuBar>(this));
        widget_menu_bar = static_cast<MenuBar*>(m_widgets.back().get());
        m_widgets.emplace_back(make_shared<Viewport>(this));
        m_widgets.emplace_back(make_shared<AssetBrowser>(this));
        m_widgets.emplace_back(make_shared<Properties>(this));
        m_widgets.emplace_back(make_shared<WorldViewer>(this));
        widget_world = m_widgets.back().get();
        m_widgets.emplace_back(make_shared<ProgressDialog>(this));
    }

    // Allow ImGui to get event's from the engine's event processing loop
    SP_SUBSCRIBE_TO_EVENT(Spartan::EventType::Sdl, SP_EVENT_HANDLER_VARIANT_STATIC(process_event));
}

Editor::~Editor()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui::RHI::Shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    Spartan::Engine::Shutdown();
}

void Editor::Tick()
{
    // this is the main editor/engine loop
    while (!Spartan::Window::WantsToClose())
    {
        bool render_editor = !Spartan::Window::IsFullScreen();

        // ImGui - Begin
        if (render_editor)
        {
            ImGui_ImplSDL2_NewFrame();
            ImGui::NewFrame();
        }

        // Engine - Tick
        Spartan::Engine::Tick();

        // Editor - Tick/End
        if (render_editor)
        {
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
        if (!Spartan::Window::IsMinimised())
        {
            Spartan::Renderer::Present();
        }

        // ImGui - Child windows
        if (render_editor && ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }
}

void Editor::BeginWindow()
{
    const auto window_flags =
        ImGuiWindowFlags_MenuBar               |
        ImGuiWindowFlags_NoDocking             |
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    // Set window position and size
    float offset_y = widget_menu_bar ? (widget_menu_bar->GetHeight() + widget_menu_bar->GetPadding()) : 0;
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
            ImGuiID dock_main_id       = window_id;
            ImGuiID dock_right_id      = ImGui::DockBuilderSplitNode(dock_main_id,  ImGuiDir_Right, 0.2f,  nullptr, &dock_main_id);
            ImGuiID dock_right_down_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down,  0.6f,  nullptr, &dock_right_id);
            ImGuiID dock_down_id       = ImGui::DockBuilderSplitNode(dock_main_id,  ImGuiDir_Down,  0.25f, nullptr, &dock_main_id);
            ImGuiID dock_down_right_id = ImGui::DockBuilderSplitNode(dock_down_id,  ImGuiDir_Right, 0.6f,  nullptr, &dock_down_id);

            // Dock windows
            ImGui::DockBuilderDockWindow("World",      dock_right_id);
            ImGui::DockBuilderDockWindow("Properties", dock_right_down_id);
            ImGui::DockBuilderDockWindow("Console",    dock_down_id);
            ImGui::DockBuilderDockWindow("Assets",     dock_down_right_id);
            ImGui::DockBuilderDockWindow("Viewport",   dock_main_id);

            ImGui::DockBuilderFinish(dock_main_id);
        }

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::DockSpace(window_id, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);
        ImGui::PopStyleVar();
    }
}
