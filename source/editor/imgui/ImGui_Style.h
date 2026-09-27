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
#include "source/imgui.h"
//=======================

namespace ImGui::Style
{
    // command deck: graphite panels set in black, crisp white readouts and one electric signal
    inline ImVec4 bg_color_1     = {0.071f, 0.075f, 0.086f, 1.0f};
    inline ImVec4 bg_color_2     = {0.125f, 0.133f, 0.153f, 1.0f};
    inline ImVec4 h_color_1      = {0.925f, 0.945f, 0.965f, 1.0f};
    inline ImVec4 h_color_2      = {0.580f, 0.615f, 0.660f, 1.0f};
    inline ImVec4 color_accent_1 = {0.161f, 0.827f, 1.000f, 1.0f};
    inline ImVec4 color_accent_2 = {0.039f, 0.431f, 0.588f, 1.0f};

    inline ImVec4 color_ok      = {0.357f, 0.890f, 0.639f, 1.0f};
    inline ImVec4 color_info    = {0.604f, 0.776f, 0.937f, 1.0f};
    inline ImVec4 color_warning = {1.000f, 0.757f, 0.353f, 1.0f};
    inline ImVec4 color_error   = {1.000f, 0.416f, 0.416f, 1.0f};

    inline ImVec4 color_canvas;
    inline ImVec4 color_canvas_deep;
    inline ImVec4 color_void;
    inline ImVec4 color_panel;
    inline ImVec4 color_surface;
    inline ImVec4 color_surface_hover;
    inline ImVec4 color_surface_active;
    inline ImVec4 color_border;
    inline ImVec4 color_border_strong;
    inline ImVec4 color_text;
    inline ImVec4 color_text_muted;
    inline ImVec4 color_text_faint;
    inline ImVec4 color_accent_hi;
    inline ImVec4 color_accent_dim;
    inline ImVec4 color_accent_line;

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
        style.WindowPadding        = ImVec2(12.0f, 10.0f);
        style.FramePadding         = ImVec2(8.0f, 5.0f);
        style.CellPadding          = ImVec2(8.0f, 4.0f);
        style.ItemSpacing          = ImVec2(8.0f, 6.0f);
        style.ItemInnerSpacing     = ImVec2(6.0f, 4.0f);
        style.IndentSpacing        = 16.0f;
        style.ColumnsMinSpacing    = 8.0f;
        style.ScrollbarSize        = 8.0f;
        style.GrabMinSize          = 8.0f;
        // no strokes, structure comes from value alone: black gutters, a darker tab strip, panels, darker wells inside them
        style.WindowBorderSize     = 0.0f;
        style.ChildBorderSize      = 0.0f;
        style.PopupBorderSize      = 0.0f;
        style.FrameBorderSize      = 0.0f;
        style.TabBorderSize        = 0.0f;
        style.TabBarBorderSize     = 0.0f;
        style.TabBarOverlineSize   = 2.0f;
        style.WindowRounding       = 4.0f;
        style.ChildRounding        = 4.0f;
        style.FrameRounding        = 3.0f;
        style.PopupRounding        = 5.0f;
        style.ScrollbarRounding    = 4.0f;
        style.GrabRounding         = 3.0f;
        style.TabRounding          = 4.0f;
        style.TabMinWidthBase      = 88.0f;
        style.TabMinWidthShrink    = 48.0f;
        style.WindowMinSize        = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign     = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_None;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign      = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign  = ImVec2(0.0f, 0.5f);
        style.TabCloseButtonMinWidthUnselected = 0.0f;
        style.TreeLinesFlags       = ImGuiTreeNodeFlags_DrawLinesNone;
        style.TreeLinesSize        = 1.0f;
        style.SeparatorTextBorderSize = 0.0f;
        style.SeparatorTextPadding = ImVec2(0.0f, 6.0f);
        // a thin black gutter between docked panels is the only separation between them
        style.DockingSeparatorSize = 2.0f;
    }

    inline void StyleSpartan()
    {
        bg_color_1     = {0.071f, 0.075f, 0.086f, 1.0f};
        bg_color_2     = {0.125f, 0.133f, 0.153f, 1.0f};
        h_color_1      = {0.925f, 0.945f, 0.965f, 1.0f};
        h_color_2      = {0.580f, 0.615f, 0.660f, 1.0f};
        color_accent_1 = {0.161f, 0.827f, 1.000f, 1.0f};
        color_accent_2 = {0.039f, 0.431f, 0.588f, 1.0f};
        color_ok       = {0.357f, 0.890f, 0.639f, 1.0f};
        color_info     = {0.604f, 0.776f, 0.937f, 1.0f};
        color_warning  = {1.000f, 0.757f, 0.353f, 1.0f};
        color_error    = {1.000f, 0.416f, 0.416f, 1.0f};
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
        // lines are the text color at low opacity, so every preset keeps hairlines that sit in its own temperature
        color_canvas         = bg_color_1;
        color_canvas_deep    = lerp(bg_color_1, {0, 0, 0, 1}, 0.40f);
        color_void           = lerp(bg_color_1, {0, 0, 0, 1}, 0.90f);
        color_panel          = lerp(bg_color_1, bg_color_2, 0.45f);
        color_surface        = bg_color_2;
        color_surface_hover  = lerp(bg_color_2, h_color_1, 0.07f);
        color_surface_active = lerp(bg_color_2, color_accent_2, 0.35f);
        color_border         = lerp(bg_color_1, h_color_1, 0.075f);
        color_border_strong  = lerp(bg_color_1, h_color_1, 0.16f);
        color_text           = h_color_1;
        color_text_muted     = h_color_2;
        color_text_faint     = lerp(bg_color_1, h_color_2, 0.62f);
        color_accent_hi      = lerp(color_accent_1, {1, 1, 1, 1}, 0.45f);
        color_accent_dim     = with_alpha(color_accent_1, 0.13f);
        color_accent_line    = with_alpha(color_accent_1, 0.32f);
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

        // three depths: the void behind everything, the canvas of a panel and the wells values sit in
        style.Colors[ImGuiCol_WindowBg] = color_canvas;
        style.Colors[ImGuiCol_ChildBg]  = {0, 0, 0, 0};
        style.Colors[ImGuiCol_PopupBg]  = with_alpha(lerp(color_canvas, color_surface, 0.35f), 0.97f);

        // the tab strip is darker than the panel, the selected tab is the panel itself rising into it
        style.Colors[ImGuiCol_TitleBg]          = color_canvas_deep;
        style.Colors[ImGuiCol_TitleBgActive]    = color_canvas_deep;
        style.Colors[ImGuiCol_TitleBgCollapsed] = color_canvas_deep;
        style.Colors[ImGuiCol_MenuBarBg]        = lerp(color_canvas_deep, color_canvas, 0.55f);

        style.Colors[ImGuiCol_Tab]                       = {0, 0, 0, 0};
        style.Colors[ImGuiCol_TabDimmed]                 = {0, 0, 0, 0};
        style.Colors[ImGuiCol_TabHovered]                = lerp(color_canvas_deep, color_canvas, 0.6f);
        style.Colors[ImGuiCol_TabSelected]               = color_canvas;
        style.Colors[ImGuiCol_TabDimmedSelected]         = color_canvas;
        style.Colors[ImGuiCol_TabSelectedOverline]       = color_accent_1;
        style.Colors[ImGuiCol_TabDimmedSelectedOverline] = {0, 0, 0, 0};

        // wells sit below the panel, buttons sit above it
        style.Colors[ImGuiCol_FrameBg]        = color_canvas_deep;
        style.Colors[ImGuiCol_FrameBgHovered] = lerp(color_canvas_deep, color_surface, 0.35f);
        style.Colors[ImGuiCol_FrameBgActive]  = lerp(color_canvas_deep, color_accent_2, 0.25f);

        style.Colors[ImGuiCol_Button]        = color_surface;
        style.Colors[ImGuiCol_ButtonHovered] = color_surface_hover;
        style.Colors[ImGuiCol_ButtonActive]  = with_alpha(color_accent_1, 0.30f);

        style.Colors[ImGuiCol_ScrollbarBg]          = {0, 0, 0, 0};
        style.Colors[ImGuiCol_ScrollbarGrab]        = with_alpha(color_text, 0.10f);
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = with_alpha(color_text, 0.22f);
        style.Colors[ImGuiCol_ScrollbarGrabActive]  = with_alpha(color_accent_1, 0.70f);
        style.Colors[ImGuiCol_CheckboxSelectedBg]   = with_alpha(color_accent_1, 0.18f);

        style.Colors[ImGuiCol_SliderGrab]       = color_accent_2;
        style.Colors[ImGuiCol_SliderGrabActive] = color_accent_1;

        // selection is the only thing tinted with the signal color, hover stays neutral
        style.Colors[ImGuiCol_Header]        = color_accent_dim;
        style.Colors[ImGuiCol_HeaderHovered] = with_alpha(color_text, 0.05f);
        style.Colors[ImGuiCol_HeaderActive]  = with_alpha(color_accent_1, 0.22f);

        // separators only light up while they are being grabbed
        style.Colors[ImGuiCol_Separator]        = with_alpha(color_text, 0.035f);
        style.Colors[ImGuiCol_SeparatorHovered] = color_accent_line;
        style.Colors[ImGuiCol_SeparatorActive]  = color_accent_1;
        style.Colors[ImGuiCol_Border]           = with_alpha(color_text, 0.06f);
        style.Colors[ImGuiCol_BorderShadow]     = {0, 0, 0, 0};

        style.Colors[ImGuiCol_ResizeGrip]        = {0, 0, 0, 0};
        style.Colors[ImGuiCol_ResizeGripHovered] = color_accent_line;
        style.Colors[ImGuiCol_ResizeGripActive]  = color_accent_1;

        style.Colors[ImGuiCol_TableHeaderBg]     = with_alpha(color_text, 0.04f);
        style.Colors[ImGuiCol_TableBorderStrong] = {0, 0, 0, 0};
        style.Colors[ImGuiCol_TableBorderLight]  = {0, 0, 0, 0};
        style.Colors[ImGuiCol_TableRowBg]        = {0, 0, 0, 0};
        style.Colors[ImGuiCol_TableRowBgAlt]     = with_alpha(color_text, 0.02f);

        // accent highlights
        style.Colors[ImGuiCol_CheckMark]              = color_accent_1;
        style.Colors[ImGuiCol_InputTextCursor]        = color_accent_1;
        style.Colors[ImGuiCol_TextLink]               = color_accent_1;
        style.Colors[ImGuiCol_TreeLines]              = lerp(color_border, color_border_strong, 0.35f);
        style.Colors[ImGuiCol_DragDropTarget]         = color_accent_1;
        style.Colors[ImGuiCol_DragDropTargetBg]       = color_accent_dim;
        style.Colors[ImGuiCol_UnsavedMarker]          = color_warning;
        style.Colors[ImGuiCol_NavCursor]              = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingHighlight]  = color_accent_1;
        style.Colors[ImGuiCol_DockingPreview]         = with_alpha(color_accent_1, 0.24f);
        style.Colors[ImGuiCol_DockingEmptyBg]         = color_void;

        style.Colors[ImGuiCol_TextSelectedBg]       = with_alpha(color_accent_1, 0.28f);
        style.Colors[ImGuiCol_PlotLines]            = color_accent_2;
        style.Colors[ImGuiCol_PlotLinesHovered]     = color_accent_1;
        style.Colors[ImGuiCol_PlotHistogram]        = color_accent_2;
        style.Colors[ImGuiCol_PlotHistogramHovered] = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingDimBg]    = with_alpha(color_void, 0.72f);
        style.Colors[ImGuiCol_ModalWindowDimBg]     = with_alpha(color_void, 0.72f);
    }
}
