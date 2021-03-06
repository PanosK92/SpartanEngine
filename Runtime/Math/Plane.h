/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace Spartan::Math
{
    class Plane
    {
    public:
        Plane() = default;

        Plane(const Vector3& normal, float d);

        // Construct from a normal vector and a point on the plane
        Plane(const Vector3& normal, const Vector3& point);

        // Construct from 3 vertices
        Plane(const Vector3& a, const Vector3& b, const Vector3& c);

        ~Plane() = default;

        void Normalize();
        static Plane Normalize(const Plane& plane);

        float Dot(const Vector3& v) const;
        static float Dot(const Plane& p, const Vector3& v);

        Vector3 normal = Vector3::Zero;
        float d = 0.0f; // distance from origin
    };
}
