/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ======================
#include "Window.h"
#include "Editor.h"
#include "ImGui/Source/imgui.h"
#include "ImGui/imgui_impl_win32.h"
#include "Input/Backend_Def.h"
#include "Input/Backend_Imp.h"
#include "Core/Engine.h"
#include "Rendering/Renderer.h"
//=================================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

static std::unique_ptr<Editor> g_editor;
static std::unique_ptr<Engine> g_engine;
static Context* g_engineContext	= nullptr;
static Renderer* g_renderer		= nullptr;
static Input* g_input			= nullptr;

void Directus_SetOutputFrameSize(int width, int height)
{
	if (!g_renderer || !g_editor)
		return;

	g_renderer->SetBackBufferSize(width, height);
	g_editor->Resize();
}

void Engine_Initialize(HWND windowHandle, HINSTANCE windowInstance, int windowWidth, int windowHeight)
{
	// 1. Create the editor but don't initialize it yet. This is because the editor implements the engine's
	// logging system and can display all output in the console upon initialization. We don't want to lose that.
	g_editor = make_unique<Editor>();

	// 2. Create and initialize the engine
	Engine::SetHandles(windowHandle, windowHandle, windowInstance);
	g_engine = make_unique<Engine>(new Context);
	g_engine->Initialize();

	// Keep some useful subsystems around
	g_engineContext = g_engine->GetContext();
	g_renderer		= g_engineContext->GetSubsystem<Renderer>();
	g_input			= g_engineContext->GetSubsystem<Input>();
	Directus_SetOutputFrameSize(windowWidth, windowHeight);

	// 3. Initialize the editor now that we have everything it needs (console handle, initialized D3D11 device)
	g_editor->Initialize(g_engineContext, windowHandle);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	Window::g_OnMessage = ImGui_ImplWin32_WndProcHandler;
	Window::g_onResize	= Directus_SetOutputFrameSize;
	Window::Create(hInstance, "Directus " + string(ENGINE_VERSION));	
	Window::Show();

	Engine_Initialize(Window::g_handle, hInstance, Window::GetWidth(), Window::GetHeight());

    // Tick
	while (Window::Tick())
    {
		// Update engine (will simulate and render)
		g_engine->Tick();

		// Set back buffer as render target (for ImGui to render on)
		g_renderer->SetRenderTarget(nullptr);

		// Update editor
		g_editor->Update(g_engine->GetDeltaTime());

		// Present back buffer (ImGui result)
		g_renderer->Present();
	}

	// Shutdown
	{
		g_editor->Shutdown();
		g_editor.release();

		g_engine->Shutdown();
		g_engine.release();
	}

    return 0;
}