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

//= INCLUDES ==============
#include "../math/Matrix.h"
//=========================

namespace spartan
{
    #pragma pack(push, 1)
    // 32 bytes, mirrored by PackedInstance. Preserve all three scales: averaging them changes
    // the rendered shape after placement has already seated it against the terrain.
    // positions stay full float, a half quantizes to a quarter metre a few hundred metres out and
    // that is enough to float a tree off a tile or bury a rock in it
    struct Instance
    {
        float position_x;     // 4 bytes
        float position_y;     // 4 bytes
        float position_z;     // 4 bytes
        uint32_t rotation_xy; // signed normalized 16-bit quaternion components
        uint32_t rotation_zw;
        float scale_x;
        float scale_y;
        float scale_z;

        math::Matrix GetMatrix() const;
        void SetMatrix(const math::Matrix& matrix);

        static Instance GetIdentity();
    };
    #pragma pack(pop)
    static_assert(sizeof(Instance) == 32);
}
