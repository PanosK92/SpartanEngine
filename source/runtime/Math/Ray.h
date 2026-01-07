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

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace spartan
{
    namespace math
    {
        class RayHit;
        class BoundingBox;
        class Plane;
        class Sphere;

        class Ray
        {
        public:
            Ray() = default;
            Ray(const Vector3& start, const Vector3& direction);
            ~Ray() = default;

            // returns hit distance or infinity if there is no hit
            float HitDistance(const BoundingBox& box) const;
            float HitDistance(const Plane& plane, Vector3* intersection_point = nullptr) const;
            float HitDistance(const Vector3& v1, const Vector3& v2, const Vector3& v3, Vector3* out_normal = nullptr, Vector3* out_bary = nullptr) const;
            float HitDistance(const Sphere& sphere) const;

            // returns the distance
            float Distance(const Vector3& point) const;
            float Distance(const Vector3& point, Vector3& closest_point) const;

            // returns the closest point
            Vector3 ClosestPoint(const Ray& ray) const;

            const Vector3& GetStart()     const { return m_origin; }
            const Vector3& GetDirection() const { return m_direction; }
            bool IsDefined()              const { return m_origin != m_direction && m_direction != Vector3::Zero; }

            Vector3 m_origin    = Vector3::Zero;
            Vector3 m_direction = Vector3::Zero;
        };
    }
}
