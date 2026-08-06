/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "pch.h"
#include "Editor.h"
#include "GeneralWindows.h"
#include "WorldPreviews.h"
#include "Widgets/MenuBar.h"
#include "Core/Engine.h"
#include "Core/Settings.h"
#include "Core/Timer.h"
#include "Input/Input.h"
#include "ImGui/ImGui_EditorUi.h"
#include "ImGui/ImGui_Extension.h"
#include "ImGui/ImGui_Style.h"
#include "ImGui/Implementation/ImGui_RHI.h"
#include "ImGui/Implementation/imgui_impl_sdl3.h"
#include "Profiling/Profiler.h"
#include "Widgets/AssetBrowser.h"
#include "Widgets/Console.h"
#include "Widgets/Style.h"
#include "Widgets/ProgressDialog.h"
#include "Widgets/Properties.h"
#include "Widgets/Viewport.h"
#include "Widgets/WorldViewer.h"
#include "Widgets/ShaderEditor.h"
#include "Widgets/ResourceViewer.h"
#include "Widgets/Profiler.h"
#include "Widgets/RenderOptions.h"
#include "Widgets/ScriptEditor.h"
#include "Widgets/AssetViewer.h"
#include "Widgets/Sequencer.h"
#include "MCP/EditorMcpCommands.h"
#include "MCP/McpAssistant.h"
#include "World/World.h"
//===============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    float font_size  = 14.0f;
    float font_scale = 1.0f;

    void process_event(spartan::sp_variant data)
    {
        SDL_Event* event_sdl = static_cast<SDL_Event*>(get<void*>(data));
        ImGui_ImplSDL3_ProcessEvent(event_sdl);
    }

    void draw_status_footer()
    {
        const float height = ImGui::EditorUi::scaled(24.0f);
        const float width = ImGui::GetWindowWidth();
        const float y = ImGui::GetWindowHeight() - height;
        const ImVec2 window_pos = ImGui::GetWindowPos();
        const ImVec2 min(
            window_pos.x,
            window_pos.y + y
        );
        const ImVec2 max(
            min.x + width,
            min.y + height
        );
        ImDrawList* draw_list = ImGui::GetWindowDrawList();
        draw_list->AddRectFilled(
            min,
            max,
            ImGui::EditorUi::color(
                ImGui::Style::color_panel
            )
        );
        draw_list->AddLine(
            min,
            ImVec2(max.x, min.y),
            ImGui::EditorUi::color(
                ImGui::Style::color_border
            )
        );

        ImGui::SetCursorPos(ImVec2(
            ImGui::EditorUi::scaled(8.0f),
            y + ImGui::EditorUi::scaled(4.0f)
        ));

        const bool playing = spartan::Engine::IsFlagSet(
            spartan::EngineMode::Playing
        );
        const bool paused = spartan::Engine::IsFlagSet(
            spartan::EngineMode::Paused
        );
        const ImVec4 status_color = playing
            ? paused
                ? ImGui::Style::color_warning
                : ImGui::Style::color_ok
            : ImGui::Style::color_text_muted;
        ImGui::TextColored(
            status_color,
            "%s",
            playing ? paused ? "Paused" : "Playing" : "Ready"
        );
        ImGui::SameLine();

        const std::string& world_name = spartan::World::GetName();
        ImGui::TextDisabled(
            "%s",
            world_name.empty() ? "No world loaded" : world_name.c_str()
        );

        const char* fps_label = "FPS";
        char fps[32] = {};
        snprintf(
            fps,
            sizeof(fps),
            "%s %.0f",
            fps_label,
            ImGui::GetIO().Framerate
        );
        const float fps_width = ImGui::CalcTextSize(fps).x;
        ImGui::SameLine(
            width -
            fps_width -
            ImGui::EditorUi::scaled(10.0f)
        );
        ImGui::TextDisabled("%s", fps);
    }
}

