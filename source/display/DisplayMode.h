/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

namespace spartan
{
    struct DisplayMode
    {
        DisplayMode() = default;
        DisplayMode(const uint32_t width, const uint32_t height, const float hz, const uint32_t display_id)
        {
            this->width      = width;
            this->height     = height;
            this->hz         = hz;
            this->display_id = display_id;
        }

        bool operator ==(const DisplayMode& rhs) const
        {
            return
                width      == rhs.width  &&
                height     == rhs.height &&
                hz         == rhs.hz     &&
                display_id == rhs.display_id;
        }

        uint32_t width      = 0;
        uint32_t height     = 0;
        float hz            = 0;
        uint32_t display_id = 0;

    };
}
