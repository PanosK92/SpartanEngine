/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace spartan::math
{
    class Sphere
    {
    public:
        Sphere();
        Sphere(const Vector3& center, const float radius);
        ~Sphere() = default;

        Vector3 center;
        float radius;
    };
}
