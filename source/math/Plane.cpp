/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =======
#include "pch.h"
//==================

namespace spartan::math
{
    Plane::Plane(const Vector3& normal, float d)
    {
        this->normal = normal;
        this->d      = d;
    }

    Plane::Plane(const Vector3& a, const Vector3& b, const Vector3& c)
    {
        const Vector3 ab = b - a;
        const Vector3 ac = c - a;

        const Vector3 cross = Vector3::Cross(ab, ac);
        this->normal        = Vector3::Normalize(cross);
        this->d             = -Vector3::Dot(normal, a);
    }

    Plane::Plane(const Vector3& normal, const Vector3& point)
    {
        this->normal = normal.Normalized();
        d = -this->normal.Dot(point);
    }

    void Plane::Normalize()
    {
        // scale normal and distance together so the plane stays the same
        const __m128 n              = normal.Simd();
        const __m128 length_squared = simd::dot3_splat(n, n);
        if (_mm_cvtss_f32(length_squared) > 0.0f)
        {
            const __m128 plane = _mm_div_ps(_mm_insert_ps(n, _mm_set_ss(d), 0x30), _mm_sqrt_ps(length_squared));
            normal = Vector3(plane);
            d      = _mm_cvtss_f32(simd::splat<3>(plane));
        }
    }
}
