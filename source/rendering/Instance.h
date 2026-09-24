/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
