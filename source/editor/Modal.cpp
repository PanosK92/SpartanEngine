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

//= INCLUDES =======================
#include "pch.h"
#include "Modal.h"
#include <utility>
#include "Editor.h"
#include "ImGui/Source/imgui.h"
#include "ImGui/Source/imgui_internal.h"
#include "ImGui/ImGui_Extension.h"
#include "Window.h"
//================================

//= NAMESPACES =====
using namespace std;
//==================

namespace
{
    Editor* editor     = nullptr;
    bool is_active     = false;
    bool should_open   = false;
    Modal::Result last_result = Modal::Result::None;
    Modal::Spec current_spec;
    function<void()> on_confirm_callback = nullptr;
    function<void()> on_cancel_callback  = nullptr;
    
    // Animation state
    float dim_animation         = 0.0f;
    float popup_animation       = 0.0f;
    const float animation_speed = 8.0f;
    
    void DrawDimmedBackground()
    {
        // Get the main viewport to cover the entire window
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
    
        // Calculate animated alpha
        float target_alpha = is_active ? current_spec.dim_alpha : 0.0f;
        dim_animation      = ImLerp(dim_animation, target_alpha, ImGui::GetIO().DeltaTime * animation_speed);
    
        if (dim_animation < 0.001f) return;
    
        // Draw fullscreen dim overlay on the foreground draw list
        ImDrawList* draw_list = ImGui::GetForegroundDrawList();
        ImU32 dim_color       = IM_COL32(0, 0, 0, static_cast<int>(dim_animation * 255.0f));
    
        draw_list->AddRectFilled(
            viewport->Pos, ImVec2(viewport->Pos.x + viewport->Size.x, viewport->Pos.y + viewport->Size.y), dim_color);
    }
    
    void DrawBlockingOverlay()
    {
        if (!is_active) return;
    
        // Create an invisible fullscreen window that captures all input
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
    
        ImGui::SetNextWindowPos(viewport->Pos);
        ImGui::SetNextWindowSize(viewport->Size);
    
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoCollapse |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoDocking;
    
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    
        ImGui::Begin("##modal_blocking_overlay", nullptr, flags);
        ImGui::End();
    
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
        
    void DrawPopupWindow()
    {
        if (!is_active && popup_animation < 0.001f) return;
    
        // Animate popup scale/alpha
        float target_scale = is_active ? 1.0f : 0.0f;
        popup_animation    = ImLerp(popup_animation, target_scale, ImGui::GetIO().DeltaTime * animation_speed);
    
        if (popup_animation < 0.001f) return;
    
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImVec2 center                 = ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f, viewport->Pos.y + viewport->Size.y * 0.5f);
    
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(current_spec.min_size, current_spec.max_size);
    
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoDocking |
                                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize;
    
        // Apply animation alpha
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, popup_animation);
    
