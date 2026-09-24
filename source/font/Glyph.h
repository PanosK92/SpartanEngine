/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    struct Glyph
    {
        int32_t offset_x            = 0;
        int32_t offset_y            = 0;
        uint32_t width              = 0;
        uint32_t height             = 0;
        uint32_t horizontal_advance = 0;
        float uv_x_left             = 0.0f;
        float uv_x_right            = 0.0f;
        float uv_y_top              = 0.0f;
        float uv_y_bottom           = 0.0f;
    };
}
