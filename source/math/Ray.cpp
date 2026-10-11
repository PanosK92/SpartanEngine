/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======
#include "pch.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::math
{
    Ray::Ray(const Vector3& start, const Vector3& direction)
    {
        m_origin    = start;
        m_direction = direction.Normalized();
    }

    float Ray::HitDistance(const BoundingBox& box) const
    {
        // check for ray origin being inside the box
        if (box.Intersects(m_origin) == Intersection::Inside)
        {
            return 0.0f;
        }

        // slab test, the origin is outside so the entry distance is the hit distance
        const __m128 infinity  = _mm_set1_ps(numeric_limits<float>::infinity());
        const __m128 origin    = m_origin.Simd();
        const __m128 direction = m_direction.Simd();
        const __m128 box_min   = box.GetMin().Simd();
        const __m128 box_max   = box.GetMax().Simd();

        const __m128 direction_inv = _mm_div_ps(_mm_set1_ps(1.0f), direction);
        const __m128 t1            = _mm_mul_ps(_mm_sub_ps(box_min, origin), direction_inv);
        const __m128 t2            = _mm_mul_ps(_mm_sub_ps(box_max, origin), direction_inv);

        // axes the ray is parallel to (and the zero w lane) only pass if the origin lies within their slab
        const __m128 parallel     = _mm_cmpeq_ps(direction, _mm_setzero_ps());
        const __m128 within_slab  = _mm_and_ps(_mm_cmpge_ps(origin, box_min), _mm_cmple_ps(origin, box_max));
        const __m128 parallel_min = _mm_blendv_ps(infinity, simd::negate(infinity), within_slab);
        const __m128 t_near       = _mm_blendv_ps(_mm_min_ps(t1, t2), parallel_min, parallel);
        const __m128 t_far        = _mm_blendv_ps(_mm_max_ps(t1, t2), infinity, parallel);

        // largest entry and smallest exit across the axes
        __m128 entry = _mm_max_ps(t_near, _mm_permute_ps(t_near, _MM_SHUFFLE(1, 0, 3, 2)));
        entry        = _mm_max_ps(entry, _mm_permute_ps(entry, _MM_SHUFFLE(2, 3, 0, 1)));
        __m128 exit  = _mm_min_ps(t_far, _mm_permute_ps(t_far, _MM_SHUFFLE(1, 0, 3, 2)));
        exit         = _mm_min_ps(exit, _mm_permute_ps(exit, _MM_SHUFFLE(2, 3, 0, 1)));

        const float distance_entry = _mm_cvtss_f32(entry);
        const float distance_exit  = _mm_cvtss_f32(exit);
        if (distance_entry >= 0.0f && distance_entry <= distance_exit)
        {
            return distance_entry;
        }

        return numeric_limits<float>::infinity();
    }

    float Ray::HitDistance(const Vector3& v1, const Vector3& v2, const Vector3& v3, Vector3* out_normal /*= nullptr*/, Vector3* out_bary /*= nullptr*/) const
    {
        // Based on Fast, Minimum Storage Ray/Triangle Intersection by Moller & Trumbore
        // http://www.graphics.cornell.edu/pubs/1997/MT97.pdf
        const __m128 a         = v1.Simd();
        const __m128 direction = m_direction.Simd();
        const __m128 edge1     = _mm_sub_ps(v2.Simd(), a);
        const __m128 edge2     = _mm_sub_ps(v3.Simd(), a);

        // Calculate determinant & check backfacing
        const __m128 p  = simd::cross3(direction, edge2);
        const float det = simd::dot3(edge1, p);

        if (det >= numeric_limits<float>::min())
        {
            // Calculate u & v parameters and test
            const __m128 t = _mm_sub_ps(m_origin.Simd(), a);
            const float u  = simd::dot3(t, p);
            if (u >= 0.0f && u <= det)
            {
                const __m128 q = simd::cross3(t, edge1);
                const float v  = simd::dot3(direction, q);
                if (v >= 0.0f && u + v <= det)
                {
                    const float distance = simd::dot3(edge2, q) / det;

                    // Discard hits behind the ray
                    if (distance >= 0.0f)
                    {
                        // There is an intersection, so calculate distance & optional normal
                        if (out_normal)
                        {
                            *out_normal = Vector3(simd::cross3(edge1, edge2));
                        }
                        if (out_bary)
                        {
                            // (0, u / det, v / det), then 1 - u / det - v / det in x
                            const __m128 uv   = _mm_div_ps(_mm_setr_ps(0.0f, u, v, 0.0f), _mm_set1_ps(det));
                            const __m128 sum  = _mm_add_ss(simd::splat<1>(uv), simd::splat<2>(uv));
                            *out_bary         = Vector3(_mm_move_ss(uv, _mm_sub_ss(_mm_set_ss(1.0f), sum)));
                        }

                        return distance;
                    }
                }
            }
        }

        return numeric_limits<float>::infinity();
    }
}
