/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ====================================
#include "pch.h"
#include "ImGui.h"
#include "core/Event.h"
#include "core/Engine.h"
#include "imgui/implementation/ImGui_RHI.h"
#include "imgui/implementation/imgui_impl_sdl3.h"
#include "input/Input.h"
#include "rendering/Renderer.h"
#include "profiling/Profiler.h"
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

void spartan::gui::initialize(bool editor)
{
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    if (editor) io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (editor && spartan::RHI_Context::supports_imgui_multi_viewport)
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigWindowsResizeFromEdges = true;
    io.IniFilename = editor ? "editor.ini" : nullptr;

    ImFontConfig config;
    config.GlyphOffset.y = -1.0f;

    const string dir_fonts =
        spartan::ResourceCache::GetResourceDirectory(
            spartan::ResourceDirectory::Fonts
        ) + "/";
    const float scaled_font_size =
        font_size *
        spartan::Window::GetDpiScale();
    spartan::gui::font_normal = io.Fonts->AddFontFromFileTTF(
        (dir_fonts + "Inter/Inter-Regular.ttf").c_str(),
        scaled_font_size
    );
    spartan::gui::font_bold = io.Fonts->AddFontFromFileTTF(
        (dir_fonts + "Inter/Inter-SemiBold.ttf").c_str(),
        scaled_font_size,
        &config
    );

    // numerics and micro labels, falls back to the text font so a missing file never takes the editor down
    const string mono_regular = dir_fonts + "JetBrainsMono/JetBrainsMono-Regular.ttf";
    const string mono_medium  = dir_fonts + "JetBrainsMono/JetBrainsMono-Medium.ttf";
    spartan::gui::font_mono        = spartan::FileSystem::Exists(mono_regular) ? io.Fonts->AddFontFromFileTTF(mono_regular.c_str(), scaled_font_size, &config) : spartan::gui::font_normal;
    spartan::gui::font_mono_medium = spartan::FileSystem::Exists(mono_medium) ? io.Fonts->AddFontFromFileTTF(mono_medium.c_str(), scaled_font_size, &config) : spartan::gui::font_bold;
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

void spartan::gui::shutdown()
{
    if (!ImGui::GetCurrentContext())
    {
        return;
    }

    ImGui::RHI::shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void spartan::gui::begin_frame()
{
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    const ImGuiIO& io = ImGui::GetIO();
    spartan::Input::SetBlockedByUi(io.WantTextInput);
    if (!spartan::Engine::IsFlagSet(spartan::EngineMode::EditorVisible))
    {
        spartan::Input::SetEditorViewportOffset(spartan::math::Vector2::Zero);
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        spartan::Input::SetMouseIsInViewport(ImGui::IsMouseHoveringRect(viewport->Pos,
            ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), false));
    }
}

void spartan::gui::render()
{
    spartan::ScopedTimeBlock time_block("spartan::gui::render");
    if (!spartan::Engine::IsFlagSet(spartan::EngineMode::EditorVisible))
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::GetBackgroundDrawList()->AddImage(
            reinterpret_cast<ImTextureID>(spartan::Renderer::GetRenderTarget(spartan::Renderer_RenderTarget::frame_output)),
            viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y));
    }
    {
        spartan::ScopedTimeBlock block("ui_layout");
        ImGui::Render();
    }
    {
        spartan::ScopedTimeBlock block("ui_acquire");
        spartan::RHI_Device::AcquireSwapChainImage();
    }
    {
        spartan::ScopedTimeBlock block("ui_record");
        ImGui::RHI::render(ImGui::GetDrawData());
    }

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    const spartan::RHI_Work submitted = spartan::RHI_Device::EndFrame();
    spartan::Renderer::SetFrameCompletion(submitted.timeline.get(), submitted.value);
    spartan::Renderer::FinalizeScreenshotReadback();
}
