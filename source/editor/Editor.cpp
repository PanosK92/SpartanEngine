/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================
#include "pch.h"
#include "Editor.h"
#include "EditorImGui.h"
#include "EditorLayout.h"
#include "GeneralWindows.h"
#include "WorldPreviews.h"
#include "widgets/MenuBar.h"
#include "core/Engine.h"
#include "core/Timer.h"
#include "core/Window.h"
#include "input/Input.h"
#include "profiling/Profiler.h"
#include "mcp/EditorMcpCommands.h"
#include "world/World.h"
#include "rendering/Renderer.h"
//===============================

//= NAMESPACES =====
using namespace std;
//==================

Editor::Editor(const vector<string>& args)
{
    spartan::Engine::Initialize(args);
    editor_imgui::initialize();
    RegisterWidgets();
    MenuBar::Initialize(this);
    editor_mcp::Register(this);
    GeneralWindows::Initialize(this);
}

Editor::~Editor()
{
    editor_mcp::Unregister();
    WorldPreviews::Shutdown();
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
        ) && !(spartan::Engine::HasArgument("--mcp-control") && spartan::Engine::HasArgument("--mcp-hidden"));
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
