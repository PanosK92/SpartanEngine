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
    class Plane
    {
    public:
        Plane() = default;

        Plane(const Vector3& normal, float d);

        // construct from a normal vector and a point on the plane
        Plane(const Vector3& normal, const Vector3& point);

        // construct from 3 vertices
        Plane(const Vector3& a, const Vector3& b, const Vector3& c);

        ~Plane() = default;

        void Normalize();
        static Plane Normalize(const Plane& plane);

        float Dot(const Vector3& v) const;
        static float Dot(const Plane& p, const Vector3& v);

        Vector3 normal = Vector3::Zero;
        float d        = 0.0f; // distance from origin
    };
}
