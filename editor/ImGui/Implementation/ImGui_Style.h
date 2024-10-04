#pragma once

#include "../Source/imgui.h"
#include <cmath>

namespace ImGui::Style {

    // TODO
    // Console Widget: warning buttons are not vertically aligned
    // Console Widget: Filter text label missing left padding
    // FileDialog Widget:   Thumbnail text label background is incorrect size


    // default values
    static ImVec4 bg_1 = {0.1,0.1,0.1,1.0f};
    static ImVec4 bg_2 = {0.59,0.59,0.59,1.0f};

    static ImVec4 h_1  = {1.0,1.0,1.0,1.0f};
    static ImVec4 h_2  = {1.0,1.0,1.0,0.1f};

    // blue
    // static ImVec4 accent        = {255.0f / 255.0f, 59.0f / 255.0f, 59.0f / 255.0f, 1.0f};

    static ImVec4 color_accent_1 = {59.0f / 255.0f, 79.0f / 255.0f, 255.0f / 255.0f, 1.0f};
    static ImVec4 color_accent_2 = {45.0f / 255.0f, 80.0f / 255.0f, 255.0f / 255.0f, 1.0f};

    // should be more generic, green does not fit every theme
    static ImVec4 color_green        = { 0.2f, 0.7f, 0.35f, 1.0f };
    static ImVec4 color_green_hover  = { 0.22f, 0.8f, 0.4f, 1.0f };
    static ImVec4 color_green_active = { 0.1f, 0.4f, 0.2f, 1.0f };

