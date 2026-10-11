/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===
#include "pch.h"
//==============

//= NAMESPACES =====
using namespace std;
//==================

//= Based on ==========================================================================//
// http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/index.htm //
// Heading  -> Yaw        -> Y-axis                                                    //
// Attitude -> Pitch    -> X-axis                                                      //
// Bank     -> Roll        -> Z-axis                                                   //
//=====================================================================================//

namespace spartan::math
{
    const Quaternion Quaternion::Identity(0.0f, 0.0f, 0.0f, 1.0f);

    void Quaternion::FromAxes(const Vector3& axis_x, const Vector3& axis_y, const Vector3& axis_z)
    {
        // straight from the rotation matrix axes, GetRotation's decomposition is unstable
        *this = FromRotationRows(axis_x.Simd(), axis_y.Simd(), axis_z.Simd());
    }

    // based on: http://www.euclideanspace.com/maths/geometry/rotations/conversions/matrixToQuaternion/
    Quaternion Quaternion::FromRotationRows(const __m128 row0, const __m128 row1, const __m128 row2)
    {
        // a = (m12, m20, m01) and b = (m21, m02, m10), every branch only needs a + b and a - b
        const __m128 row0_zxy = _mm_permute_ps(row0, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 row1_zxy = _mm_permute_ps(row1, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 row2_zxy = _mm_permute_ps(row2, _MM_SHUFFLE(3, 1, 0, 2));
        const __m128 row0_yzx = _mm_permute_ps(row0, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 row1_yzx = _mm_permute_ps(row1, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 row2_yzx = _mm_permute_ps(row2, _MM_SHUFFLE(3, 0, 2, 1));
        const __m128 a        = _mm_blend_ps(_mm_blend_ps(row1_zxy, row2_zxy, 0x2), row0_zxy, 0x4);
        const __m128 b        = _mm_blend_ps(_mm_blend_ps(row2_yzx, row0_yzx, 0x2), row1_yzx, 0x4);
        const __m128 sum      = _mm_add_ps(a, b);
        const __m128 diff     = _mm_sub_ps(a, b);

        // (m00, m11, m22), and 1 + (m00 + m11 + m22, m00 - m11 - m22, m11 - m00 - m22, m22 - m00 - m11) for all four branch roots at once
        const __m128 diagonal = _mm_blend_ps(_mm_blend_ps(row0, row1, 0x2), row2, 0x4);
        const float m00       = _mm_cvtss_f32(diagonal);
        const float m11       = _mm_cvtss_f32(simd::splat<1>(diagonal));
        const float m22       = _mm_cvtss_f32(simd::splat<2>(diagonal));
        __m128 root_args      = _mm_mul_ps(simd::splat<0>(diagonal), _mm_setr_ps(1.0f, 1.0f, -1.0f, -1.0f));
        root_args             = _mm_fmadd_ps(simd::splat<1>(diagonal), _mm_setr_ps(1.0f, -1.0f, 1.0f, -1.0f), root_args);
        root_args             = _mm_fmadd_ps(simd::splat<2>(diagonal), _mm_setr_ps(1.0f, -1.0f, -1.0f, 1.0f), root_args);
        const __m128 roots    = _mm_sqrt_ps(_mm_add_ps(root_args, _mm_set1_ps(1.0f)));
        const float trace     = _mm_cvtss_f32(root_args);

        // the root lane is scaled by 0.5, the others by 0.5 / root
        const __m128 half = _mm_set1_ps(0.5f);
        __m128 q;
        if (trace > 0.0f)
        {
            const __m128 root = simd::splat<0>(roots);
            q = _mm_mul_ps(_mm_blend_ps(diff, root, 0x8), _mm_blend_ps(_mm_div_ps(half, root), half, 0x8));
        }
        else if (m00 >= m11 && m00 >= m22)
        {
            // (root, m01 + m10, m02 + m20, m12 - m21)
            const __m128 root = simd::splat<1>(roots);
            q = _mm_blend_ps(_mm_blend_ps(_mm_permute_ps(sum, _MM_SHUFFLE(0, 1, 2, 0)), root, 0x1), simd::splat<0>(diff), 0x8);
            q = _mm_mul_ps(q, _mm_blend_ps(_mm_div_ps(half, root), half, 0x1));
        }
        else if (m11 > m22)
        {
            // (m10 + m01, root, m21 + m12, m20 - m02)
            const __m128 root = simd::splat<2>(roots);
            q = _mm_blend_ps(_mm_blend_ps(_mm_permute_ps(sum, _MM_SHUFFLE(1, 0, 1, 2)), root, 0x2), simd::splat<1>(diff), 0x8);
            q = _mm_mul_ps(q, _mm_blend_ps(_mm_div_ps(half, root), half, 0x2));
        }
        else
        {
            // (m20 + m02, m21 + m12, root, m01 - m10)
            const __m128 root = simd::splat<3>(roots);
            q = _mm_blend_ps(_mm_blend_ps(_mm_permute_ps(sum, _MM_SHUFFLE(2, 2, 0, 1)), root, 0x4), simd::splat<2>(diff), 0x8);
            q = _mm_mul_ps(q, _mm_blend_ps(_mm_div_ps(half, root), half, 0x4));
        }

        // negate all four components when w is negative
        return Quaternion(_mm_xor_ps(q, _mm_and_ps(simd::splat<3>(q), simd::sign_mask())));
    }

    string Quaternion::ToString() const
    {
        char buffer[200];
        sprintf_s(buffer, sizeof(buffer), "X:%f, Y:%f, Z:%f, W:%f", x, y, z, w);
        return string(buffer);
    }
}
