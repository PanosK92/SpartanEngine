/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include "Selection.h"
#include "world/WorldSystems.h"
#include "mcp/McpCommandsCar.h"
#include "world/CameraController.h"
#include "Editor.h"
#include "EditorImGui.h"
#include "EditorLayout.h"
#include "GeneralWindows.h"
#include "WorldPreviews.h"
#include "AssetThumbnails.h"
#include "widgets/MenuBar.h"
#include "widgets/TextureViewer.h"
#include "core/Engine.h"
#include "core/Timer.h"
#include "core/Window.h"
#include "input/Input.h"
#include "profiling/Profiler.h"
#include "mcp/EditorMcpCommands.h"
#include "mcp/McpCommandsDiagnostics.h"
#include "mcp/McpCommandsComponents.h"
#include "world/World.h"
#include "rendering/Renderer.h"
#include "imgui/source/imgui_internal.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    // a [Panel][Title] section in editor.ini per docked panel, imgui keeps the dock placement but not whether it was open
    void* panel_settings_read_open(ImGuiContext*, ImGuiSettingsHandler* handler, const char* name)
    {
        Widget* match = nullptr;
        static_cast<Editor*>(handler->UserData)->ForEachWidget([&](Widget* widget)
        {
            if (!match && widget->PersistsVisibility() && strcmp(widget->GetTitle(), name) == 0)
            {
                match = widget;
            }
        });
        return match;
    }

    void panel_settings_read_line(ImGuiContext*, ImGuiSettingsHandler*, void* entry, const char* line)
    {
        constexpr const char* key = "Visible=";
        if (strncmp(line, key, strlen(key)) == 0)
        {
            static_cast<Widget*>(entry)->RestoreVisibility(atoi(line + strlen(key)) != 0);
        }
    }

    void panel_settings_write_all(ImGuiContext*, ImGuiSettingsHandler* handler, ImGuiTextBuffer* buffer)
    {
        static_cast<Editor*>(handler->UserData)->ForEachWidget([&](Widget* widget)
        {
            if (widget->PersistsVisibility())
            {
                buffer->appendf("[%s][%s]\nVisible=%d\n\n", handler->TypeName, widget->GetTitle(), widget->GetVisible() ? 1 : 0);
            }
        });
    }

    // must run before the first frame, that is when imgui reads editor.ini
    void register_panel_settings(Editor* editor)
    {
        ImGuiSettingsHandler handler;
        handler.TypeName   = "Panel";
        handler.TypeHash   = ImHashStr("Panel");
        handler.ReadOpenFn = panel_settings_read_open;
        handler.ReadLineFn = panel_settings_read_line;
        handler.WriteAllFn = panel_settings_write_all;
        handler.UserData   = editor;
        ImGui::AddSettingsHandler(&handler);
    }
}

Editor::Editor(const vector<string>& args)
{
    spartan::Engine::Initialize(args);
    spartan::world_systems::Initialize();
    spartan::RegisterCarMcpCommands();
    spartan::mcp_diagnostics::Register();
    spartan::mcp_components::Register();
    editor_imgui::initialize();
    spartan::Selection::Initialize();
    SP_SUBSCRIBE_TO_EVENT(spartan::EventType::InputTicked, [](spartan::sp_variant)
    {
        if (spartan::Engine::IsFlagSet(spartan::EngineMode::EditorVisible) && spartan::Input::GetKeyDown(spartan::KeyCode::F))
            if (auto* camera = spartan::World::GetCamera())
                spartan::CameraController::Get(*camera).Focus(spartan::Selection::GetSelectedEntity(), spartan::Selection::GetSelectedInstance());
    });
    spartan::gui::texture_preview = [] { return spartan::gui::TexturePreview{
        TextureViewer::GetVisualisedTextureId(), TextureViewer::GetVisualisationFlags(),
        TextureViewer::GetMipLevel(), TextureViewer::GetArrayLevel()}; };
    RegisterWidgets();
    register_panel_settings(this);
    MenuBar::Initialize(this);
    editor_mcp::Register(this);
    if (!spartan::Engine::IsStartupSmokeTest())
        GeneralWindows::Initialize(this); // welcome/update services may access the network
    else
        SP_LOG_INFO("Startup smoke test: editor initialized, entering idle loop");
}

Editor::~Editor()
{
    editor_mcp::Unregister();
    if (!spartan::Engine::IsStartupSmokeTest())
    {
        AssetThumbnails::Shutdown();
        WorldPreviews::Shutdown();
    }
    spartan::gui::texture_preview = nullptr;
    editor_imgui::shutdown();
    spartan::Engine::Shutdown();
}

void Editor::Tick()
{
    spartan::Timer::Reset();
    if (spartan::Engine::IsStartupSmokeTest())
    {
        for (uint32_t tick = 0; tick < 10; ++tick)
        {
            spartan::Engine::Tick();
            spartan::Timer::PostTick();
        }
        SP_LOG_INFO("Startup smoke test: completed 10 editor idle ticks");
        return;
    }
    while (!spartan::Window::WantsToClose())
    {
        spartan::Profiler::FrameStart();
        spartan::Profiler::TimeBlockStart(
            "frame_active",
            spartan::TimeBlockType::Cpu
        );

        spartan::World::ProcessPendingLoad();

        const bool render_editor = spartan::Engine::IsFlagSet(
            spartan::EngineMode::EditorVisible
        ) && !spartan::Engine::IsHeadless();
        // Runtime HUDs also use ImGui. Keep its frame and presentation alive
        // when the editor panels are hidden.
        spartan::Renderer::SetPresentInRenderer(false);
        editor_imgui::begin_frame();
        if (!render_editor)
            spartan::Input::SetBlockedByUi(false);

        spartan::Engine::Tick();

        if (render_editor)
        {
            editor_layout::begin_root(this);

            for (unique_ptr<Widget>& widget : m_widgets)
            {
                widget->Tick();
            }
            MenuBar::Tick();

            editor_layout::end_root();
            GeneralWindows::Tick();
        }
        editor_imgui::render();

        spartan::Profiler::TimeBlockEnd(spartan::TimeBlockType::Cpu);
        spartan::Timer::PostTick();
        spartan::Profiler::PostTick();
    }
}