        // Make the popup window stand out
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * spartan::Window::GetDpiScale());
        float dpi_scale = spartan::Window::GetDpiScale();
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20.0f * dpi_scale, 16.0f * dpi_scale));
    
        string window_title = current_spec.title + "###modal_popup";
    
        if (ImGui::Begin(window_title.c_str(), nullptr, flags))
        {
            // Ensure this window is always on top and focused
            ImGui::SetWindowFocus();
    
            // Message text with wrapping
            if (!current_spec.message.empty())
            {
                ImGui::PushTextWrapPos(ImGui::GetCursorPos().x + current_spec.max_size.x - 40.0f);
                ImGui::TextUnformatted(current_spec.message.c_str());
                ImGui::PopTextWrapPos();
                ImGui::Spacing();
                ImGui::Spacing();
            }
    
            // Custom content callback
            if (current_spec.custom_content)
            {
                current_spec.custom_content();
                ImGui::Spacing();
            }
    
            ImGui::Separator();
            ImGui::Spacing();
    
            // Buttons
            float button_width = 100.0f * spartan::Window::GetDpiScale();
            float total_width  = button_width;
            if (current_spec.show_cancel_button) { total_width += button_width + ImGui::GetStyle().ItemSpacing.x; }
    
            // Center the buttons
            float avail_width = ImGui::GetContentRegionAvail().x;
            float offset      = (avail_width - total_width) * 0.5f;
            if (offset > 0.0f) { ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset); }
    
            // Confirm button
            if (ImGuiSp::button(current_spec.confirm_text.c_str(), ImVec2(button_width, 0)))
            {
                last_result = Modal::Result::Confirmed;
                is_active   = false;
    
                if (on_confirm_callback) { on_confirm_callback(); }
            }
    
            // Cancel button
            if (current_spec.show_cancel_button)
            {
                ImGui::SameLine();
                if (ImGuiSp::button(current_spec.cancel_text.c_str(), ImVec2(button_width, 0)))
                {
                    last_result = Modal::Result::Cancelled;
                    is_active   = false;
    
                    if (on_cancel_callback) { on_cancel_callback(); }
                }
            }
    
            // Handle Escape key to cancel
            if (ImGui::IsKeyPressed(ImGuiKey_Escape) && current_spec.show_cancel_button)
            {
                last_result = Modal::Result::Cancelled;
                is_active   = false;
    
                if (on_cancel_callback) { on_cancel_callback(); }
            }
    
            // Handle Enter key to confirm
            if (ImGui::IsKeyPressed(ImGuiKey_Enter) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter))
            {
                last_result = Modal::Result::Confirmed;
                is_active   = false;
    
                if (on_confirm_callback) { on_confirm_callback(); }
            }
        }
        ImGui::End();
    
        ImGui::PopStyleVar(3);
    }

}  // namespace

Modal::Modal(const Spec& spec)
{
    current_spec = spec;
}

void Modal::Initialize(Editor* editor_in)
{
    editor = editor_in;
}

void Modal::Show(const Spec& spec)
{
    current_spec        = spec;
    is_active           = true;
    should_open         = true;
    last_result         = Result::None;
    on_confirm_callback = nullptr;
    on_cancel_callback  = nullptr;
}

void Modal::ShowMessage(const string& title, const string& message)
{
    Spec spec;
    spec.title              = title;
    spec.message            = message;
    spec.confirm_text       = "OK";
    spec.show_cancel_button = false;
    Show(spec);
}

void Modal::ShowConfirmation(const string& title, const string& message, function<void()> on_confirm, function<void()> on_cancel)
{
    Spec spec;
    spec.title              = title;
    spec.message            = message;
    spec.confirm_text       = "Yes";
    spec.cancel_text        = "No";
    spec.show_cancel_button = true;

    on_confirm_callback = std::move(on_confirm);
    on_cancel_callback  = std::move(on_cancel);

    Show(spec);
}

void Modal::Tick()
{
    // Draw the dimmed background first (behind everything except the popup)
    DrawDimmedBackground();

    // Draw invisible blocking overlay to capture clicks
    DrawBlockingOverlay();

    // Draw the actual popup window on top
    DrawPopupWindow();

    // Reset animation when fully closed
    if (!is_active && dim_animation < 0.001f && popup_animation < 0.001f)
    {
        on_confirm_callback = nullptr;
        on_cancel_callback  = nullptr;
    }
}

bool Modal::IsActive() { return is_active || dim_animation > 0.001f; }

Modal::Result Modal::GetLastResult() { return last_result; }

void Modal::ModalHeader(const std::string& text, bool indentAfter, bool unindend_Before)
{
    if (unindend_Before) ImGui::Unindent();
    ImGui::TextColored(ImColor(170, 170, 170).Value, text.c_str());
    ImGui::Separator();
    if (indentAfter) ImGui::Indent();
}

void Modal::Close()
{
    is_active   = false;
    last_result = Result::Cancelled;
}
