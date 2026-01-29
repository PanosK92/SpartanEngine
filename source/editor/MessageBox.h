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

#define BIT(x) (1u << x)
#define SPARTAN_MSG_BOX_OK_BTN BIT(0)
#define SPARTAN_MSG_BOX_CANCEL_BTN BIT(1)
#define SPARTAN_MSG_BOX_FUNC BIT(2)
#define SPARTAN_MSG_BOX_AUTO_SIZE BIT(3)

struct MessageBoxData
{
    std::string title;
    std::string body;
    uint32_t flags      = 0;
    uint32_t width      = 0;
    uint32_t height     = 0;
    uint32_t min_width  = 0;
    uint32_t min_height = 0;
    uint32_t max_width  = -1;
    uint32_t max_height = -1;
    std::function<void()> user_render_function;
    bool should_open = true;
    bool is_open     = false;
};

static std::unordered_map<std::string, MessageBoxData> s_MessageBoxes;

static void PushFontBold() { ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[0]); }

static void PushFontLarge() { ImGui::PushFont(ImGui::GetIO().Fonts->Fonts[1]); }

template <uint32_t flags = 0, typename... Args>
static void ShowSimpleMessageBox(const char* title, std::format_string<Args...> message, Args&&... args)
{
    auto& messageBoxData       = s_MessageBoxes[title];
    messageBoxData.title       = std::format("{0}##MessageBox{1}", title, s_MessageBoxes.size() + 1);
    messageBoxData.body        = std::format(message, std::forward<Args>(args)...);
    messageBoxData.flags       = flags;
    messageBoxData.width       = 600;
    messageBoxData.height      = 0;
    messageBoxData.should_open = true;
}

static void ShowMessageBox(const char* title, const std::function<void()>& renderFunction, uint32_t width = 600, uint32_t height = 0,
    uint32_t minWidth = 0, uint32_t minHeight = 0, uint32_t maxWidth = -1, uint32_t maxHeight = -1, uint32_t flags = SPARTAN_MSG_BOX_AUTO_SIZE)
{
    auto& messageBoxData                = s_MessageBoxes[title];
    messageBoxData.title                = std::format("{0}##MessageBox{1}", title, s_MessageBoxes.size() + 1);
    messageBoxData.user_render_function = renderFunction;
    messageBoxData.flags                = SPARTAN_MSG_BOX_FUNC | flags;
    messageBoxData.width                = width;
    messageBoxData.height               = height;
    messageBoxData.min_width            = minWidth;
    messageBoxData.min_height           = minHeight;
    messageBoxData.max_width            = maxWidth;
    messageBoxData.max_height           = maxHeight;
    messageBoxData.should_open          = true;
}
