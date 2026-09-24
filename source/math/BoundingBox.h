/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
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
            BoundingBox();
            BoundingBox(const Vector3& min, const Vector3& max);
            BoundingBox(const Vector3* vertices, const uint32_t point_count);
            BoundingBox(const RHI_Vertex_PosTexNorTan* vertices, const uint32_t vertex_count);
            ~BoundingBox() = default;

            BoundingBox& operator =(const BoundingBox& rhs) = default;

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

            // intersection
            Intersection Intersects(const Vector3& point) const;
            Intersection Intersects(const BoundingBox& box) const;
            bool Contains(const Vector3& point) const;

            // modulation
            void Merge(const BoundingBox& box);

            // edges and points on them
            Vector3 GetClosestPoint(const Vector3& point) const
            {
                return Vector3(
                    std::max(m_min.x, std::min(point.x, m_max.x)),
                    std::max(m_min.y, std::min(point.y, m_max.y)),
                    std::max(m_min.z, std::min(point.z, m_max.z))
                );
            }
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

            // dimensions
            // infinite min/max make center/extents nan via inf-inf, treat that case explicitly
            bool IsInfinite() const { return m_min == Vector3::InfinityNeg && m_max == Vector3::Infinity; }
            Vector3 GetCenter() const  { return IsInfinite() ? Vector3::Zero : (m_max + m_min) * 0.5f; }
            Vector3 GetSize() const    { return IsInfinite() ? Vector3::Infinity : (m_max - m_min); }
            Vector3 GetExtents() const { return IsInfinite() ? Vector3::Infinity : (m_max - m_min) * 0.5f; }
            float GetVolume() const
            {
                Vector3 size = GetSize();
                return size.x * size.y * size.z;
            }
            const Vector3& GetMin() const { return m_min; }
            const Vector3& GetMax() const { return m_max; }

            static const BoundingBox Zero;
            static const BoundingBox Unit;
            static const BoundingBox Infinite;

        private:
            Vector3 m_min;
            Vector3 m_max;
        };
    }
}
