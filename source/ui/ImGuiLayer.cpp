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
    if (editor && spartan::RHI_Device::SupportsImGuiMultiViewport() && !Engine::IsHeadless())
    {
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    }
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigWindowsResizeFromEdges = true;
    io.IniFilename = editor ? "settings/editor.ini" : nullptr;
    io.LogFilename = "logs/imgui_log.txt";

    const string dir_fonts =
        spartan::ResourceCache::GetResourceDirectory(
            spartan::ResourceDirectory::Fonts
        ) + "/";
    const float scaled_font_size =
        font_size *
        spartan::Window::GetDpiScale();
    ImFontConfig config;
    config.Flags |= ImFontFlags_NoLoadError;
    config.SizePixels = scaled_font_size;
    auto load_font = [&](const char* file, ImFont* fallback)
    {
        ImFont* font = io.Fonts->AddFontFromFileTTF((dir_fonts + file).c_str(), scaled_font_size, &config);
        return font ? font : fallback;
    };
    font_normal = load_font("Inter/Inter-Regular.ttf", nullptr);
    if (!font_normal)
        font_normal = io.Fonts->AddFontDefault(&config);
    config.GlyphOffset.y = -1.0f;
    font_bold        = load_font("Inter/Inter-SemiBold.ttf", font_normal);
    font_mono        = load_font("JetBrainsMono/JetBrainsMono-Regular.ttf", font_normal);
    font_mono_medium = load_font("JetBrainsMono/JetBrainsMono-Medium.ttf", font_bold);
    ImGui::GetStyle().FontScaleMain = font_scale;

    if (Engine::IsStartupSmokeTest())
    {
        io.IniFilename = nullptr;
        return; // exercise editor context/fonts without creating platform or renderer backends
    }

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

    // the hidden editor never lays out its panels, saving would clobber the user's layout
    if (spartan::Engine::IsHeadless())
    {
        ImGui::GetIO().IniFilename = nullptr;
    }

    if (!Engine::IsStartupSmokeTest())
    {
        ImGui::RHI::shutdown();
        ImGui_ImplSDL3_Shutdown();
    }
    ImGui::DestroyContext();
}

void spartan::gui::glass_pane(const ImVec2& min, const ImVec2& max, const float rounding)
{
    ImGui::RHI::add_glass_pane(min, max, rounding);
}

void spartan::gui::begin_frame()
{
    ImGui::RHI::clear_glass_panes();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (spartan::Engine::IsHeadless())
    {
        ImGui::GetIO().WantSaveIniSettings = false;
    }

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
    bool ui_rendered = false;
    {
        spartan::ScopedTimeBlock block("ui_record");
        ui_rendered = ImGui::RHI::render(ImGui::GetDrawData());
    }

    if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
    }

    const spartan::RHI_Work submitted = spartan::RHI_Device::EndFrame();
    spartan::Renderer::SetFrameCompletion(submitted.timeline.get(), submitted.value);
    spartan::Renderer::FinalizeScreenshotReadback();

    // only dismiss the splash screen and show the main window once a real ui frame has actually been drawn, presented, and completed on the gpu
    if (!spartan::Renderer::HasCompletedFirstFrame() && spartan::Renderer::GetFrameNumber() >= 1 && (ui_rendered || spartan::Engine::IsHeadless()))
    {
        if (submitted.timeline && submitted.value > 0)
        {
            submitted.timeline->Wait(UINT64_MAX, submitted.value);
        }
        spartan::Renderer::NotifyFirstFrameCompleted();
    }
}
