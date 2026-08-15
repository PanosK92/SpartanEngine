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
