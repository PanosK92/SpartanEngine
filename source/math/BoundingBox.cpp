/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================
#include "pch.h"
#include "../rhi/RHI_Vertex.h"
//============================

namespace spartan::math
{
    const BoundingBox BoundingBox::Zero(Vector3(0.0f, 0.0f, 0.0f), Vector3(0.0f, 0.0f, 0.0f));
    const BoundingBox BoundingBox::Unit(Vector3(-0.5f, -0.5f, -0.5f), Vector3(0.5f, 0.5f, 0.5f));
    const BoundingBox BoundingBox::Infinite(Vector3::InfinityNeg, Vector3::Infinity);

    BoundingBox::BoundingBox()
    {
        m_min = Vector3::Infinity;
        m_max = Vector3::InfinityNeg;
    }

    BoundingBox::BoundingBox(const Vector3& min, const Vector3& max)
    {
        this->m_min = min;
        this->m_max = max;
    }

    // the point goes first in min/max so a nan component is skipped, matching std::min/std::max
    BoundingBox::BoundingBox(const Vector3* points, const uint32_t point_count)
    {
        __m128 box_min = Vector3::Infinity.Simd();
        __m128 box_max = Vector3::InfinityNeg.Simd();

        for (uint32_t i = 0; i < point_count; i++)
        {
            const __m128 point = points[i].Simd();
            box_min = _mm_min_ps(point, box_min);
            box_max = _mm_max_ps(point, box_max);
        }

        m_min = Vector3(box_min);
        m_max = Vector3(box_max);
    }

    BoundingBox::BoundingBox(const RHI_Vertex_PosTexNorTan* vertices, const uint32_t vertex_count)
    {
        static_assert(offsetof(RHI_Vertex_PosTexNorTan, pos) + 4 * sizeof(float) <= sizeof(RHI_Vertex_PosTexNorTan), "a full 4-wide load of pos must stay inside the vertex");

        __m128 box_min = Vector3::Infinity.Simd();
        __m128 box_max = Vector3::InfinityNeg.Simd();

        for (uint32_t i = 0; i < vertex_count; i++)
        {
            // w picks up the next attribute, it is never stored
            const __m128 point = _mm_loadu_ps(vertices[i].pos);
            box_min = _mm_min_ps(point, box_min);
            box_max = _mm_max_ps(point, box_max);
        }

        m_min = Vector3(box_min);
        m_max = Vector3(box_max);
    }

    Intersection BoundingBox::Intersects(const Vector3& point) const
    {
        const __m128 p = point.Simd();
        const int outside = _mm_movemask_ps(_mm_or_ps(_mm_cmplt_ps(p, m_min.Simd()), _mm_cmpgt_ps(p, m_max.Simd())));
        return (outside & 0x7) ? Intersection::Outside : Intersection::Inside;
    }

    Intersection BoundingBox::Intersects(const BoundingBox& box) const
    {
        const __m128 self_min  = m_min.Simd();
        const __m128 self_max  = m_max.Simd();
        const __m128 other_min = box.m_min.Simd();
        const __m128 other_max = box.m_max.Simd();

        const int outside = _mm_movemask_ps(_mm_or_ps(_mm_cmplt_ps(other_max, self_min), _mm_cmpgt_ps(other_min, self_max)));
        if (outside & 0x7)
        {
            return Intersection::Outside;
        }

        const int crossing = _mm_movemask_ps(_mm_or_ps(_mm_cmplt_ps(other_min, self_min), _mm_cmpgt_ps(other_max, self_max)));
        if (crossing & 0x7)
        {
            return Intersection::Intersects;
        }

        return Intersection::Inside;
    }

    void BoundingBox::Merge(const BoundingBox& box)
    {
        m_min = Vector3(_mm_min_ps(box.m_min.Simd(), m_min.Simd()));
        m_max = Vector3(_mm_max_ps(box.m_max.Simd(), m_max.Simd()));
    }

    bool BoundingBox::Contains(const Vector3& point) const
    {
        const __m128 p = point.Simd();
        const int inside = _mm_movemask_ps(_mm_and_ps(_mm_cmpge_ps(p, m_min.Simd()), _mm_cmple_ps(p, m_max.Simd())));
        return (inside & 0x7) == 0x7;
    }
}
