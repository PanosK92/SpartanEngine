/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======
#include "pch.h"
#include "Vector4.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::math
{
    const Vector4 Vector4::One(1.0f, 1.0f, 1.0f, 1.0f);
    const Vector4 Vector4::Zero(0.0f, 0.0f, 0.0f, 0.0f);
    const Vector4 Vector4::Infinity(numeric_limits<float>::infinity(), numeric_limits<float>::infinity(), numeric_limits<float>::infinity(), numeric_limits<float>::infinity());
    const Vector4 Vector4::InfinityNeg(-numeric_limits<float>::infinity(), -numeric_limits<float>::infinity(), -numeric_limits<float>::infinity(), -numeric_limits<float>::infinity());

    Vector4::Vector4(const Vector3& value, float w)
    {
        this->x = value.x;
        this->y = value.y;
        this->z = value.z;
        this->w = w;
    }

    Vector4::Vector4(const Vector3& value)
    {
        this->x = value.x;
        this->y = value.y;
        this->z = value.z;
        this->w = 0.0f;
    }

    string Vector4::ToString() const
    {
        char buffer[200];
        sprintf_s(buffer, sizeof(buffer), "X:%f, Y:%f, Z:%f, W:%f", x, y, z, w);
        return string(buffer);
    }
}