Editor::Editor(const vector<string>& args)
{
    spartan::Engine::Initialize(args);
    ImGui::CreateContext();

    // configure ImGui
    ImGuiIO& io                      = ImGui::GetIO();
    io.ConfigFlags                  |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags                  |= ImGuiConfigFlags_DockingEnable;
    if (spartan::RHI_Context::supports_imgui_multi_viewport)
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
    io.ConfigFlags                  |= ImGuiConfigFlags_NoMouseCursorChange; // cursor control is given to ImGui, but dynamically, from the engine
    io.ConfigWindowsResizeFromEdges  = true;
    io.IniFilename                   = "editor.ini";

    ImFontConfig config;
    config.GlyphOffset.y = -1.0f;

    const string dir_fonts =
        spartan::ResourceCache::GetResourceDirectory(
            spartan::ResourceDirectory::Fonts
        ) + "/";
    const float scaled_font_size =
        font_size *
        spartan::Window::GetDpiScale();
    font_normal = io.Fonts->AddFontFromFileTTF(
        (dir_fonts + "Inter/Inter-Regular.ttf").c_str(),
        scaled_font_size
    );
    font_bold = io.Fonts->AddFontFromFileTTF(
        (dir_fonts + "Inter/Inter-SemiBold.ttf").c_str(),
        scaled_font_size,
        &config
    );
    ImGui::GetStyle().FontScaleMain = font_scale;

    // initialize imgui backends, the rhi-aware sdl platform glue lives behind ImGui::RHI so the editor stays api-agnostic
    SP_ASSERT_MSG(ImGui::RHI::InitializePlatformBackend(spartan::Window::GetHandleSDL()), "Failed to initialize ImGui's SDL backend");
    ImGui::RHI::Initialize();

    // create all imgui widgets
    m_widgets.emplace_back(make_shared<Style>(this));
    m_widgets.emplace_back(make_shared<ProgressDialog>(this));
    m_widgets.emplace_back(make_shared<Console>(this));
    m_widgets.emplace_back(make_shared<Profiler>(this));
    m_widgets.emplace_back(make_shared<ResourceViewer>(this));
    m_widgets.emplace_back(make_shared<ShaderEditor>(this));
    m_widgets.emplace_back(make_shared<ScriptEditor>(this));
    m_widgets.emplace_back(make_shared<McpAssistant>(this));
    m_widgets.emplace_back(make_shared<AssetViewer>(this));
    m_widgets.emplace_back(make_shared<RenderOptions>(this));
    m_widgets.emplace_back(make_shared<TextureViewer>(this));
    m_widgets.emplace_back(make_shared<Viewport>(this));
    m_widgets.emplace_back(make_shared<AssetBrowser>(this));
    m_widgets.emplace_back(make_shared<Properties>(this));
    m_widgets.emplace_back(make_shared<WorldViewer>(this));
    m_widgets.emplace_back(make_shared<Sequencer>(this));
    MenuBar::Initialize(this);

    // the widgets that mcp can drive are all up by now, the commands that reach them are registered
    // from one place so no widget has to know that mcp exists
    editor_mcp::Register(this);

    // allow imgui to get event's from the engine's event processing loop
    SP_SUBSCRIBE_TO_EVENT(spartan::EventType::Sdl, SP_EVENT_HANDLER_VARIANT_STATIC(process_event));

    GeneralWindows::Initialize(this);
}

Editor::~Editor()
{
    // handlers hold the editor, so they go before anything they could reach does
    editor_mcp::Unregister();

    WorldPreviews::Shutdown();

    if (ImGui::GetCurrentContext())
    {
        ImGui::RHI::shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }

    spartan::Engine::Shutdown();
}

