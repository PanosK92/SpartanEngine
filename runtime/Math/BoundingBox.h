/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES ==========
#include "MathHelper.h"
#include "Vector3.h"
//=====================

namespace Spartan
{
    struct RHI_Vertex_PosTexNorTan;

    namespace Math
    {
        class Matrix;

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

            // Returns a transformed bounding box
            BoundingBox Transform(const Matrix& transform) const;

            // merge with another bounding box
            void Merge(const BoundingBox& box);

            float Volume() const
            {
                Vector3 size = GetSize();
                return size.x * size.y * size.z;
            }

            std::array<Vector3, 8> GetCorners() const
            {
                std::array<Vector3, 8> corners = {
                    m_min,
                    Vector3(m_max.x, m_min.y, m_min.z),
                    Vector3(m_min.x, m_max.y, m_min.z),
                    Vector3(m_min.x, m_min.y, m_max.z),
                    Vector3(m_min.x, m_max.y, m_max.z),
                    Vector3(m_max.x, m_min.y, m_max.z),
                    Vector3(m_max.x, m_max.y, m_min.z),
                    m_max
                };
                return corners;
            }

            bool operator==(const BoundingBox& other) const
            {
                return GetMin() == other.GetMin() && GetMax() == other.GetMax();
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
