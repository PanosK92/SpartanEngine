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
    inline ImVec4 bg_color_1     = {0.082f, 0.090f, 0.102f, 1.0f};
    inline ImVec4 bg_color_2     = {0.137f, 0.153f, 0.176f, 1.0f};
    inline ImVec4 h_color_1      = {0.945f, 0.953f, 0.961f, 1.0f};
    inline ImVec4 h_color_2      = {0.588f, 0.627f, 0.678f, 1.0f};
    inline ImVec4 color_accent_1 = {0.208f, 0.725f, 0.914f, 1.0f};
    inline ImVec4 color_accent_2 = {0.129f, 0.494f, 0.667f, 1.0f};

    inline ImVec4 color_ok      = {0.353f, 0.769f, 0.514f, 1.0f};
    inline ImVec4 color_info    = {0.588f, 0.753f, 0.933f, 1.0f};
    inline ImVec4 color_warning = {0.941f, 0.678f, 0.306f, 1.0f};
    inline ImVec4 color_error   = {0.925f, 0.361f, 0.373f, 1.0f};

    inline ImVec4 color_canvas;
    inline ImVec4 color_canvas_deep;
    inline ImVec4 color_panel;
    inline ImVec4 color_surface;
    inline ImVec4 color_surface_hover;
    inline ImVec4 color_surface_active;
    inline ImVec4 color_border;
    inline ImVec4 color_border_strong;
    inline ImVec4 color_text;
    inline ImVec4 color_text_muted;

    inline ImVec4 lerp(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t
        );
    }

    inline ImVec4 with_alpha(ImVec4 color, const float alpha)
    {
        color.w = alpha;
        return color;
    }

    inline void SetupLayout()
    {
        ImGuiStyle& style          = ImGui::GetStyle();
        style.WindowPadding        = ImVec2(8.0f, 6.0f);
        style.FramePadding         = ImVec2(8.0f, 5.0f);
        style.CellPadding          = ImVec2(8.0f, 4.0f);
        style.ItemSpacing          = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing     = ImVec2(6.0f, 4.0f);
        style.IndentSpacing        = 20.0f;
        style.ColumnsMinSpacing    = 8.0f;
        style.ScrollbarSize        = 12.0f;
        style.GrabMinSize          = 9.0f;
        style.WindowBorderSize     = 1.0f;
        style.ChildBorderSize      = 1.0f;
        style.PopupBorderSize      = 1.0f;
        style.FrameBorderSize      = 1.0f;
        style.TabBorderSize        = 0.0f;
        style.TabBarBorderSize     = 1.0f;
        style.TabBarOverlineSize   = 2.0f;
        style.WindowRounding       = 6.0f;
        style.ChildRounding        = 6.0f;
        style.FrameRounding        = 4.0f;
        style.PopupRounding        = 8.0f;
        style.ScrollbarRounding    = 6.0f;
        style.GrabRounding         = 4.0f;
        style.TabRounding          = 5.0f;
        style.TabMinWidthBase      = 72.0f;
        style.TabMinWidthShrink    = 48.0f;
        style.WindowMinSize        = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign     = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Left;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign      = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign  = ImVec2(0.0f, 0.5f);
        style.TabCloseButtonMinWidthUnselected = 0.0f;
    }

    inline void StyleSpartan()
    {
        bg_color_1     = {0.082f, 0.090f, 0.102f, 1.0f};
        bg_color_2     = {0.137f, 0.153f, 0.176f, 1.0f};
        h_color_1      = {0.945f, 0.953f, 0.961f, 1.0f};
        h_color_2      = {0.588f, 0.627f, 0.678f, 1.0f};
        color_accent_1 = {0.208f, 0.725f, 0.914f, 1.0f};
        color_accent_2 = {0.129f, 0.494f, 0.667f, 1.0f};
        color_ok       = {0.353f, 0.769f, 0.514f, 1.0f};
        color_info     = {0.588f, 0.753f, 0.933f, 1.0f};
        color_warning  = {0.941f, 0.678f, 0.306f, 1.0f};
        color_error    = {0.925f, 0.361f, 0.373f, 1.0f};
        SetupLayout();
    }

    inline void StyleDark()
    {
        bg_color_1     = {0.10f, 0.10f, 0.10f, 1.0f};
        bg_color_2     = {0.59f, 0.59f, 0.59f, 1.0f};
        h_color_1      = {1.00f, 1.00f, 1.00f, 1.0f};
        h_color_2      = {0.62f, 0.62f, 0.62f, 1.0f};
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
        h_color_2      = {0.32f, 0.32f, 0.32f, 1.0f};
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
        SetupLayout();
    }

    inline void UpdateSemanticColors()
    {
        color_canvas         = bg_color_1;
        color_canvas_deep    = lerp(bg_color_1, {0, 0, 0, 1}, 0.24f);
        color_panel          = lerp(bg_color_1, bg_color_2, 0.42f);
        color_surface        = bg_color_2;
        color_surface_hover  = lerp(bg_color_2, {1, 1, 1, 1}, 0.08f);
        color_surface_active = lerp(bg_color_2, color_accent_2, 0.28f);
        color_border         = lerp(bg_color_1, bg_color_2, 0.64f);
        color_border_strong  = lerp(bg_color_2, h_color_2, 0.24f);
        color_text           = h_color_1;
        color_text_muted     = h_color_2;
    }

    inline void SyncSemanticColorsFromImGui()
    {
        const ImGuiStyle& style = ImGui::GetStyle();
        bg_color_1 = style.Colors[ImGuiCol_WindowBg];
        bg_color_2 = style.Colors[ImGuiCol_Button];
        h_color_1 = style.Colors[ImGuiCol_Text];
        h_color_2 = style.Colors[ImGuiCol_TextDisabled];
        color_accent_1 = style.Colors[ImGuiCol_CheckMark];
        color_accent_2 = style.Colors[ImGuiCol_SliderGrabActive];
        UpdateSemanticColors();
    }

    inline void SetupImGuiColors()
    {
        ImGuiStyle& style = ImGui::GetStyle();
        UpdateSemanticColors();

        style.Colors[ImGuiCol_Text]         = color_text;
        style.Colors[ImGuiCol_TextDisabled] = color_text_muted;

        style.Colors[ImGuiCol_WindowBg] = color_canvas;
        style.Colors[ImGuiCol_ChildBg]  = {0, 0, 0, 0};
        style.Colors[ImGuiCol_PopupBg]  = color_panel;

        style.Colors[ImGuiCol_TitleBg]               = color_panel;
        style.Colors[ImGuiCol_TitleBgActive]         = color_panel;
        style.Colors[ImGuiCol_TitleBgCollapsed]      = color_panel;
        style.Colors[ImGuiCol_MenuBarBg]             = lerp(bg_color_1, bg_color_2, 0.35f);
        style.Colors[ImGuiCol_Tab]                   = color_panel;
        style.Colors[ImGuiCol_TabDimmed]             = color_panel;
        style.Colors[ImGuiCol_TabHovered]            = color_surface_hover;

        ImVec4 tab_selected = lerp(color_canvas, color_surface, 0.38f);
        style.Colors[ImGuiCol_TabSelected]           = tab_selected;
        style.Colors[ImGuiCol_TabDimmedSelected]     = tab_selected;
        style.Colors[ImGuiCol_TabSelectedOverline]   = color_accent_1;
        style.Colors[ImGuiCol_TabDimmedSelectedOverline] = color_accent_1;

        style.Colors[ImGuiCol_FrameBg]        = color_canvas_deep;
        style.Colors[ImGuiCol_FrameBgHovered] = lerp(
            color_surface_hover,
            color_accent_2,
            0.18f
        );
        style.Colors[ImGuiCol_FrameBgActive]  = lerp(
            color_surface,
            color_accent_2,
            0.42f
        );

        style.Colors[ImGuiCol_Button]        = color_surface;
        style.Colors[ImGuiCol_ButtonHovered] = lerp(
            color_surface_hover,
            color_accent_2,
            0.12f
        );
        style.Colors[ImGuiCol_ButtonActive]  = lerp(
            color_surface,
            color_accent_2,
            0.34f
        );

        style.Colors[ImGuiCol_ScrollbarBg]          = color_canvas;
        style.Colors[ImGuiCol_ScrollbarGrab]        = color_surface;
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = lerp(
            color_surface_hover,
            color_accent_2,
            0.20f
        );
        style.Colors[ImGuiCol_ScrollbarGrabActive]  = color_accent_1;
        style.Colors[ImGuiCol_CheckboxSelectedBg]   = with_alpha(
            color_accent_2,
            0.46f
        );

        style.Colors[ImGuiCol_SliderGrab]       = color_surface_hover;
        style.Colors[ImGuiCol_SliderGrabActive] = color_accent_1;

        style.Colors[ImGuiCol_Header]        = color_surface;
        style.Colors[ImGuiCol_HeaderHovered] = lerp(
            color_surface_hover,
            color_accent_2,
            0.16f
        );
        style.Colors[ImGuiCol_HeaderActive]  = lerp(
            color_surface,
            color_accent_2,
            0.30f
        );

        style.Colors[ImGuiCol_Separator]        = color_border;
        style.Colors[ImGuiCol_SeparatorHovered] = color_border_strong;
        style.Colors[ImGuiCol_SeparatorActive]  = color_accent_1;
        style.Colors[ImGuiCol_Border]           = color_border;
        style.Colors[ImGuiCol_BorderShadow]     = {0, 0, 0, 0};

        style.Colors[ImGuiCol_ResizeGrip]        = {0, 0, 0, 0.3f};
        style.Colors[ImGuiCol_ResizeGripHovered] = color_surface_hover;
        style.Colors[ImGuiCol_ResizeGripActive]  = color_accent_1;

        style.Colors[ImGuiCol_TableHeaderBg]     = color_panel;
        style.Colors[ImGuiCol_TableBorderStrong] = color_border;
        style.Colors[ImGuiCol_TableBorderLight]  = lerp(color_border, color_canvas, 0.5f);
        style.Colors[ImGuiCol_TableRowBg]        = {0, 0, 0, 0};
        style.Colors[ImGuiCol_TableRowBgAlt]     = lerp(color_canvas, color_panel, 0.26f);

        // accent highlights
        style.Colors[ImGuiCol_CheckMark]              = color_accent_1;
        style.Colors[ImGuiCol_InputTextCursor]        = color_accent_1;
        style.Colors[ImGuiCol_TextLink]               = color_accent_1;
        style.Colors[ImGuiCol_TreeLines]              = color_border_strong;
        style.Colors[ImGuiCol_DragDropTarget]         = color_accent_1;
        style.Colors[ImGuiCol_DragDropTargetBg]       = with_alpha(
            color_accent_2,
            0.14f
        );
        style.Colors[ImGuiCol_UnsavedMarker]          = color_warning;
        style.Colors[ImGuiCol_NavCursor]              = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingHighlight]  = color_accent_1;
        style.Colors[ImGuiCol_DockingPreview]         = color_accent_1;
        style.Colors[ImGuiCol_DockingEmptyBg]         = color_canvas;

        style.Colors[ImGuiCol_TextSelectedBg]       = lerp(color_accent_1, {0, 0, 0, 1}, 0.6f);
        style.Colors[ImGuiCol_PlotLines]            = color_surface_active;
        style.Colors[ImGuiCol_PlotLinesHovered]     = color_accent_1;
        style.Colors[ImGuiCol_PlotHistogram]        = color_surface_active;
        style.Colors[ImGuiCol_PlotHistogramHovered] = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingDimBg]    = {0, 0, 0, 0.6f};
        style.Colors[ImGuiCol_ModalWindowDimBg]     = {0, 0, 0, 0.6f};
    }
}
