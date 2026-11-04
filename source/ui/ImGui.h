/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
struct ImFont;
struct ImVec2;
namespace spartan::gui
{
    inline ImFont* font_normal = nullptr;
    inline ImFont* font_bold = nullptr;
    inline ImFont* font_mono = nullptr;
    inline ImFont* font_mono_medium = nullptr;
    void initialize(bool editor = true);
    void shutdown();
    void begin_frame();
    void render();

    // frosts a screen rectangle of the current window, for windows without a background that draw their own panels
    void glass_pane(const ImVec2& min, const ImVec2& max, float rounding);
}
