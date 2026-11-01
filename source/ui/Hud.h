/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include <string>
#include "../math/Vector2.h"
#include "../math/Vector4.h"

// screen space drawing (menus, titles, fades), in pixels of the game view: the editor viewport while the editor shows, the window otherwise
// primitives only land while imgui is between new frame and render, outside it they are dropped
namespace spartan::hud
{
    // zero when there is no game view
    math::Vector2 get_size();
    // relative to the game view, -1 when there is no game view
    math::Vector2 get_mouse_position();

    // path is relative to the working directory (binaries), the name is what text and text_width take
    bool load_font(const std::string& name, const std::string& path);

    void rect(float x, float y, float w, float h, const math::Vector4& color, float rounding);
    void rect_outline(float x, float y, float w, float h, const math::Vector4& color, float thickness, float rounding);
    // corners clockwise from the top left
    void rect_gradient(float x, float y, float w, float h, const math::Vector4& top_left, const math::Vector4& top_right, const math::Vector4& bottom_right, const math::Vector4& bottom_left);
    void line(float x1, float y1, float x2, float y2, const math::Vector4& color, float thickness);
    // a thickness of zero or less fills the circle
    void circle(float x, float y, float radius, const math::Vector4& color, float thickness);
    void triangle(float x1, float y1, float x2, float y2, float x3, float y3, const math::Vector4& color);

    // font is a name given to load_font, or regular, bold or mono
    float text_width(const std::string& content, float size, const std::string& font, float spacing);
    // align 0 left, 0.5 centre, 1 right of x; y is the top of the line; returns the drawn width
    float text(const std::string& content, float x, float y, float size, const math::Vector4& color, const std::string& font, float align, float spacing);
}
