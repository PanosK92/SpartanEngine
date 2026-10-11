/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======
#include "pch.h"
#include "Frustum.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace spartan::math
{
    Frustum::Frustum(const Matrix& view, const Matrix& projection)
    {
        const Matrix view_projection = view * projection;
        const __m128 c0 = view_projection.Column(0);
        const __m128 c1 = view_projection.Column(1);
        const __m128 c2 = view_projection.Column(2);
        const __m128 c3 = view_projection.Column(3);

        // each plane is (normal, d)
        __m128 planes[8] =
        {
            _mm_add_ps(c3, c2), // near
            _mm_sub_ps(c3, c2), // far
            _mm_add_ps(c3, c0), // left
            _mm_sub_ps(c3, c0), // right
            _mm_sub_ps(c3, c1), // top
            _mm_add_ps(c3, c1), // bottom
            _mm_setzero_ps(),
            _mm_setzero_ps()
        };

        for (uint32_t i = 0; i < 6; ++i)
        {
            planes[i] = _mm_div_ps(planes[i], _mm_sqrt_ps(simd::dot3_splat(planes[i], planes[i])));
        }

        // transpose into one array per component
        for (uint32_t i = 0; i < 8; i += 4)
        {
            __m128 x = planes[i + 0];
            __m128 y = planes[i + 1];
            __m128 z = planes[i + 2];
            __m128 d = planes[i + 3];
            _MM_TRANSPOSE4_PS(x, y, z, d);
            _mm_storeu_ps(m_plane_x + i, x);
            _mm_storeu_ps(m_plane_y + i, y);
            _mm_storeu_ps(m_plane_z + i, z);
            _mm_storeu_ps(m_plane_d + i, d);
        }
    }

    bool Frustum::IsVisible(const Vector3& center, const Vector3& extent, bool ignore_depth /*= false*/) const
    {
        SP_ASSERT(!center.IsNaN() && !extent.IsNaN());

        const __m256 sign = _mm256_set1_ps(-0.0f);
        const __m256 nx   = _mm256_loadu_ps(m_plane_x);
        const __m256 ny   = _mm256_loadu_ps(m_plane_y);
        const __m256 nz   = _mm256_loadu_ps(m_plane_z);

        // signed distance from the box center to each plane
        __m256 distance = _mm256_fmadd_ps(nx, _mm256_set1_ps(center.x), _mm256_loadu_ps(m_plane_d));
        distance        = _mm256_fmadd_ps(ny, _mm256_set1_ps(center.y), distance);
        distance        = _mm256_fmadd_ps(nz, _mm256_set1_ps(center.z), distance);

        // projected radius of the box on each plane normal
        __m256 radius = _mm256_mul_ps(_mm256_andnot_ps(sign, nx), _mm256_set1_ps(extent.x));
        radius        = _mm256_fmadd_ps(_mm256_andnot_ps(sign, ny), _mm256_set1_ps(extent.y), radius);
        radius        = _mm256_fmadd_ps(_mm256_andnot_ps(sign, nz), _mm256_set1_ps(extent.z), radius);

        // outside if the box is fully behind any plane, near and far are the two low lanes
        const int outside = _mm256_movemask_ps(_mm256_cmp_ps(_mm256_add_ps(distance, radius), _mm256_setzero_ps(), _CMP_LT_OQ));
        return (outside & (ignore_depth ? 0x3c : 0x3f)) == 0;
    }
}
