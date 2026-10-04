/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include "Selection.h"
#include "game/GameWorld.h"
#include "game/CarMcp.h"
#include "game/CameraController.h"
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
//===============================

//= NAMESPACES =====
using namespace std;
//==================

Editor::Editor(const vector<string>& args)
{
    spartan::Engine::Initialize(args);
    spartan::game::Initialize();
    spartan::game::RegisterCarMcpCommands();
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
    MenuBar::Initialize(this);
    editor_mcp::Register(this);
    GeneralWindows::Initialize(this);
}

Editor::~Editor()
{
    editor_mcp::Unregister();
    AssetThumbnails::Shutdown();
    WorldPreviews::Shutdown();
    spartan::gui::texture_preview = nullptr;
    editor_imgui::shutdown();
    spartan::Engine::Shutdown();
}

void Editor::Tick()
{
    spartan::Timer::Reset();
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
