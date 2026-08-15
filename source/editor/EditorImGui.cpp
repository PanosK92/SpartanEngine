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
#include "EditorImGui.h"
#include "core/Event.h"
#include "imgui/implementation/ImGui_RHI.h"
#include "imgui/implementation/imgui_impl_sdl3.h"
#include "input/Input.h"
#include "rendering/Renderer.h"
#include "rhi/RHI_Device.h"
#include "resource/ResourceCache.h"
#include "rhi/RHI_Implementation.h"
#include "Window.h"
//===============================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    constexpr float font_size  = 14.0f;
    constexpr float font_scale = 1.0f;

    void process_event(spartan::sp_variant data)
    {
        SDL_Event* event_sdl = static_cast<SDL_Event*>(get<void*>(data));
        ImGui_ImplSDL3_ProcessEvent(event_sdl);
    }
}

void editor_imgui::initialize()
{
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (spartan::RHI_Context::supports_imgui_multi_viewport)
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigWindowsResizeFromEdges = true;
    io.IniFilename = "editor.ini";

    ImFontConfig config;
    config.GlyphOffset.y = -1.0f;

    const string dir_fonts =
        spartan::ResourceCache::GetResourceDirectory(
            spartan::ResourceDirectory::Fonts
        ) + "/";
    const float scaled_font_size =
        font_size *
        spartan::Window::GetDpiScale();
    Editor::font_normal = io.Fonts->AddFontFromFileTTF(
        (dir_fonts + "Inter/Inter-Regular.ttf").c_str(),
        scaled_font_size
    );
    Editor::font_bold = io.Fonts->AddFontFromFileTTF(
        (dir_fonts + "Inter/Inter-SemiBold.ttf").c_str(),
        scaled_font_size,
        &config
    );
    ImGui::GetStyle().FontScaleMain = font_scale;

    SP_ASSERT_MSG(
        ImGui::RHI::InitializePlatformBackend(spartan::Window::GetHandleSDL()),
        "Failed to initialize ImGui's SDL backend"
    );
    ImGui::RHI::Initialize();

    SP_SUBSCRIBE_TO_EVENT(
        spartan::EventType::Sdl,
        SP_EVENT_HANDLER_VARIANT_STATIC(process_event)
    );
}

void editor_imgui::shutdown()
{
    if (!ImGui::GetCurrentContext())
    {
        return;
    }

    ImGui::RHI::shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void editor_imgui::begin_frame()
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiIO& io = ImGui::GetIO();
    spartan::Input::SetBlockedByUi(io.WantTextInput);
}

void editor_imgui::render()
{
    ImGui::Render();

    spartan::RHI_Device::AcquireSwapChainImage();
    ImGui::RHI::render(ImGui::GetDrawData());

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    spartan::RHI_Device::EndFrame();
    spartan::Renderer::FinalizeScreenshotReadback();
}
