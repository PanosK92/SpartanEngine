/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ===========================
#include "Vector3.h"
#include "../Core/Spartan_Definitions.h"
//======================================

namespace Spartan
{
    class Context;

    namespace Math
    {
        class RayHit;
        class BoundingBox;

        class SPARTAN_CLASS Ray
        {
        public:
            Ray() = default;
            Ray(const Vector3& start, const Vector3& end);
            ~Ray() = default;

            // Returns hit distance to a bounding box, or infinity if there is no hit.
            float HitDistance(const BoundingBox& box) const;

            // Return hit distance to a triangle, or infinity if no hit. Optionally return hit normal and hit barycentric coordinate at intersect point.
            float HitDistance(const Vector3& v1, const Vector3& v2, const Vector3& v3, Vector3* out_normal = nullptr, Vector3* out_bary = nullptr) const;

            const auto& GetStart()      const { return m_start; }
            const auto& GetEnd()        const { return m_end; }
            const auto& GetLength()     const { return m_length; }
            const auto& GetDirection()  const { return m_direction; }

        private:
            Vector3 m_start;
            Vector3 m_end;
            Vector3 m_direction;
            float m_length = 0.0f;
        };
    }
}
