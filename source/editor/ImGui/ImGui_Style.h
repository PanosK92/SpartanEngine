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
#pragma once

//= INCLUDES ============
#include "Source/imgui.h"
//=======================

namespace ImGui::Style
{
    // palette: content (dark/recessed), frame (medium/surrounding), text, accent
    static ImVec4 bg_color_1     = {0.118f, 0.118f, 0.125f, 1.0f};
    static ImVec4 bg_color_2     = {0.220f, 0.220f, 0.227f, 1.0f};
    static ImVec4 h_color_1      = {0.824f, 0.824f, 0.824f, 1.0f};
    static ImVec4 h_color_2      = {0.549f, 0.549f, 0.549f, 0.2f};
    static ImVec4 color_accent_1 = {0.000f, 0.784f, 1.000f, 1.0f};
    static ImVec4 color_accent_2 = {0.196f, 0.627f, 0.863f, 1.0f};

    // status colors
    static ImVec4 color_ok      = {0.361f, 0.722f, 0.361f, 1.0f};
    static ImVec4 color_info    = {0.784f, 0.784f, 0.784f, 1.0f};
    static ImVec4 color_warning = {0.941f, 0.678f, 0.306f, 1.0f};
    static ImVec4 color_error   = {0.851f, 0.325f, 0.310f, 1.0f};