void Editor::Tick()
{
    // main loop
    while (!spartan::Window::WantsToClose())
    {
        spartan::Profiler::FrameStart();
        spartan::Profiler::TimeBlockStart(
            "frame_active",
            spartan::TimeBlockType::Cpu
        );

        bool render_editor = spartan::Engine::IsFlagSet(spartan::EngineMode::EditorVisible);
        spartan::Renderer::SetPresentInRenderer(
            !render_editor
        );

        // logic
        {
            // imgui
            if (render_editor)
            {
                ImGui_ImplSDL3_NewFrame();
                ImGui::NewFrame();

                const ImGuiIO& io = ImGui::GetIO();
                spartan::Input::SetBlockedByUi(
                    io.WantTextInput
                );
            }
            else
            {
                spartan::Input::SetBlockedByUi(false);
            }

            // engine
            spartan::Engine::Tick();

            // editor
            if (render_editor)
            {
                BeginWindow();

                for (shared_ptr<Widget>& widget : m_widgets)
                {
                    widget->Tick();
                }
                MenuBar::Tick();

                ImGui::End();

                // various windows that don't belong to a certain widget
                GeneralWindows::Tick();
            }
        }

        // render
        if (render_editor)
        {
            ImGui::Render();

            // record main imgui first, then create/present child viewports before presenting
            // the main swapchain, otherwise undocked content vanishes for a frame
            spartan::Renderer::AcquireSwapchainImage();
            ImGui::RHI::render(ImGui::GetDrawData());

            if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
            {
                ImGui::UpdatePlatformWindows();
                ImGui::RenderPlatformWindowsDefault();
            }

            spartan::Renderer::SubmitAndPresent();
        }

        spartan::Profiler::TimeBlockEnd(
            spartan::TimeBlockType::Cpu
        );
        spartan::Timer::PostTick();
        spartan::Profiler::PostTick();
    }
}

void Editor::BeginWindow()
{
    // note: don't use ImGuiWindowFlags_MenuBar here since we use BeginMainMenuBar() separately
    const auto window_flags =
        ImGuiWindowFlags_NoDocking             |
        ImGuiWindowFlags_NoTitleBar            |
        ImGuiWindowFlags_NoCollapse            |
        ImGuiWindowFlags_NoResize              |
        ImGuiWindowFlags_NoMove                |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoNavFocus;

    // set window position and size to the work area (excludes main menu bar)
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);

    // draw window border for borderless window (use full viewport for border around entire window)
    {
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        ImVec2 min = viewport->Pos;
        ImVec2 max = ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y);
        ImU32 border_color = ImGui::ColorConvertFloat4ToU32(
            ImGui::Style::color_border
        );
        draw_list->AddRect(min, max, border_color, 0.0f, 1.0f);
    }

    // set window style
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding,   0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.0f, 0.0f));

    // begin window
    const char* name = "##main_window";
    bool open = true;
    ImGui::Begin(name, &open, window_flags);
    ImGui::PopStyleVar(3);

    // begin dock space
    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_DockingEnable)
    {
        // dock space
        const auto window_id = ImGui::GetID(name);
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
            ImGuiID dock_down_right_id = ImGui::DockBuilderSplitNode(dock_down_id,  ImGuiDir_Right, 0.3f,  nullptr, &dock_down_id);

            // dock windows
            ImGui::DockBuilderDockWindow("World",      dock_right_id);
            ImGui::DockBuilderDockWindow("Properties", dock_right_down_id);
            ImGui::DockBuilderDockWindow("Console",    dock_down_id);
            ImGui::DockBuilderDockWindow("Assets",     dock_down_right_id);
            ImGui::DockBuilderDockWindow("Viewport",   dock_main_id);
            ImGui::DockBuilderDockWindow("Sequencer",  dock_down_id);

            ImGui::DockBuilderFinish(dock_main_id);
        }

        const float footer_height = ImGui::EditorUi::scaled(24.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::DockSpace(
            window_id,
            ImVec2(0.0f, -footer_height),
            ImGuiDockNodeFlags_PassthruCentralNode
        );
        ImGui::PopStyleVar();
        draw_status_footer();
    }
}
