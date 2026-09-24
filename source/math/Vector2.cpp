/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======
#include "pch.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::math
{
    const Vector2 Vector2::Zero(0.0f, 0.0f);
    const Vector2 Vector2::One(1.0f, 1.0f);

    string Vector2::ToString() const
    {
        char buffer[200];
        sprintf_s(buffer, sizeof(buffer), "X:%f, Y:%f", x, y);
        return string(buffer);
    }
}
