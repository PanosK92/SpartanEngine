/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES ===============
#include "Rendering/Color.h"
#include <string>
#include <cstdint>
//==========================

class ButtonColorPicker
{
public:
    ButtonColorPicker(const std::string& window_title);
    ButtonColorPicker() = default;

    void Update();

    void SetColor(const Spartan::Color& color) { m_color = color; }
    const Spartan::Color& GetColor()     const { return m_color; }

private:
    bool m_is_visible          = false;
    bool m_hdr                 = false;
    bool m_alpha_preview       = true;
    bool m_alpha_half_preview  = false;
    bool m_options_menu        = true;
    bool m_show_wheel          = false;
    bool m_show_preview        = false;
    bool m_show_rgb            = true;
    bool m_show_hsv            = false;
    bool m_show_hex            = true;
    Spartan::Color m_color     = Spartan::Color(0, 0, 0, 1);
    uint32_t m_combo_box_index = 0;
    std::string m_window_title;
    std::string m_color_picker_label;
};
