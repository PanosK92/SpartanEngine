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

#pragma once

//= INCLUDES ==================================
#include "../ImGui/imgui.h"
#include "Graphics/D3D11/D3D11GraphicsDevice.h"
//=============================================

struct SDL_Window;
union SDL_Event;

IMGUI_API bool	ImGui_Impl_Initialize(SDL_Window* window, Directus::Context* context);
IMGUI_API bool	ImGui_Impl_ProcessEvent(SDL_Event* event);
IMGUI_API void	ImGui_Impl_Shutdown();
IMGUI_API void	ImGui_Impl_NewFrame(SDL_Window* window);

// Use if you want to reset your rendering device without losing ImGui state.
IMGUI_API void	ImGui_Impl_InvalidateDeviceObjects();
IMGUI_API bool	ImGui_Impl_CreateDeviceObjects();
