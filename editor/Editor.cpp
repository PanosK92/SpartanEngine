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

//= INCLUDES ====================================
#include "Editor.h"
#include "Core/Engine.h"
#include "Core/Settings.h"
#include "ImGui/ImGuiExtension.h"
#include "ImGui/Implementation/ImGui_RHI.h"
#include "ImGui/Implementation/imgui_impl_sdl2.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/Console.h"
#include "Widgets/TitleBar.h"
#include "Widgets/ProgressDialog.h"
#include "Widgets/Properties.h"
#include "Widgets/Viewport.h"
#include "Widgets/WorldViewer.h"
#include "Widgets/ShaderEditor.h"
#include "Widgets/ResourceViewer.h"
#include "Widgets/Profiler.h"
#include "Widgets/RenderOptions.h"
//===============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    float k_font_size         = 18.0f;
    float k_font_scale        = 1.0f;
    TitleBar* widget_menu_bar = nullptr;
    Widget* widget_world      = nullptr;

    void process_event(Spartan::sp_variant data)
    {
        SDL_Event* event_sdl = static_cast<SDL_Event*>(get<void*>(data));
        ImGui_ImplSDL2_ProcessEvent(event_sdl);
    }

    void apply_colors()
    {
        // use default dark style as a base
        ImGui::StyleColorsDark();
        ImVec4* colors = ImGui::GetStyle().Colors;

        // color
        const ImVec4 k_palette_color_0 = { 10.0f / 255.0f, 12.0f / 255.0f, 17.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_1 = { 18.0f / 255.0f, 20.0f / 255.0f, 25.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_2 = { 22.0f / 255.0f, 30.0f / 255.0f, 45.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_3 = { 35.0f / 255.0f, 48.0f / 255.0f, 76.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_4 = { 65.0f / 255.0f, 90.0f / 255.0f, 119.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_5 = { 119.0f / 255.0f, 141.0f / 255.0f, 169.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_6 = { 224.0f / 255.0f, 225.0f / 255.0f, 221.0f / 255.0f, 1.0f };

        colors[ImGuiCol_Text]                  = k_palette_color_6;
        colors[ImGuiCol_TextDisabled]          = k_palette_color_6;
        colors[ImGuiCol_WindowBg]              = k_palette_color_1;
        colors[ImGuiCol_ChildBg]               = k_palette_color_1;
        colors[ImGuiCol_PopupBg]               = k_palette_color_1;
        colors[ImGuiCol_Border]                = k_palette_color_3;
        colors[ImGuiCol_BorderShadow]          = k_palette_color_0;
        colors[ImGuiCol_FrameBg]               = k_palette_color_2; // Background of checkbox, radio button, plot, slider, text input
        colors[ImGuiCol_FrameBgHovered]        = k_palette_color_3;
        colors[ImGuiCol_FrameBgActive]         = k_palette_color_4;
        colors[ImGuiCol_TitleBg]               = k_palette_color_1;
        colors[ImGuiCol_TitleBgActive]         = k_palette_color_1;
        colors[ImGuiCol_TitleBgCollapsed]      = k_palette_color_1;
        colors[ImGuiCol_MenuBarBg]             = k_palette_color_0;
        colors[ImGuiCol_ScrollbarBg]           = k_palette_color_0;
        colors[ImGuiCol_ScrollbarGrab]         = k_palette_color_3;
        colors[ImGuiCol_ScrollbarGrabHovered]  = k_palette_color_4;
        colors[ImGuiCol_ScrollbarGrabActive]   = k_palette_color_2;
        colors[ImGuiCol_CheckMark]             = k_palette_color_6;
        colors[ImGuiCol_SliderGrab]            = k_palette_color_4;
        colors[ImGuiCol_SliderGrabActive]      = k_palette_color_3;
        colors[ImGuiCol_Button]                = k_palette_color_3;
        colors[ImGuiCol_ButtonHovered]         = k_palette_color_4;
        colors[ImGuiCol_ButtonActive]          = k_palette_color_2;
        colors[ImGuiCol_Header]                = k_palette_color_0; // Header colors are used for CollapsingHeader, TreeNode, Selectable, MenuItem
        colors[ImGuiCol_HeaderHovered]         = k_palette_color_3;
        colors[ImGuiCol_HeaderActive]          = k_palette_color_0;
        colors[ImGuiCol_Separator]             = k_palette_color_5;
        colors[ImGuiCol_SeparatorHovered]      = k_palette_color_6;
        colors[ImGuiCol_SeparatorActive]       = k_palette_color_6;
        colors[ImGuiCol_ResizeGrip]            = k_palette_color_4;
        colors[ImGuiCol_ResizeGripHovered]     = k_palette_color_5;
        colors[ImGuiCol_ResizeGripActive]      = k_palette_color_3;
        colors[ImGuiCol_Tab]                   = k_palette_color_2;
        colors[ImGuiCol_TabHovered]            = k_palette_color_3;
        colors[ImGuiCol_TabActive]             = k_palette_color_1;
        colors[ImGuiCol_TabUnfocused]          = k_palette_color_2;
        colors[ImGuiCol_TabUnfocusedActive]    = k_palette_color_2; // Might be called active, but it's active only because it's it's the only tab available, the user didn't really activate it
        colors[ImGuiCol_DockingPreview]        = k_palette_color_4; // Preview overlay color when about to docking something
        colors[ImGuiCol_DockingEmptyBg]        = k_palette_color_6; // Background color for empty node (e.g. CentralNode with no window docked into it)
        colors[ImGuiCol_PlotLines]             = k_palette_color_5;
        colors[ImGuiCol_PlotLinesHovered]      = k_palette_color_6;
        colors[ImGuiCol_PlotHistogram]         = k_palette_color_5;
        colors[ImGuiCol_PlotHistogramHovered]  = k_palette_color_6;
        colors[ImGuiCol_TextSelectedBg]        = k_palette_color_4;
        colors[ImGuiCol_DragDropTarget]        = k_palette_color_4; // Color when hovering over target
        colors[ImGuiCol_NavHighlight]          = k_palette_color_3; // Gamepad/keyboard: current highlighted item
        colors[ImGuiCol_NavWindowingHighlight] = k_palette_color_2; // Highlight window when using CTRL+TAB
        colors[ImGuiCol_NavWindowingDimBg]     = k_palette_color_2; // Darken/colorize entire screen behind the CTRL+TAB window list, when active
        colors[ImGuiCol_ModalWindowDimBg]      = k_palette_color_2;
    }

    void apply_style()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.FramePadding      = ImVec2(5.0f, 5.0f);
        style.CellPadding       = ImVec2(6.0f, 5.0f);
        style.ItemSpacing       = ImVec2(6.0f, 5.0f);
        style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing     = 25.0f;
        style.ScrollbarSize     = 13.0f;
        style.GrabMinSize       = 10.0f;
        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;
        style.TabBorderSize     = 1.0f;
        style.WindowRounding    = 2.0f;
        style.ChildRounding     = 3.0f;
        style.FrameRounding     = 0.0f;
        style.PopupRounding     = 3.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabRounding      = 3.0f;
        style.LogSliderDeadzone = 4.0f;
        style.TabRounding       = 3.0f;
        style.Alpha             = 1.0f;

        style.ScaleAllSizes(Spartan::Window::GetDpiScale());
    }
}

Editor::Editor(const std::vector<std::string>& args)
{
    Spartan::Engine::Initialize(args);
    ImGui::CreateContext();

    // configure ImGui
    ImGuiIO& io                      = ImGui::GetIO();
    io.ConfigFlags                  |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags                  |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags                  |= ImGuiConfigFlags_ViewportsEnable;
    io.ConfigFlags                  |= ImGuiConfigFlags_NoMouseCursorChange; // cursor visibility is handled by the engine
    io.ConfigWindowsResizeFromEdges  = true;
    io.ConfigViewportsNoTaskBarIcon  = true;
    io.ConfigViewportsNoDecoration   = true; // borderless child windows but with ImGui min, max and close buttons
    io.IniFilename                   = "editor.ini";

    // load font
    ImFontConfig config; // Config for bold font (mainly for use in headers)
    config.GlyphOffset.y = -2.0f;

    const string dir_fonts = Spartan::ResourceCache::GetResourceDirectory(Spartan::ResourceDirectory::Fonts) + "/";
    font_normal            = io.Fonts->AddFontFromFileTTF((dir_fonts + "OpenSans/OpenSans-Medium.ttf").c_str(), k_font_size * Spartan::Window::GetDpiScale());
    font_bold              = io.Fonts->AddFontFromFileTTF((dir_fonts + "OpenSans/OpenSans-Bold.ttf").c_str(), k_font_size * Spartan::Window::GetDpiScale(), &config);
    io.FontGlobalScale     = k_font_scale;

    // initialise imgui backends
    SP_ASSERT_MSG(ImGui_ImplSDL2_Init(), "Failed to initialize ImGui's SDL backend");
    ImGui::RHI::Initialize();

    // apply colors and style
    apply_colors();
    apply_style();

    // initialization of some helper static classes
    IconLoader::Initialize();
    EditorHelper::Initialize(this);

    // create all imgui widgets
    m_widgets.emplace_back(make_shared<ProgressDialog>(this));
    m_widgets.emplace_back(make_shared<Console>(this));
    m_widgets.emplace_back(make_shared<Profiler>(this));
    m_widgets.emplace_back(make_shared<ResourceViewer>(this));
    m_widgets.emplace_back(make_shared<ShaderEditor>(this));
    m_widgets.emplace_back(make_shared<RenderOptions>(this));
    m_widgets.emplace_back(make_shared<TextureViewer>(this));
    m_widgets.emplace_back(make_shared<Viewport>(this));
    m_widgets.emplace_back(make_shared<AssetBrowser>(this));
    m_widgets.emplace_back(make_shared<Properties>(this));
    m_widgets.emplace_back(make_shared<WorldViewer>(this));
    widget_world = m_widgets.back().get();
    m_widgets.emplace_back(make_shared<TitleBar>(this));
    widget_menu_bar = static_cast<TitleBar*>(m_widgets.back().get());

    // allow imgui to get event's from the engine's event processing loop
    SP_SUBSCRIBE_TO_EVENT(Spartan::EventType::Sdl, SP_EVENT_HANDLER_VARIANT_STATIC(process_event));

    // register imgui as a third party library (will show up in the about window)
    Spartan::Settings::RegisterThirdPartyLib("ImGui", IMGUI_VERSION, "https://github.com/ocornut/imgui");
}

Editor::~Editor()
{
    if (ImGui::GetCurrentContext())
    {
        ImGui::RHI::shutdown();
        ImGui_ImplSDL2_Shutdown();
        ImGui::DestroyContext();
    }

    Spartan::Engine::Shutdown();
}

void Editor::Tick()
{
    // main loop
    while (!Spartan::Window::WantsToClose())
    {
        bool render_editor = Spartan::Engine::IsFlagSet(Spartan::EngineMode::Editor);

        // logic
        {
            // imgui
            if (render_editor)
            {
                ImGui_ImplSDL2_NewFrame();
                ImGui::NewFrame();
            }

            // engine
            Spartan::Engine::Tick();

            // editor
            if (render_editor)
            {
                BeginWindow();

                for (shared_ptr<Widget>& widget : m_widgets)
                {
                    widget->Tick();
                }

                ImGui::End();
            }
        }

        // render
        if (render_editor)
        {
            ImGui::Render();

            if (Spartan::Renderer::CanUseCmdList())
            {
                // main window
                ImGui::RHI::render(ImGui::GetDrawData());
                Spartan::Renderer::Present();
            }

            // child windows
            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }
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

    ImGuiStyle& style = ImGui::GetStyle();

    // set window position and size
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float padding_offset    = 2.0f * (style.FramePadding.y - TitleBar::GetPadding().y) - 1.0f;
    const float offset_y          = widget_menu_bar ? widget_menu_bar->GetHeight() + padding_offset : 0;

    ImGui::SetNextWindowPos(ImVec2(viewport->Pos.x, viewport->Pos.y - offset_y));
    ImGui::SetNextWindowSize(ImVec2(viewport->Size.x, viewport->Size.y - offset_y));
    ImGui::SetNextWindowViewport(viewport->ID);

    // set window style
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.0f);

    // begin window
    std::string name = "##main_window";
    bool open = true;
    ImGui::Begin(name.c_str(), &open, window_flags);
    ImGui::PopStyleVar(3);

    // begin dock space
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        // dock space
        const auto window_id = ImGui::GetID(name.c_str());
        if (!ImGui::DockBuilderGetNode(window_id))
        {
            // reset current docking state
            ImGui::DockBuilderRemoveNode(window_id);
            ImGui::DockBuilderAddNode(window_id, ImGuiDockNodeFlags_None);
            ImGui::DockBuilderSetNodeSize(window_id, ImGui::GetMainViewport()->Size);

            // dockBuilderSplitNode(ImGuiID node_id, ImGuiDir split_dir, float size_ratio_for_node_at_dir, ImGuiID* out_id_dir, ImGuiID* out_id_other);
            ImGuiID dock_main_id       = window_id;
            ImGuiID dock_right_id      = ImGui::DockBuilderSplitNode(dock_main_id,  ImGuiDir_Right, 0.17f, nullptr, &dock_main_id);
            ImGuiID dock_right_down_id = ImGui::DockBuilderSplitNode(dock_right_id, ImGuiDir_Down,  0.6f,  nullptr, &dock_right_id);
            ImGuiID dock_down_id       = ImGui::DockBuilderSplitNode(dock_main_id,  ImGuiDir_Down,  0.22f, nullptr, &dock_main_id);
            ImGuiID dock_down_right_id = ImGui::DockBuilderSplitNode(dock_down_id,  ImGuiDir_Right, 0.5f,  nullptr, &dock_down_id);

            // dock windows
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
