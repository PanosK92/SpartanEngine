/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===============
#include "rendering/Color.h"
#include <string>
#include <cstdint>
//==========================

class ButtonColorPicker
{
public:
    ButtonColorPicker(const std::string& window_title);
    ButtonColorPicker() = default;

    void Update();

    void SetColor(const spartan::Color& color) { m_color = color; }
    const spartan::Color& GetColor()     const { return m_color; }

private:
    bool m_is_visible          = false;
    bool m_hdr                 = false;
    bool m_alpha_half_preview  = false;
    bool m_options_menu        = true;
    bool m_show_wheel          = false;
    bool m_show_preview        = false;
    bool m_show_rgb            = true;
    bool m_show_hsv            = false;
    bool m_show_hex            = true;
    spartan::Color m_color     = spartan::Color(0, 0, 0, 1);
    uint32_t m_combo_box_index = 0;
    std::string m_window_title;
    std::string m_color_picker_label;
};