    inline ImVec4 HSV(float h, float s, float v, float a = 1.0f){
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h, s, v, r, g, b);
        return {r, g, b, a};
    }

    inline ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float t) {
        return ImVec4(a.x + (b.x - a.x) * t,
                      a.y + (b.y - a.y) * t,
                      a.z + (b.z - a.z) * t,
                      a.w + (b.w - a.w) * t);
    }

    // needs polish to fix missing colors from PushStyleColor calls
    inline const void SetupImGuiStyleClassic()
    {
        // use default dark style as a base
        ImGui::StyleColorsDark();
        ImVec4* colors = ImGui::GetStyle().Colors;

        // color
        const ImVec4 k_palette_color_0 = { 10.0f / 255.0f, 12.0f / 255.0f, 17.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_1 = { 18.0f / 255.0f, 20.0f / 255.0f, 25.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_2 = { 22.0f / 255.0f, 30.0f / 255.0f, 45.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_3 = { 35.0f / 255.0f, 48.0f / 255.0f, 76.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_4 = { 65.0f / 255.0f, 90.0f / 255.0f, 119.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_5 = { 119.0f / 255.0f, 141.0f / 255.0f, 169.0f / 255.0f, 1.0f };
        const ImVec4 k_palette_color_6 = { 224.0f / 255.0f, 225.0f / 255.0f, 221.0f / 255.0f, 1.0f };

        colors[ImGuiCol_Text]                  = k_palette_color_6;
        colors[ImGuiCol_TextDisabled]          = k_palette_color_6;
        colors[ImGuiCol_WindowBg]              = k_palette_color_1;
        colors[ImGuiCol_ChildBg]               = k_palette_color_1;
        colors[ImGuiCol_PopupBg]               = k_palette_color_1;
        colors[ImGuiCol_Border]                = k_palette_color_3;
        colors[ImGuiCol_BorderShadow]          = k_palette_color_0;
        colors[ImGuiCol_FrameBg]               = k_palette_color_2; // Background of checkbox, radio button, plot, slider, text input
        colors[ImGuiCol_FrameBgHovered]        = k_palette_color_3;
        colors[ImGuiCol_FrameBgActive]         = k_palette_color_4;
        colors[ImGuiCol_TitleBg]               = k_palette_color_1;
        colors[ImGuiCol_TitleBgActive]         = k_palette_color_1;
        colors[ImGuiCol_TitleBgCollapsed]      = k_palette_color_1;
        colors[ImGuiCol_MenuBarBg]             = k_palette_color_0;
        colors[ImGuiCol_ScrollbarBg]           = k_palette_color_0;
        colors[ImGuiCol_ScrollbarGrab]         = k_palette_color_3;
        colors[ImGuiCol_ScrollbarGrabHovered]  = k_palette_color_4;
        colors[ImGuiCol_ScrollbarGrabActive]   = k_palette_color_2;
        colors[ImGuiCol_CheckMark]             = k_palette_color_6;
        colors[ImGuiCol_SliderGrab]            = k_palette_color_4;
        colors[ImGuiCol_SliderGrabActive]      = k_palette_color_3;
        colors[ImGuiCol_Button]                = k_palette_color_3;
        colors[ImGuiCol_ButtonHovered]         = k_palette_color_4;
        colors[ImGuiCol_ButtonActive]          = k_palette_color_2;
        colors[ImGuiCol_Header]                = k_palette_color_4;
        colors[ImGuiCol_HeaderHovered]         = k_palette_color_3;
        colors[ImGuiCol_HeaderActive]          = k_palette_color_0;
        colors[ImGuiCol_Separator]             = k_palette_color_5;
        colors[ImGuiCol_SeparatorHovered]      = k_palette_color_6;
        colors[ImGuiCol_SeparatorActive]       = k_palette_color_6;
        colors[ImGuiCol_ResizeGrip]            = k_palette_color_4;
        colors[ImGuiCol_ResizeGripHovered]     = k_palette_color_5;
        colors[ImGuiCol_ResizeGripActive]      = k_palette_color_3;
        colors[ImGuiCol_Tab]                   = k_palette_color_2;
        colors[ImGuiCol_TabHovered]            = k_palette_color_3;
        colors[ImGuiCol_TabSelected]           = k_palette_color_1;
        colors[ImGuiCol_TabDimmed]             = k_palette_color_2;
        colors[ImGuiCol_TabDimmedSelected]     = k_palette_color_2; // Might be called active, but it's active only because it's it's the only tab available, the user didn't really activate it
        colors[ImGuiCol_DockingPreview]        = k_palette_color_4; // Preview overlay color when about to docking something
        colors[ImGuiCol_DockingEmptyBg]        = k_palette_color_6; // Background color for empty node (e.g. CentralNode with no window docked into it)
        colors[ImGuiCol_PlotLines]             = k_palette_color_5;
        colors[ImGuiCol_PlotLinesHovered]      = k_palette_color_6;
        colors[ImGuiCol_PlotHistogram]         = k_palette_color_5;
        colors[ImGuiCol_PlotHistogramHovered]  = k_palette_color_6;
        colors[ImGuiCol_TextSelectedBg]        = k_palette_color_4;
        colors[ImGuiCol_DragDropTarget]        = k_palette_color_4; // Color when hovering over target
        colors[ImGuiCol_NavHighlight]          = k_palette_color_3; // Gamepad/keyboard: current highlighted item
        colors[ImGuiCol_NavWindowingHighlight] = k_palette_color_2; // Highlight window when using CTRL+TAB
        colors[ImGuiCol_NavWindowingDimBg]     = k_palette_color_2; // Darken/colorize entire screen behind the CTRL+TAB window list, when active
        colors[ImGuiCol_ModalWindowDimBg]      = k_palette_color_2;

        ImGuiStyle& style = ImGui::GetStyle();

        style.WindowPadding     = ImVec2(8.0f, 8.0f);
        style.FramePadding      = ImVec2(5.0f, 5.0f);
        style.CellPadding       = ImVec2(6.0f, 5.0f);
        style.ItemSpacing       = ImVec2(6.0f, 5.0f);
        style.ItemInnerSpacing  = ImVec2(6.0f, 6.0f);
        style.TouchExtraPadding = ImVec2(0.0f, 0.0f);
        style.IndentSpacing     = 25.0f;
        style.ScrollbarSize     = 13.0f;
        style.GrabMinSize       = 10.0f;
        style.WindowBorderSize  = 1.0f;
        style.ChildBorderSize   = 1.0f;
        style.PopupBorderSize   = 1.0f;
        style.FrameBorderSize   = 1.0f;
        style.TabBorderSize     = 1.0f;
        style.WindowRounding    = 2.0f;
        style.ChildRounding     = 3.0f;
        style.FrameRounding     = 0.0f;
        style.PopupRounding     = 3.0f;
        style.ScrollbarRounding = 9.0f;
        style.GrabRounding      = 3.0f;
        style.LogSliderDeadzone = 4.0f;
        style.TabRounding       = 3.0f;
        style.Alpha             = 1.0f;

        style.ScaleAllSizes(Spartan::Window::GetDpiScale());
    }

    inline const void SetupImGuiStyle() {
        // Fork of Comfy style from ImThemes
        ImGuiStyle& style = ImGui::GetStyle();

        style.Alpha = 1.0f;
        style.DisabledAlpha = 0.60f;

        style.WindowPadding = ImVec2(8.0f, 4.0f);
        style.CellPadding = ImVec2(8.0f, 4.0f);
        style.FramePadding = ImVec2(8.0f, 4.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);

        style.WindowRounding = 2.0f;
        style.GrabRounding = 2.0f;
        style.TabRounding = 2.0f;
        style.ChildRounding = 2.0f;
        style.PopupRounding = 2.0f;
        style.FrameRounding = 2.0f;
        style.ScrollbarRounding = 2.0f;

        style.WindowBorderSize = 1.0f;
        style.PopupBorderSize = 1.0f;

        style.ChildBorderSize = 0.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;

        style.WindowMinSize = ImVec2(32.0f, 32.0f);
        style.WindowTitleAlign = ImVec2(0.0f, 0.5f);
        style.WindowMenuButtonPosition = ImGuiDir_Left;

        style.ItemInnerSpacing = ImVec2(2.0f, 2.0f);
        style.IndentSpacing = 21.0f;
        style.ColumnsMinSpacing = 6.0f;
        style.ScrollbarSize = 13.0f;
        style.GrabMinSize = 7.0f;
        style.TabMinWidthForCloseButton = 0.0f;
        style.ColorButtonPosition = ImGuiDir_Right;
        style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
        style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

        ImVec4 color_background_1     = Lerp(bg_1, bg_2, .0);
        ImVec4 color_background_2     = Lerp(bg_1, bg_2, .1);
        ImVec4 color_background_3     = Lerp(bg_1, bg_2, .2);
        ImVec4 color_background_4     = Lerp(bg_1, bg_2, .3);
        ImVec4 color_background_5     = Lerp(bg_1, bg_2, .4);
        ImVec4 color_background_6     = Lerp(bg_1, bg_2, .5);
        ImVec4 color_background_7     = Lerp(bg_1, bg_2, .6);
        ImVec4 color_background_8     = Lerp(bg_1, bg_2, .7);
        ImVec4 color_background_9     = Lerp(bg_1, bg_2, .8);
        ImVec4 color_background_10    = Lerp(bg_1, bg_2, .9);

        // should be dark
        ImVec4 color_black_transparent_9     = {0.0f, 0.0f, 0.0f, 0.9f};
        ImVec4 color_black_transparent_6     = {0.0f, 0.0f, 0.0f, 0.6f};
        ImVec4 color_black_transparent_3     = {0.0f, 0.0f, 0.0f, 0.3f};
        ImVec4 color_black_transparent_1     = {0.0f, 0.0f, 0.0f, 0.1f};

        ImVec4 color_highlight_1       = Lerp(h_1,h_2, 0);

        ImVec4 color_accent_2         = Lerp(h_1,h_2, 0.2);//{55.0f / 255.0f, 75.0f / 255.0f, 255.0f / 255.0f, 1.0f};
        ImVec4 color_accent_3         = Lerp(h_1,h_2, 0.3);//{50.0f / 255.0f, 70.0f / 255.0f, 255.0f / 255.0f, 1.0f};

        // not used
        // ImVec4 color_highlight_2     = Lerp(h_1,h_2,.1);
        // ImVec4 color_highlight_3     = Lerp(h_1,h_2,.2);
        // ImVec4 color_highlight_4     = Lerp(h_1,h_2,.3);
        // ImVec4 color_highlight_5     = Lerp(h_1,h_2,.4);
        // ImVec4 color_highlight_6     = Lerp(h_1,h_2,.5);
        // ImVec4 color_highlight_7     = Lerp(h_1,h_2,.6);
        // ImVec4 color_highlight_8     = Lerp(h_1,h_2,.7);
        // ImVec4 color_highlight_9     = Lerp(h_1,h_2,.8);
        // ImVec4 color_highlight_10     = Lerp(h_1,h_2,.9);

        style.Colors[ImGuiCol_Text] = color_highlight_1;
        style.Colors[ImGuiCol_TextDisabled] = color_background_9;

        style.Colors[ImGuiCol_WindowBg] = color_background_2;
        style.Colors[ImGuiCol_FrameBg] = color_background_4;
        style.Colors[ImGuiCol_TitleBg] = color_background_1;
        style.Colors[ImGuiCol_TitleBgActive] = color_background_2;

        // accent
        style.Colors[ImGuiCol_ScrollbarGrabActive] = color_accent_1;
        style.Colors[ImGuiCol_SeparatorActive] = color_accent_1;
        style.Colors[ImGuiCol_SliderGrabActive] = color_accent_1;
        style.Colors[ImGuiCol_ResizeGripActive] = color_accent_1;
        style.Colors[ImGuiCol_DragDropTarget] = color_accent_1;
        style.Colors[ImGuiCol_NavHighlight] = color_accent_1;
        style.Colors[ImGuiCol_NavWindowingHighlight] = color_accent_1;
        style.Colors[ImGuiCol_TabSelectedOverline] = color_accent_1;
        style.Colors[ImGuiCol_TabDimmedSelectedOverline] = color_accent_1;
        style.Colors[ImGuiCol_CheckMark] = color_accent_1;

        style.Colors[ImGuiCol_Tab] = style.Colors[ImGuiCol_TitleBg];
        style.Colors[ImGuiCol_TabDimmed] = style.Colors[ImGuiCol_TitleBg];

        style.Colors[ImGuiCol_TabSelected] = style.Colors[ImGuiCol_WindowBg];
        style.Colors[ImGuiCol_TabDimmedSelected] = style.Colors[ImGuiCol_WindowBg];

        style.Colors[ImGuiCol_FrameBgHovered] = color_background_3;

        style.Colors[ImGuiCol_TitleBgCollapsed] = color_background_2;
        style.Colors[ImGuiCol_MenuBarBg] = color_background_3;
        style.Colors[ImGuiCol_ScrollbarBg] = color_background_2;


        style.Colors[ImGuiCol_Button] = color_background_2;
        style.Colors[ImGuiCol_ButtonHovered] = color_background_3;
        style.Colors[ImGuiCol_ButtonActive] = color_background_4;

        // alternative
        // style.Colors[ImGuiCol_Button] = {};
        // style.Colors[ImGuiCol_ButtonHovered] = color_highlight_4;
        // style.Colors[ImGuiCol_ButtonActive] = color_highlight_5;

        style.Colors[ImGuiCol_ResizeGrip] =  color_black_transparent_3;
        style.Colors[ImGuiCol_ResizeGripHovered] = color_black_transparent_6;
        style.Colors[ImGuiCol_TableRowBgAlt] = color_black_transparent_1;
        style.Colors[ImGuiCol_TextSelectedBg] = color_black_transparent_1;

        style.Colors[ImGuiCol_DockingPreview] = color_accent_1;
        style.Colors[ImGuiCol_PlotLinesHovered] = color_accent_2;
        style.Colors[ImGuiCol_PlotHistogramHovered] = color_accent_3;

        style.Colors[ImGuiCol_PlotHistogram] = color_background_10;

        style.Colors[ImGuiCol_HeaderHovered] = color_background_9;
        style.Colors[ImGuiCol_HeaderActive] = color_background_9;
        style.Colors[ImGuiCol_PlotLines] = color_background_9;

        style.Colors[ImGuiCol_TabHovered] = color_background_7;
        style.Colors[ImGuiCol_SeparatorHovered] = color_background_8;
        style.Colors[ImGuiCol_SliderGrab] = color_background_8;
        style.Colors[ImGuiCol_PopupBg] = color_background_6;
        style.Colors[ImGuiCol_Header] = color_background_6;
        style.Colors[ImGuiCol_TableBorderStrong] = color_background_6;
        style.Colors[ImGuiCol_ScrollbarGrabHovered] = color_background_6;
        style.Colors[ImGuiCol_Separator] = color_background_4;
        style.Colors[ImGuiCol_TableBorderLight] = color_background_4;
        style.Colors[ImGuiCol_FrameBgActive] = color_background_5;
        style.Colors[ImGuiCol_ScrollbarGrab] = color_background_5;

        style.Colors[ImGuiCol_ChildBg] = {};
        style.Colors[ImGuiCol_Border] = color_background_5;

        style.Colors[ImGuiCol_TableHeaderBg] = color_background_3;

        style.Colors[ImGuiCol_NavWindowingDimBg] = color_black_transparent_6;
        style.Colors[ImGuiCol_ModalWindowDimBg] = color_black_transparent_6;

        style.Colors[ImGuiCol_TableRowBg] = {};
        style.Colors[ImGuiCol_BorderShadow] = {};

        style.ScaleAllSizes(Spartan::Window::GetDpiScale());
    }
}
