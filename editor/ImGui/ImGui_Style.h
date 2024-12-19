/*
Copyright(c) 2016-2024 Panos Karabelas

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

    // TODO
    // Console Widget:    warning buttons are not vertically aligned
    // Console Widget:    filter text label missing left padding
    // FileDialog Widget: thumbnail text label background is incorrect size

    static ImVec4 bg_color_1 = {0.1f,0.1f,0.1f,1.0f};
    static ImVec4 bg_color_2 = {0.59f,0.59f,0.59f,1.0f};

    static ImVec4 h_color_1  = {1.0f,1.0f,1.0f,1.0f};
    static ImVec4 h_color_2  = {1.0f,1.0f,1.0f,0.1f};

    static ImVec4 color_accent_1 = {59.0f / 255.0f, 79.0f / 255.0f, 255.0f / 255.0f, 1.0f};
    static ImVec4 color_accent_2 = {45.0f / 255.0f, 80.0f / 255.0f, 255.0f / 255.0f, 1.0f};

    static ImVec4 color_ok      = {51.0f / 255.0f, 179.0f / 255.0f, 89.0f / 255.0f, 1.0f};
    static ImVec4 color_info    = {235.0f / 255.0f, 235.0f / 255.0f, 235.0f / 255.0f, 1.0f};
    static ImVec4 color_warning = {255.0f / 255.0f, 149.0f / 255.0f, 49.0f / 255.0f, 1.0f};
    static ImVec4 color_error   = {255.0f / 255.0f, 58.0f / 255.0f, 58.0f / 255.0f, 1.0f};

    inline ImVec4 HSV(float h, float s, float v, float a = 1.0f)
    {
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
        return {r, g, b, a};
    }

    inline ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(a.x + (b.x - a.x) * t,
                      a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t,
                      a.w + (b.w - a.w) * t);
    }

    inline void StyleSpartan()
    {
        bg_color_1 = {30.0f / 255.0f, 30.0f / 255.0f, 41.0f / 255.0f, 1.0f};
        bg_color_2 = {71.0f / 255.0f, 85.0f / 255.0f, 117.0f / 255.0f, 1.0f};

        h_color_1  = {1.0,1.0,1.0,1.0f};
        h_color_2  = {1.0,1.0,1.0,0.1f};

        color_accent_1 = {181.0f / 255.0f, 198.0f / 255.0f, 238.0f / 255.0f, 1.0f};
        color_accent_2 = {79.0f / 255.0f, 82.0f / 255.0f, 99.0f / 255.0f, 1.0f};

        color_ok        = {51.0f / 255.0f, 179.0f / 255.0f, 89.0f / 255.0f, 1.0f};
        color_info      = {235.0f / 255.0f, 235.0f / 255.0f, 235.0f / 255.0f, 1.0f};
        color_warning   = {255.0f / 255.0f, 149.0f / 255.0f, 49.0f / 255.0f, 1.0f};
        color_error     = {255.0f / 255.0f, 58.0f / 255.0f, 58.0f / 255.0f, 1.0f};
    }

    inline void StyleDark()
    {
        bg_color_1 = {0.1f,0.1f,0.1f,1.0f};
        bg_color_2 = {0.59f,0.59f,0.59f,1.0f};

        h_color_1  = {1.0f,1.0f,1.0f,1.0f};
        h_color_2  = {1.0f,1.0f,1.0f,0.1f};

        color_accent_1 = {59.0f / 255.0f, 79.0f / 255.0f, 255.0f / 255.0f, 1.0f};
        color_accent_2 = {45.0f / 255.0f, 80.0f / 255.0f, 255.0f / 255.0f, 1.0f};

        color_ok        = {51.0f / 255.0f, 179.0f / 255.0f, 89.0f / 255.0f, 1.0f};
        color_info      = {235.0f / 255.0f, 235.0f / 255.0f, 235.0f / 255.0f, 1.0f};
        color_warning   = {255.0f / 255.0f, 149.0f / 255.0f, 49.0f / 255.0f, 1.0f};
        color_error     = {255.0f / 255.0f, 58.0f / 255.0f, 58.0f / 255.0f, 1.0f};
    }

    inline void StyleLight()
    {
        bg_color_1 = {219.0f / 255.0f, 219.0f / 255.0f, 219.0f / 255.0f, 1.0f};
        bg_color_2 = {70.0f / 255.0f, 70.0f / 255.0f, 70.0f / 255.0f, 1.0f};

        h_color_1 = {7.0f / 255.0f, 7.0f / 255.0f, 7.0f / 255.0f, 1.0f};
        h_color_2  = {0.0f,0.0f,0.0f,0.1f};

        color_accent_1 = {59.0f / 255.0f, 79.0f / 255.0f, 255.0f / 255.0f, 1.0f};
        color_accent_2 = {45.0f / 255.0f, 80.0f / 255.0f, 255.0f / 255.0f, 1.0f};

        color_ok        = {51.0f / 255.0f, 179.0f / 255.0f, 89.0f / 255.0f, 1.0f};
        color_info      = {235.0f / 255.0f, 235.0f / 255.0f, 235.0f / 255.0f, 1.0f};
        color_warning   = {255.0f / 255.0f, 149.0f / 255.0f, 49.0f / 255.0f, 1.0f};
        color_error     = {255.0f / 255.0f, 58.0f / 255.0f, 58.0f / 255.0f, 1.0f};
    }

    inline const void SetupImGuiBase()
    {
        ImGuiStyle& style = ImGui::GetStyle();

        style.Alpha                     = 1.0f;
        style.DisabledAlpha             = 0.60f;

        style.WindowPadding             = ImVec2(8.0f, 4.0f);
        style.CellPadding               = ImVec2(8.0f, 4.0f);
        style.FramePadding              = ImVec2(8.0f, 4.0f);
        style.ItemSpacing               = ImVec2(8.0f, 4.0f);

        style.WindowRounding            = 2.0f;
        style.GrabRounding              = 2.0f;
        style.TabRounding               = 2.0f;
        style.ChildRounding             = 2.0f;
        style.PopupRounding             = 2.0f;
        style.FrameRounding             = 2.0f;
        style.ScrollbarRounding         = 2.0f;

        style.WindowBorderSize          = 1.0f;
        style.PopupBorderSize           = 1.0f;

        style.ChildBorderSize           = 0.0f;
        style.FrameBorderSize           = 0.0f;
        style.TabBorderSize             = 0.0f;

        style.WindowMinSize             = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign          = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition  = ImGuiDir_Left;

        style.ItemInnerSpacing          = ImVec2(2.0f, 2.0f);
        style.IndentSpacing             = 21.0f;
        style.ColumnsMinSpacing         = 6.0f;
        style.ScrollbarSize             = 13.0f;
        style.GrabMinSize               = 7.0f;
        style.TabMinWidthForCloseButton = 0.0f;
        style.ColorButtonPosition       = ImGuiDir_Right;
        style.ButtonTextAlign           = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign       = ImVec2(0.0f, 0.0f);
    }

    inline void SetupImGuiColors()
    {
        ImGuiStyle& style                                = ImGui::GetStyle();

        ImVec4 color_background_1                        = Lerp(bg_color_1, bg_color_2, .0f);
        ImVec4 color_background_2                        = Lerp(bg_color_1, bg_color_2, .1f);
        ImVec4 color_background_3                        = Lerp(bg_color_1, bg_color_2, .2f);
        ImVec4 color_background_4                        = Lerp(bg_color_1, bg_color_2, .3f);
        ImVec4 color_background_5                        = Lerp(bg_color_1, bg_color_2, .4f);
        ImVec4 color_background_6                        = Lerp(bg_color_1, bg_color_2, .5f);
        ImVec4 color_background_7                        = Lerp(bg_color_1, bg_color_2, .6f);
        ImVec4 color_background_8                        = Lerp(bg_color_1, bg_color_2, .7f);
        ImVec4 color_background_9                        = Lerp(bg_color_1, bg_color_2, .8f);
        ImVec4 color_background_10                       = Lerp(bg_color_1, bg_color_2, .9f);

        // should be dark
        ImVec4 color_black_transparent_9                 = {0.0f, 0.0f, 0.0f, 0.9f};
        ImVec4 color_black_transparent_6                 = {0.0f, 0.0f, 0.0f, 0.6f};
        ImVec4 color_black_transparent_3                 = {0.0f, 0.0f, 0.0f, 0.3f};
        ImVec4 color_black_transparent_1                 = {0.0f, 0.0f, 0.0f, 0.1f};

        ImVec4 color_highlight_1                         = Lerp(h_color_1,h_color_2, 0);

        ImVec4 color_accent_2                            = Lerp(h_color_1,h_color_2, 0.2f);//{55.0f / 255.0f, 75.0f / 255.0f, 255.0f / 255.0f, 1.0f};
        ImVec4 color_accent_3                            = Lerp(h_color_1,h_color_2, 0.3f);//{50.0f / 255.0f, 70.0f / 255.0f, 255.0f / 255.0f, 1.0f};

        // not used
        // ImVec4 color_highlight_2                      = Lerp(h_color_1,h_color_2,.1);
        // ImVec4 color_highlight_3                      = Lerp(h_color_1,h_color_2,.2);
        // ImVec4 color_highlight_4                      = Lerp(h_color_1,h_color_2,.3);
        // ImVec4 color_highlight_5                      = Lerp(h_color_1,h_color_2,.4);
        // ImVec4 color_highlight_6                      = Lerp(h_color_1,h_color_2,.5);
        // ImVec4 color_highlight_7                      = Lerp(h_color_1,h_color_2,.6);
        // ImVec4 color_highlight_8                      = Lerp(h_color_1,h_color_2,.7);
        // ImVec4 color_highlight_9                      = Lerp(h_color_1,h_color_2,.8);
        // ImVec4 color_highlight_10                     = Lerp(h_color_1,h_color_2,.9);

        style.Colors[ImGuiCol_Text]                      = color_highlight_1;
        style.Colors[ImGuiCol_TextDisabled]              = color_background_9;

        style.Colors[ImGuiCol_WindowBg]                  = color_background_2;
        style.Colors[ImGuiCol_FrameBg]                   = color_background_4;
        style.Colors[ImGuiCol_TitleBg]                   = color_background_1;
        style.Colors[ImGuiCol_TitleBgActive]             = color_background_2;

        // accent
        style.Colors[ImGuiCol_ScrollbarGrabActive]       = color_accent_1;
        style.Colors[ImGuiCol_SeparatorActive]           = color_accent_1;
        style.Colors[ImGuiCol_SliderGrabActive]          = color_accent_1;
        style.Colors[ImGuiCol_ResizeGripActive]          = color_accent_1;
        style.Colors[ImGuiCol_DragDropTarget]            = color_accent_1;
        style.Colors[ImGuiCol_NavCursor]                 = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingHighlight]     = color_accent_1;
        style.Colors[ImGuiCol_TabSelectedOverline]       = color_accent_1;
        style.Colors[ImGuiCol_TabDimmedSelectedOverline] = color_accent_1;
        style.Colors[ImGuiCol_CheckMark]                 = color_accent_1;

        style.Colors[ImGuiCol_Tab]                       = style.Colors[ImGuiCol_TitleBg];
        style.Colors[ImGuiCol_TabDimmed]                 = style.Colors[ImGuiCol_TitleBg];

        style.Colors[ImGuiCol_TabSelected]               = style.Colors[ImGuiCol_WindowBg];
        style.Colors[ImGuiCol_TabDimmedSelected]         = style.Colors[ImGuiCol_WindowBg];

        style.Colors[ImGuiCol_FrameBgHovered]            = color_background_3;

        style.Colors[ImGuiCol_TitleBgCollapsed]          = color_background_2;
        style.Colors[ImGuiCol_MenuBarBg]                 = color_background_3;
        style.Colors[ImGuiCol_ScrollbarBg]               = color_background_2;


        style.Colors[ImGuiCol_Button]                    = color_background_3;
        style.Colors[ImGuiCol_ButtonHovered]             = color_background_4;
        style.Colors[ImGuiCol_ButtonActive]              = color_background_1;

        // alternative
        // style.Colors[ImGuiCol_Button]                 = {};
        // style.Colors[ImGuiCol_ButtonHovered]          = color_highlight_4;
        // style.Colors[ImGuiCol_ButtonActive]           = color_highlight_5;

        style.Colors[ImGuiCol_ResizeGrip]                =  color_black_transparent_3;
        style.Colors[ImGuiCol_ResizeGripHovered]         = color_black_transparent_6;
        style.Colors[ImGuiCol_TableRowBgAlt]             = color_black_transparent_1;
        style.Colors[ImGuiCol_TextSelectedBg]            = color_black_transparent_1;

        style.Colors[ImGuiCol_DockingPreview]            = color_accent_1;
        style.Colors[ImGuiCol_PlotLinesHovered]          = color_accent_2;
        style.Colors[ImGuiCol_PlotHistogramHovered]      = color_accent_3;

        style.Colors[ImGuiCol_PlotHistogram]             = color_background_10;

        style.Colors[ImGuiCol_HeaderHovered]             = color_background_9;
        style.Colors[ImGuiCol_HeaderActive]              = color_background_9;
        style.Colors[ImGuiCol_PlotLines]                 = color_background_9;

        style.Colors[ImGuiCol_TabHovered]                = color_background_7;
        style.Colors[ImGuiCol_SeparatorHovered]          = color_background_8;
        style.Colors[ImGuiCol_SliderGrab]                = color_background_8;
        style.Colors[ImGuiCol_PopupBg]                   = color_background_6;
        style.Colors[ImGuiCol_Header]                    = color_background_6;
        style.Colors[ImGuiCol_TableBorderStrong]         = color_background_6;
        style.Colors[ImGuiCol_ScrollbarGrabHovered]      = color_background_6;
        style.Colors[ImGuiCol_Separator]                 = color_background_4;
        style.Colors[ImGuiCol_TableBorderLight]          = color_background_4;
        style.Colors[ImGuiCol_FrameBgActive]             = color_background_5;
        style.Colors[ImGuiCol_ScrollbarGrab]             = color_background_5;

        style.Colors[ImGuiCol_ChildBg]                   = {};
        style.Colors[ImGuiCol_Border]                    = color_background_5;

        style.Colors[ImGuiCol_TableHeaderBg]             = color_background_3;

        style.Colors[ImGuiCol_NavWindowingDimBg]         = color_black_transparent_6;
        style.Colors[ImGuiCol_ModalWindowDimBg]          = color_black_transparent_6;

        style.Colors[ImGuiCol_TableRowBg]                = {};
        style.Colors[ImGuiCol_BorderShadow]              = {};
    }
}
