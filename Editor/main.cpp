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

//= INCLUDES =================
#include <iostream>
#include <SDL.h>
#include <SDL_syswm.h>
#include <Core/Engine.h>
#include "Graphics/Renderer.h"
#include "Editor.h"
#include "ImGui/imgui.h"
//============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
using namespace Math;
//=======================

SDL_Window* g_window		= nullptr;
Editor* g_editor			= nullptr;
Engine* g_engine			= nullptr;
Context* g_engineContext	= nullptr;
Graphics* g_graphics		= nullptr;
Renderer* g_renderer		= nullptr;
map<Uint32, bool> g_keys;

Vector2 GetResolutionDisplayPrimary()
{
	SDL_DisplayMode dm;
	if (SDL_GetCurrentDisplayMode(0, &dm) != 0)
	{
		LOG_ERROR("main: " + std::string(SDL_GetError()));
		return Vector2::Zero;
	}

	return Vector2((float)dm.w, (float)dm.h);
}

void SetBackBufferResolution(int width, int height)
{
	if (!g_renderer)
	{
		LOG_ERROR("main: Can't set resolution, renderer is null");
		return;
	}

	g_renderer->SetResolutionBackBuffer(width, height);
	g_renderer->SetViewportBackBuffer((float)width, (float)height);
}

void SetBackBufferResolution(const Vector2& resolution)
{
	SetBackBufferResolution((int)resolution.x, (int)resolution.y);
}

void Event_Input(SDL_Event* event)
{
	if (event->type == SDL_KEYDOWN)
	{
		g_keys[event->key.keysym.sym] = true;
	}
	else if (event->type == SDL_KEYUP)
	{
		g_keys[event->key.keysym.sym] = false;
	}
}

void Event_Window(SDL_Event* event)
{
	if (event->type != SDL_WINDOWEVENT)
		return;

	if (event->window.event == SDL_WINDOWEVENT_RESIZED)
	{
		SetBackBufferResolution(event->window.data1, event->window.data2);
	}
}

void Update(bool& isRunning, SDL_Event* event)
{
	// Check for update end
	if (event->type == SDL_QUIT || g_keys[SDLK_ESCAPE])
	{ 
		isRunning = false;
	}

	while (SDL_PollEvent(event))
	{
		Event_Window(event);
		Event_Input(event);
		g_editor->HandleEvent(event);
	}

	// Update engine (will simulate and render)
	g_engine->Update();
	// Set back buffer as render target (for ImGUI to render on)
	g_renderer->SetRenderTarget(nullptr);
	// Update editor
	g_editor->Update();
	// Present back buffer (ImGui result)
	g_renderer->Present();
}

int main(int argc, char* argv[])
{
	// Hide console
	auto console = GetConsoleWindow();
	ShowWindow(console, 0);

	// Initialize SDL
	if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_VIDEO))
	{
		LOG_ERROR("main: " + std::string(SDL_GetError()));
		return -1;
	}
	Settings::g_versionSDL = to_string(SDL_MAJOR_VERSION) + "." + to_string(SDL_MINOR_VERSION) + "." + to_string(SDL_PATCHLEVEL);

	// Get current resolution
	Vector2 resolution = GetResolutionDisplayPrimary();

	// Create window with SDL
	g_window = nullptr;
	g_window = SDL_CreateWindow(
		string("Directus " + string(ENGINE_VERSION)).c_str(),	// Title
		SDL_WINDOWPOS_CENTERED,									// X
		SDL_WINDOWPOS_CENTERED,									// Y
		(int)resolution.x,										// Width
		(int)resolution.y,										// Height
		SDL_WINDOW_MAXIMIZED | SDL_WINDOW_RESIZABLE				// Flags
	);
	SDL_SetWindowMinimumSize(g_window, 800, 600);

	// Get windows info
	SDL_SysWMinfo systemInfo;
	SDL_VERSION(&systemInfo.version);
	SDL_GetWindowWMInfo(g_window, &systemInfo);
	auto winHandle = systemInfo.info.win.window;
	auto winInstance = systemInfo.info.win.hinstance;
	
	// Create the editor before the engine because it implements
	// the engine's logging system and will catch all output from it.
	g_editor = new Editor();

	// Create and initialize the engine
	Engine::SetHandles(winHandle, winHandle, winInstance);
	g_engine = new Engine(new Context);
	g_engine->Initialize();
	g_engineContext = g_engine->GetContext();
	g_renderer = g_engineContext->GetSubsystem<Renderer>();

	// Initialize
	ImGui::CreateContext();
	g_editor->Initialize(g_window, g_engineContext);

	// Start loop
	bool isRunning = true;
	SDL_Event event;
	while (isRunning)
	{
		Update(isRunning, &event);
	}

	// Shutdown
	{
		g_editor->Shutdown();
		delete g_editor;
		ImGui::DestroyContext();

		g_engine->Shutdown();
		delete g_engine;
		
		SDL_DestroyWindow(g_window);
		SDL_Quit();		
	}

	// Exit
	return 0;
}
