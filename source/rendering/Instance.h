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
#include "../Math/Matrix.h"
//=========================

namespace spartan
{
    #pragma pack(push, 1)
    // one packed transform per instance, 12 bytes so the stride matches the shader side PackedInstance
    struct Instance
    {
        uint16_t position_x;  // 2 bytes
        uint16_t position_y;  // 2 bytes
        uint16_t position_z;  // 2 bytes
        uint16_t normal_oct;  // 2 bytes
        uint8_t yaw_packed;   // 1 byte
        uint8_t scale_packed; // 1 byte
        uint16_t padding;     // 2 bytes, keeps stride at 12 to match shader PackedInstance (3xu32)
                              // procedural grass uses Sb_GrassInstance instead, half-floats lose sub-cm precision a few hundred meters out

        math::Matrix GetMatrix() const;
        void SetMatrix(const math::Matrix& matrix);

        static Instance GetIdentity();
    };
    #pragma pack(pop)
}
