/*
Copyright(c) 2015-2026 Panos Karabelas & Thomas Ray

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

//= INCLUDES ======================
#include "ImGui/Source/imgui.h"
#include <functional>
#include <string>
//=================================

// FWD DECLARATIONS =
class Editor;
//===================

class Modal
{
public:
    // Result returned when the user interacts with the popup
    enum class Result : uint8_t
    {
        None,       // Popup is still open / no interaction yet
        Confirmed,  // User clicked confirm/yes button
        Cancelled   // User clicked cancel/no button or dismissed
    };

    // Specification for creating a modal popup
    struct ModalPanel
    {
        std::string title                    = "Popup";
        std::string message;
        std::string confirm_text             = "OK";
        std::string cancel_text              = "Cancel";
        bool show_cancel_button              = true;
        float dim_alpha                      = 0.6f;  // Background dim opacity (0.0 - 1.0)
        float blur_strength                  = 0.0f;  // Reserved for future blur implementation
        ImVec2 min_size                      = ImVec2(300, 0);
        ImVec2 max_size                      = ImVec2(600, 400);
        std::function<void()> custom_content = nullptr;  // Optional custom content callback
    };

    Modal() = default;
    Modal(const ModalPanel& spec);
    static void Initialize(Editor* editor);
    static void Show(const ModalPanel& spec);  // Show a modal popup with the given specification
    static void Tick();
    static void Close();

    // Show a simple message popup (single OK button)
    static void ShowMessage(const std::string& title, const std::string& message);

    // Show a confirmation popup (Yes/No buttons)
    static void ShowConfirmation(const std::string& title, const std::string& message,
        std::function<void()> on_confirm = nullptr,std::function<void()> on_cancel = nullptr);

    static bool IsActive();         // Check if a modal is currently active
    static Result GetLastResult();  // Get the result of the last popup interaction

    /**
     * @brief Renders a modal header with optional indentation control.
     * @param text The header text to display.
     * @param indent_After Whether to indent after the header.
     * @param unindent_Before Whether to unindent before the header.
     */
    static void ModalHeader(const std::string& text, bool indent_After = true, bool unindent_Before = false);

};