    inline ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        );
    }

    inline void StyleSpartan()
    {
        // colors: dark content areas, lighter surrounding frame
        bg_color_1     = {0.118f, 0.118f, 0.125f, 1.0f};
        bg_color_2     = {0.220f, 0.220f, 0.227f, 1.0f};
        h_color_1      = {0.824f, 0.824f, 0.824f, 1.0f};
        h_color_2      = {0.549f, 0.549f, 0.549f, 0.2f};
        color_accent_1 = {0.000f, 0.784f, 1.000f, 1.0f};
        color_accent_2 = {0.196f, 0.627f, 0.863f, 1.0f};
        color_ok       = {0.361f, 0.722f, 0.361f, 1.0f};
        color_info     = {0.784f, 0.784f, 0.784f, 1.0f};
        color_warning  = {0.941f, 0.678f, 0.306f, 1.0f};
        color_error    = {0.851f, 0.325f, 0.310f, 1.0f};

        // layout: compact padding, minimal rounding
        ImGuiStyle& style          = ImGui::GetStyle();
        style.WindowPadding        = ImVec2(6.0f, 4.0f);
        style.FramePadding         = ImVec2(6.0f, 3.0f);
        style.CellPadding          = ImVec2(4.0f, 2.0f);
        style.ItemSpacing          = ImVec2(6.0f, 3.0f);
        style.ItemInnerSpacing     = ImVec2(4.0f, 2.0f);
        style.WindowRounding       = 0.0f;
        style.ChildRounding        = 0.0f;
        style.FrameRounding        = 1.0f;
        style.PopupRounding        = 0.0f;
        style.ScrollbarRounding    = 0.0f;
        style.GrabRounding         = 1.0f;
        style.TabRounding          = 0.0f;
        style.WindowBorderSize     = 1.0f;
        style.PopupBorderSize      = 1.0f;
        style.FrameBorderSize      = 0.0f;
        style.TabBorderSize        = 0.0f;
        style.ScrollbarSize        = 12.0f;
        style.GrabMinSize          = 8.0f;
        style.TabMinWidthForCloseButton = 0.0f;
        style.TabBarBorderSize          = 1.0f;  // line under tab bar that connects tabs as a group
        style.TabBarOverlineSize        = 2.0f;  // overline on selected tab
    }

    inline void StyleDark()
    {
        bg_color_1     = {0.10f, 0.10f, 0.10f, 1.0f};
        bg_color_2     = {0.59f, 0.59f, 0.59f, 1.0f};
        h_color_1      = {1.00f, 1.00f, 1.00f, 1.0f};
        h_color_2      = {1.00f, 1.00f, 1.00f, 0.1f};
        color_accent_1 = {0.231f, 0.310f, 1.000f, 1.0f};
        color_accent_2 = {0.176f, 0.314f, 1.000f, 1.0f};
        color_ok       = {0.200f, 0.702f, 0.349f, 1.0f};
        color_info     = {0.922f, 0.922f, 0.922f, 1.0f};
        color_warning  = {1.000f, 0.584f, 0.192f, 1.0f};
        color_error    = {1.000f, 0.227f, 0.227f, 1.0f};
    }

    inline void StyleLight()
    {
        bg_color_1     = {0.859f, 0.859f, 0.859f, 1.0f};
        bg_color_2     = {0.275f, 0.275f, 0.275f, 1.0f};
        h_color_1      = {0.027f, 0.027f, 0.027f, 1.0f};
        h_color_2      = {0.000f, 0.000f, 0.000f, 0.1f};
        color_accent_1 = {0.231f, 0.310f, 1.000f, 1.0f};
        color_accent_2 = {0.176f, 0.314f, 1.000f, 1.0f};
        color_ok       = {0.200f, 0.702f, 0.349f, 1.0f};
        color_info     = {0.922f, 0.922f, 0.922f, 1.0f};
        color_warning  = {1.000f, 0.584f, 0.192f, 1.0f};
        color_error    = {1.000f, 0.227f, 0.227f, 1.0f};
    }

    inline void SetupImGuiBase()
    {
        ImGuiStyle& style               = ImGui::GetStyle();
        style.Alpha                     = 1.0f;
        style.DisabledAlpha             = 0.6f;
        style.WindowPadding             = ImVec2(8.0f, 4.0f);
        style.FramePadding              = ImVec2(8.0f, 4.0f);
        style.CellPadding               = ImVec2(8.0f, 4.0f);
        style.ItemSpacing               = ImVec2(8.0f, 4.0f);
        style.ItemInnerSpacing          = ImVec2(2.0f, 2.0f);
        style.IndentSpacing             = 21.0f;
        style.ColumnsMinSpacing         = 6.0f;
        style.ScrollbarSize             = 13.0f;
        style.GrabMinSize               = 7.0f;
        style.WindowBorderSize          = 1.0f;
        style.ChildBorderSize           = 0.0f;
        style.PopupBorderSize           = 1.0f;
        style.FrameBorderSize           = 0.0f;
        style.TabBorderSize             = 0.0f;
        style.TabBarBorderSize          = 1.0f;
        style.TabBarOverlineSize        = 2.0f;
        style.WindowRounding            = 2.0f;
        style.ChildRounding             = 2.0f;
        style.FrameRounding             = 2.0f;
        style.PopupRounding             = 2.0f;
        style.ScrollbarRounding         = 2.0f;
        style.GrabRounding              = 2.0f;
        style.TabRounding               = 2.0f;
        style.WindowMinSize             = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign          = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition  = ImGuiDir_Left;
        style.ColorButtonPosition       = ImGuiDir_Right;
        style.ButtonTextAlign           = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign       = ImVec2(0.0f, 0.0f);
        style.TabMinWidthForCloseButton = 0.0f;
    }

    inline void SetupImGuiColors()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        // derive shades from base colors
        ImVec4 content       = bg_color_1;
        ImVec4 content_deep  = lerp(bg_color_1, {0, 0, 0, 1}, 0.2f);
        ImVec4 frame         = bg_color_2;
        ImVec4 frame_hover   = lerp(bg_color_2, {1, 1, 1, 1}, 0.08f);
        ImVec4 frame_active  = lerp(bg_color_2, {1, 1, 1, 1}, 0.15f);
        ImVec4 border        = lerp(bg_color_1, bg_color_2, 0.3f);
        ImVec4 text          = h_color_1;
        ImVec4 text_dim      = lerp(h_color_1, bg_color_2, 0.5f);

        // text
        style.Colors[ImGuiCol_Text]         = text;
        style.Colors[ImGuiCol_TextDisabled] = text_dim;

        // backgrounds
        style.Colors[ImGuiCol_WindowBg] = content;
        style.Colors[ImGuiCol_ChildBg]  = {0, 0, 0, 0};
        style.Colors[ImGuiCol_PopupBg]  = content;

        // title bar and tabs
        style.Colors[ImGuiCol_TitleBg]               = frame;
        style.Colors[ImGuiCol_TitleBgActive]         = frame;
        style.Colors[ImGuiCol_TitleBgCollapsed]      = frame;
        style.Colors[ImGuiCol_MenuBarBg]             = frame;
        style.Colors[ImGuiCol_Tab]                   = frame;
        style.Colors[ImGuiCol_TabDimmed]             = frame;
        style.Colors[ImGuiCol_TabHovered]            = frame_active;

        // selected tabs are slightly lighter than content so the tab bar separator line is visible
        // this creates a visual connection between tabs while distinguishing them from buttons
        ImVec4 tab_selected = lerp(content, frame, 0.2f);
        style.Colors[ImGuiCol_TabSelected]           = tab_selected;
        style.Colors[ImGuiCol_TabDimmedSelected]     = tab_selected;
        style.Colors[ImGuiCol_TabSelectedOverline]   = color_accent_1;
        style.Colors[ImGuiCol_TabDimmedSelectedOverline] = color_accent_1;

        // input frames
        style.Colors[ImGuiCol_FrameBg]        = content_deep;
        style.Colors[ImGuiCol_FrameBgHovered] = lerp(content_deep, frame, 0.3f);
        style.Colors[ImGuiCol_FrameBgActive]  = lerp(content_deep, frame, 0.5f);

        // buttons
        style.Colors[ImGuiCol_Button]        = frame_hover;
        style.Colors[ImGuiCol_ButtonHovered] = frame_active;
        style.Colors[ImGuiCol_ButtonActive]  = content_deep;

        // scrollbar
        style.Colors[ImGuiCol_ScrollbarBg]          = content;
        style.Colors[ImGuiCol_ScrollbarGrab]        = frame;
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = frame_hover;
        style.Colors[ImGuiCol_ScrollbarGrabActive]  = color_accent_1;

        // slider
        style.Colors[ImGuiCol_SliderGrab]       = frame_hover;
        style.Colors[ImGuiCol_SliderGrabActive] = color_accent_1;

        // headers
        style.Colors[ImGuiCol_Header]        = frame;
        style.Colors[ImGuiCol_HeaderHovered] = frame_hover;
        style.Colors[ImGuiCol_HeaderActive]  = frame_active;

        // separators and borders
        style.Colors[ImGuiCol_Separator]        = border;
        style.Colors[ImGuiCol_SeparatorHovered] = frame_hover;
        style.Colors[ImGuiCol_SeparatorActive]  = color_accent_1;
        style.Colors[ImGuiCol_Border]           = border;
        style.Colors[ImGuiCol_BorderShadow]     = {0, 0, 0, 0};

        // resize grip
        style.Colors[ImGuiCol_ResizeGrip]        = {0, 0, 0, 0.3f};
        style.Colors[ImGuiCol_ResizeGripHovered] = frame_hover;
        style.Colors[ImGuiCol_ResizeGripActive]  = color_accent_1;

        // tables
        style.Colors[ImGuiCol_TableHeaderBg]     = frame;
        style.Colors[ImGuiCol_TableBorderStrong] = border;
        style.Colors[ImGuiCol_TableBorderLight]  = lerp(border, content, 0.5f);
        style.Colors[ImGuiCol_TableRowBg]        = {0, 0, 0, 0};
        style.Colors[ImGuiCol_TableRowBgAlt]     = {0, 0, 0, 0.1f};

        // accent highlights
        style.Colors[ImGuiCol_CheckMark]              = color_accent_1;
        style.Colors[ImGuiCol_DragDropTarget]         = color_accent_1;
        style.Colors[ImGuiCol_NavCursor]              = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingHighlight]  = color_accent_1;
        style.Colors[ImGuiCol_DockingPreview]         = color_accent_1;

        // misc
        style.Colors[ImGuiCol_TextSelectedBg]       = lerp(color_accent_1, {0, 0, 0, 1}, 0.6f);
        style.Colors[ImGuiCol_PlotLines]            = frame_active;
        style.Colors[ImGuiCol_PlotLinesHovered]     = color_accent_1;
        style.Colors[ImGuiCol_PlotHistogram]        = frame_active;
        style.Colors[ImGuiCol_PlotHistogramHovered] = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingDimBg]    = {0, 0, 0, 0.6f};
        style.Colors[ImGuiCol_ModalWindowDimBg]     = {0, 0, 0, 0.6f};
    }
}
