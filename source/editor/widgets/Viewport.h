/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include "Widget.h"
//=================

class Viewport : public Widget
{
public:
    Viewport(Editor* editor);

    void OnTickVisible() override;

    // screen-space rect of the 3d render area, published every frame so other systems
    // (eg. game huds) can snap overlays to the viewport instead of the os window
    static const spartan::math::Vector2& GetScreenPosition() { return m_screen_position; }
    static const spartan::math::Vector2& GetScreenSize()     { return m_screen_size; }

private:
    spartan::math::Vector2 m_offset = spartan::math::Vector2::Zero;
    float m_window_padding          = 4.0f;

    inline static spartan::math::Vector2 m_screen_position = spartan::math::Vector2::Zero;
    inline static spartan::math::Vector2 m_screen_size     = spartan::math::Vector2::Zero;
};
