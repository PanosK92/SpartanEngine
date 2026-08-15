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
    while (!spartan::Window::WantsToClose())
    {
        spartan::Profiler::FrameStart();
        spartan::Profiler::TimeBlockStart(
            "frame_active",
            spartan::TimeBlockType::Cpu
        );

        const bool render_editor = spartan::Engine::IsFlagSet(
            spartan::EngineMode::EditorVisible
        );
        spartan::Renderer::SetPresentInRenderer(!render_editor);

        if (render_editor)
        {
            editor_imgui::begin_frame();
        }
        else
        {
            spartan::Input::SetBlockedByUi(false);
        }

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
            editor_imgui::render();
        }

        spartan::Profiler::TimeBlockEnd(spartan::TimeBlockType::Cpu);
        spartan::Timer::PostTick();
        spartan::Profiler::PostTick();
    }
}
