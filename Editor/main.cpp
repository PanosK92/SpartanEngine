/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES =====================================
#include "Window.h"
#include "Editor.h"
#include "ImGui/Implementation/imgui_impl_win32.h"
//================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	std::unique_ptr<Editor> editor;

	// Create window
	Window::g_OnMessage = ImGui_ImplWin32_WndProcHandler;
	Window::g_onResize	= [&editor](unsigned int width, unsigned int height) { editor->Resize(width, height); };
	Window::Create(hInstance, "Directus " + std::string(ENGINE_VERSION));	
	Window::Show();

	// Create editor
	editor = std::make_unique<Editor>(Window::g_handle, hInstance, Window::GetWidth(), Window::GetHeight());

    // Tick
	while (Window::Tick()) 
	{ 
		editor->Tick();
	}

	Window::Destroy();
    return 0;
}