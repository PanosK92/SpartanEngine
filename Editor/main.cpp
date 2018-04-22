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

//= INCLUDES =======================
#include "Window.h"
#include "Editor.h"
#include "ImGui/imgui.h"
#include "UI/ImGui_Implementation.h"
#include "Core/Engine.h"
#include "Core/Backends_Imp.h"
#include "Graphics/Renderer.h"
//==================================

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
	{
		LOG_ERROR("Directus_SetOutputFrameSize: Failed");
		return;
	}

	g_renderer->SetBackBufferSize(width, height);
	g_editor->Resize();
}

void Engine_Initialize(HWND windowHandle, HINSTANCE windowInstance, int windowWidth, int windowHeight)
{
	ImGui::CreateContext();

	// Create the editor before the engine because it implements
	// the engine's logging system and will catch all output from it.
	g_editor = make_unique<Editor>();

	// Create and initialize the engine
	Engine::SetHandles(windowHandle, windowHandle, windowInstance);
	g_engine = make_unique<Engine>(new Context);
	g_engine->Initialize();

	// Keep some useful subsystems
	g_engineContext = g_engine->GetContext();
	g_renderer		= g_engineContext->GetSubsystem<Renderer>();
	g_input			= g_engineContext->GetSubsystem<Input>();

	Directus_SetOutputFrameSize(windowWidth, windowHeight);

	// Initialize
	g_editor->Initialize(g_engineContext);
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

		// Set back buffer as render target (for ImGUI to render on)
		g_renderer->SetRenderTarget(nullptr);

		// Update editor
		g_editor->Update();

		// Present back buffer (ImGui result)
		g_renderer->Present();
	}

	// Shutdown
	{
		g_editor->Shutdown();
		g_editor.release();

		g_engine->Shutdown();
		g_engine.release();

		ImGui::DestroyContext();
	}

    return 0;
}