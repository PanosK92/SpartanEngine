/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "Helper.h"
#include "Vector3.h"
#include "Matrix.h"
#include <array>
//==================

namespace spartan
{
    struct RHI_Vertex_PosTexNorTan;

    namespace math
    {
        class BoundingBox
        {
        public:
            // construct with zero size
            BoundingBox();

            // construct from minimum and maximum vectors
            BoundingBox(const Vector3& min, const Vector3& max);

            // construct from points
            BoundingBox(const Vector3* vertices, const uint32_t point_count);

            // construct from vertices
            BoundingBox(const RHI_Vertex_PosTexNorTan* vertices, const uint32_t vertex_count);

            ~BoundingBox() = default;

            // assign from bounding box
            BoundingBox& operator =(const BoundingBox& rhs) = default;

            // returns the center
            Vector3 GetCenter() const { return (m_max + m_min) * 0.5f; }

            // returns the size
            Vector3 GetSize() const { return m_max - m_min; }

            // returns extents
            Vector3 GetExtents() const { return (m_max - m_min) * 0.5f; }

            // test if a point is inside
            Intersection Intersects(const Vector3& point) const;

            // test if a bounding box is inside
            Intersection Intersects(const BoundingBox& box) const;

            bool Contains(const Vector3& point) const;

            // merge with another bounding box
            void Merge(const BoundingBox& box);

            float Volume() const
            {
                Vector3 size = GetSize();
                return size.x * size.y * size.z;
            }

            Vector3 GetClosestPoint(const Vector3& point) const;

            void GetCorners(std::array<Vector3, 8>* corners) const
            {
                *corners =
                {
                    m_min,
                    Vector3(m_max.x, m_min.y, m_min.z),
                    Vector3(m_min.x, m_max.y, m_min.z),
                    Vector3(m_min.x, m_min.y, m_max.z),
                    Vector3(m_min.x, m_max.y, m_max.z),
                    Vector3(m_max.x, m_min.y, m_max.z),
                    Vector3(m_max.x, m_max.y, m_min.z),
                    m_max
                };
            }

            bool operator==(const BoundingBox& other) const
            {
                return GetMin() == other.GetMin() && GetMax() == other.GetMax();
            }

            BoundingBox operator*(const Matrix& transform) const
            {
                const Vector3 center_new = transform * GetCenter();
                const Vector3 extent_old = GetExtents();
        
                const Vector3 extent_new = Vector3
                (
                    abs(transform.m00) * extent_old.x + abs(transform.m10) * extent_old.y + abs(transform.m20) * extent_old.z,
                    abs(transform.m01) * extent_old.x + abs(transform.m11) * extent_old.y + abs(transform.m21) * extent_old.z,
                    abs(transform.m02) * extent_old.x + abs(transform.m12) * extent_old.y + abs(transform.m22) * extent_old.z
                );

                return BoundingBox(center_new - extent_new, center_new + extent_new);
            }

            const Vector3& GetMin() const { return m_min; }
            const Vector3& GetMax() const { return m_max; }

            static const BoundingBox Undefined;

        private:
            Vector3 m_min;
            Vector3 m_max;
        };
    }
}
