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
                // rows for row-vector multiplication, shared by the center and the extent
                __m128 row0 = transform.Column(0);
                __m128 row1 = transform.Column(1);
                __m128 row2 = transform.Column(2);
                __m128 row3 = transform.Column(3);
                _MM_TRANSPOSE4_PS(row0, row1, row2, row3);

                // infinite min/max make center/extents nan via inf-inf, so an infinite box keeps a zero center and infinite extents
                const __m128 box_min  = m_min.Simd();
                const __m128 box_max  = m_max.Simd();
                const __m128 infinity = _mm_set1_ps(std::numeric_limits<float>::infinity());
                const bool infinite   = (simd::equal_mask(box_min, simd::negate(infinity)) & simd::equal_mask(box_max, infinity) & 0x7) == 0x7;
                const __m128 half     = _mm_set1_ps(0.5f);
                const __m128 center   = infinite ? _mm_setzero_ps() : _mm_mul_ps(_mm_add_ps(box_max, box_min), half);
                const __m128 extent   = infinite ? infinity : _mm_mul_ps(_mm_sub_ps(box_max, box_min), half);

                __m128 center_new   = _mm_fmadd_ps(row0, simd::splat<0>(center), row3);
                center_new          = _mm_fmadd_ps(row1, simd::splat<1>(center), center_new);
                center_new          = _mm_fmadd_ps(row2, simd::splat<2>(center), center_new);
                const __m128 w      = simd::splat<3>(center_new);
                if (_mm_cvtss_f32(w) != 1.0f)
                {
                    center_new = _mm_div_ps(center_new, w);
                }

                // the extent along each new axis is the extent projected on the absolute rotation-scale rows
                __m128 extent_new   = _mm_mul_ps(simd::abs(row0), simd::splat<0>(extent));
                extent_new          = _mm_fmadd_ps(simd::abs(row1), simd::splat<1>(extent), extent_new);
                extent_new          = _mm_fmadd_ps(simd::abs(row2), simd::splat<2>(extent), extent_new);

                return BoundingBox(Vector3(_mm_sub_ps(center_new, extent_new)), Vector3(_mm_add_ps(center_new, extent_new)));
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
                return Vector3(_mm_max_ps(m_min.Simd(), _mm_min_ps(point.Simd(), m_max.Simd())));
            }
            void GetCorners(std::array<Vector3, 8>* corners) const
            {
                // each blend bit picks max over min for that axis
                const __m128 box_min = m_min.Simd();
                const __m128 box_max = m_max.Simd();
                *corners =
                {
                    m_min,
                    Vector3(_mm_blend_ps(box_min, box_max, 0x1)),
                    Vector3(_mm_blend_ps(box_min, box_max, 0x2)),
                    Vector3(_mm_blend_ps(box_min, box_max, 0x4)),
                    Vector3(_mm_blend_ps(box_min, box_max, 0x6)),
                    Vector3(_mm_blend_ps(box_min, box_max, 0x5)),
                    Vector3(_mm_blend_ps(box_min, box_max, 0x3)),
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
                const __m128 size = GetSize().Simd();
                return _mm_cvtss_f32(_mm_mul_ss(_mm_mul_ss(size, _mm_movehdup_ps(size)), _mm_movehl_ps(size, size)));
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
