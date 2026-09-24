/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======
#include "pch.h"
//==================

namespace spartan::math
{
    Sphere::Sphere(const Vector3& center, const float radius)
    {
        this->center = center;
        this->radius = radius;
    }
}
