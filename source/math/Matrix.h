/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ==========
#include "Quaternion.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Simd.h"
//=====================

namespace spartan::math
{
    // directx style matrix, column-major layout, constructor accepts values in row-major (but stores them in column-major) for human readability
    class Matrix
    {
    public:
        Matrix()
        {
            SetIdentity();
        }

        Matrix(const Matrix& rhs) = default;

        Matrix(
            float m00, float m01, float m02, float m03,
            float m10, float m11, float m12, float m13,
            float m20, float m21, float m22, float m23,
            float m30, float m31, float m32, float m33)
        {
            this->m00 = m00; this->m01 = m01; this->m02 = m02; this->m03 = m03;
            this->m10 = m10; this->m11 = m11; this->m12 = m12; this->m13 = m13;
            this->m20 = m20; this->m21 = m21; this->m22 = m22; this->m23 = m23;
            this->m30 = m30; this->m31 = m31; this->m32 = m32; this->m33 = m33;
        }

        Matrix(const Vector3& translation, const Quaternion& rotation, const Vector3& scale)
        {
            __m128 r0, r1, r2;
            RotationColumns(rotation, r0, r1, r2);

            const __m128 s = scale.Simd();
            const __m128 t = translation.Simd();
            SetColumns(
                _mm_blend_ps(_mm_mul_ps(r0, s), simd::splat<0>(t), 0x8),
                _mm_blend_ps(_mm_mul_ps(r1, s), simd::splat<1>(t), 0x8),
                _mm_blend_ps(_mm_mul_ps(r2, s), simd::splat<2>(t), 0x8),
                _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f)
            );
        }

        Matrix(const float m[16])
        {
            // row-major to column-major
            m00 = m[0];  m01 = m[1];  m02 = m[2];  m03 = m[3];
            m10 = m[4];  m11 = m[5];  m12 = m[6];  m13 = m[7];
            m20 = m[8];  m21 = m[9];  m22 = m[10]; m23 = m[11];
            m30 = m[12]; m31 = m[13]; m32 = m[14]; m33 = m[15];
        }

        ~Matrix() = default;

        // column i is (m0i, m1i, m2i, m3i), contiguous in memory
        static Matrix FromColumns(const __m128 c0, const __m128 c1, const __m128 c2, const __m128 c3)
        {
            Matrix m;
            m.SetColumns(c0, c1, c2, c3);
            return m;
        }

        [[nodiscard]] __m128 Column(const uint32_t index) const { return simd::load4(&m00 + index * 4); }

        void SetColumns(const __m128 c0, const __m128 c1, const __m128 c2, const __m128 c3)
        {
            float* data = &m00;
            simd::store4(data + 0,  c0);
            simd::store4(data + 4,  c1);
            simd::store4(data + 8,  c2);
            simd::store4(data + 12, c3);
        }

        [[nodiscard]] Vector3 GetTranslation() const { return Vector3(m30, m31, m32); }

        static Matrix CreateTranslation(const Vector3& translation)
        {
            const __m128 t = translation.Simd();
            return FromColumns(
                _mm_blend_ps(_mm_setr_ps(1.0f, 0.0f, 0.0f, 0.0f), simd::splat<0>(t), 0x8),
                _mm_blend_ps(_mm_setr_ps(0.0f, 1.0f, 0.0f, 0.0f), simd::splat<1>(t), 0x8),
                _mm_blend_ps(_mm_setr_ps(0.0f, 0.0f, 1.0f, 0.0f), simd::splat<2>(t), 0x8),
                _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f)
            );
        }

        static Matrix CreateRotation(const Quaternion& rotation)
        {
            __m128 c0, c1, c2;
            RotationColumns(rotation, c0, c1, c2);
            return FromColumns(c0, c1, c2, _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f));
        }

        [[nodiscard]] Quaternion GetRotation() const
        {
            const Vector3 scale = GetScale();
            if (scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f)
            {
                return Quaternion::Identity;
            }

            // divide each row by its scale, lane i of a column belongs to row i
            const __m128 scale_inv = _mm_div_ps(_mm_set1_ps(1.0f), simd::pad_one<0x8>(scale.Simd()));
            __m128 row0 = _mm_mul_ps(Column(0), scale_inv);
            __m128 row1 = _mm_mul_ps(Column(1), scale_inv);
            __m128 row2 = _mm_mul_ps(Column(2), scale_inv);
            __m128 row3 = Column(3);
            _MM_TRANSPOSE4_PS(row0, row1, row2, row3);

            return Quaternion::FromRotationRows(row0, row1, row2);
        }

        static Quaternion RotationMatrixToQuaternion(const Matrix& rotation_matrix)
        {
            __m128 row0 = rotation_matrix.Column(0);
            __m128 row1 = rotation_matrix.Column(1);
            __m128 row2 = rotation_matrix.Column(2);
            __m128 row3 = rotation_matrix.Column(3);
            _MM_TRANSPOSE4_PS(row0, row1, row2, row3);

            return Quaternion::FromRotationRows(row0, row1, row2);
        }

        [[nodiscard]] Vector3 GetScale() const
        {
            // columns are contiguous, sum their squares lane-wise to get all three row lengths at once
            const __m128 c0       = Column(0);
            const __m128 c1       = Column(1);
            const __m128 c2       = Column(2);
            const __m128 c3       = Column(3);
            const __m128 sums     = _mm_fmadd_ps(c2, c2, _mm_fmadd_ps(c1, c1, _mm_mul_ps(c0, c0)));
            const __m128 products = _mm_mul_ps(_mm_mul_ps(_mm_mul_ps(c0, c1), c2), c3);
            const __m128 signs    = _mm_and_ps(_mm_cmplt_ps(products, _mm_setzero_ps()), simd::sign_mask());
            return Vector3(_mm_xor_ps(_mm_sqrt_ps(sums), signs));
        }

        static Matrix CreateScale(float scale) { return CreateScale(scale, scale, scale); }
        static Matrix CreateScale(const Vector3& scale) { return CreateScale(scale.x, scale.y, scale.z); }
        static Matrix CreateScale(float scale_x, float scale_y, float scale_z)
        {
            const __m128 s    = _mm_setr_ps(scale_x, scale_y, scale_z, 1.0f);
            const __m128 zero = _mm_setzero_ps();
            return FromColumns(_mm_blend_ps(zero, s, 0x1), _mm_blend_ps(zero, s, 0x2), _mm_blend_ps(zero, s, 0x4), _mm_blend_ps(zero, s, 0x8));
        }

        static Matrix CreateLookAtLH(const Vector3& position, const Vector3& target, const Vector3& up)
        {
            return CreateLookToLH(position, target - position, up);
        }

        // prefer this over CreateLookAtLH far from the origin, position + direction rounds to the float spacing
        // there (~0.5 mm at 5 km) and the recovered direction wobbles as the camera moves
        static Matrix CreateLookToLH(const Vector3& position, const Vector3& direction, const Vector3& up)
        {
            const __m128 p      = position.Simd();
            const __m128 axis_z = Vector3::Normalize(direction).Simd();
            const __m128 axis_x = Vector3(simd::cross3(up.Simd(), axis_z)).Normalized().Simd();
            const __m128 axis_y = simd::cross3(axis_z, axis_x);

            // each column is an axis with the negated eye-space translation in w
            return FromColumns(
                _mm_blend_ps(axis_x, simd::negate(simd::dot3_splat(axis_x, p)), 0x8),
                _mm_blend_ps(axis_y, simd::negate(simd::dot3_splat(axis_y, p)), 0x8),
                _mm_blend_ps(axis_z, simd::negate(simd::dot3_splat(axis_z, p)), 0x8),
                _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f)
            );
        }

        static Matrix CreateOrthographicLH(float width, float height, float z_near, float z_far)
        {
            const __m128 d    = _mm_div_ps(_mm_setr_ps(2.0f, 2.0f, 1.0f, z_near), _mm_setr_ps(width, height, z_far - z_near, z_near - z_far));
            const __m128 zero = _mm_setzero_ps();
            return FromColumns(_mm_blend_ps(zero, d, 0x1), _mm_blend_ps(zero, d, 0x2), _mm_blend_ps(zero, d, 0xC), _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f));
        }

        static Matrix CreateOrthoOffCenterLH(float left, float right, float bottom, float top, float z_near, float z_far)
        {
            // scales in the low half, translations in the high half, one division for all of them
            const __m256 d = _mm256_div_ps(
                _mm256_setr_ps(2.0f, 2.0f, 1.0f, z_near, left + right, top + bottom, 0.0f, 0.0f),
                _mm256_setr_ps(right - left, top - bottom, z_far - z_near, z_near - z_far, left - right, bottom - top, 1.0f, 1.0f));
            const __m128 scale       = _mm256_castps256_ps128(d);
            const __m128 translation = _mm256_extractf128_ps(d, 1);
            const __m128 zero        = _mm_setzero_ps();
            return FromColumns(
                _mm_blend_ps(_mm_blend_ps(zero, scale, 0x1), simd::splat<0>(translation), 0x8),
                _mm_blend_ps(_mm_blend_ps(zero, scale, 0x2), simd::splat<1>(translation), 0x8),
                _mm_blend_ps(zero, scale, 0xC),
                _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f)
            );
        }

        static Matrix CreatePerspectiveFieldOfViewLH(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane)
        {
            // (f / aspect, f, far / range, -near * far / range) with f = 1 / tan(fov / 2) = cos / sin
            __m128 sin, cos;
            simd::sin_cos(_mm_set1_ps(fov_y_radians * 0.5f), sin, cos);
            const __m128 range = _mm_set1_ps(far_plane - near_plane);
            const __m128 d     = _mm_div_ps(
                _mm_blend_ps(cos, _mm_setr_ps(0.0f, 0.0f, far_plane, -near_plane * far_plane), 0xC),
                _mm_blend_ps(_mm_mul_ps(sin, _mm_setr_ps(aspect_ratio, 1.0f, 1.0f, 1.0f)), range, 0xC));
            const __m128 zero         = _mm_setzero_ps();
            return FromColumns(_mm_blend_ps(zero, d, 0x1), _mm_blend_ps(zero, d, 0x2), _mm_blend_ps(zero, d, 0xC), _mm_setr_ps(0.0f, 0.0f, 1.0f, 0.0f));
        }

        [[nodiscard]] Matrix Transposed() const { return Transpose(*this); }
        void Transpose() { *this = Transpose(*this); }
        static Matrix Transpose(const Matrix& matrix)
        {
            __m128 c0 = matrix.Column(0);
            __m128 c1 = matrix.Column(1);
            __m128 c2 = matrix.Column(2);
            __m128 c3 = matrix.Column(3);
            _MM_TRANSPOSE4_PS(c0, c1, c2, c3);
            return FromColumns(c0, c1, c2, c3);
        }

        // block-wise inverse on 2x2 sub-matrices, the layout does not matter since inverse(transpose(m)) == transpose(inverse(m))
        [[nodiscard]] Matrix Inverted() const { return Invert(*this); }
        static inline Matrix Invert(const Matrix& matrix)
        {
            const __m128 r0 = matrix.Column(0);
            const __m128 r1 = matrix.Column(1);
            const __m128 r2 = matrix.Column(2);
            const __m128 r3 = matrix.Column(3);

            // sub-matrices: | a b |
            //               | c d |
            const __m128 a = _mm_movelh_ps(r0, r1);
            const __m128 b = _mm_movehl_ps(r1, r0);
            const __m128 c = _mm_movelh_ps(r2, r3);
            const __m128 d = _mm_movehl_ps(r3, r2);

            // (|a|, |b|, |c|, |d|)
            const __m128 det_sub = _mm_fmsub_ps(
                _mm_shuffle_ps(r0, r2, _MM_SHUFFLE(2, 0, 2, 0)), _mm_shuffle_ps(r1, r3, _MM_SHUFFLE(3, 1, 3, 1)),
                _mm_mul_ps(_mm_shuffle_ps(r0, r2, _MM_SHUFFLE(3, 1, 3, 1)), _mm_shuffle_ps(r1, r3, _MM_SHUFFLE(2, 0, 2, 0))));
            const __m128 det_a = simd::splat<0>(det_sub);
            const __m128 det_b = simd::splat<1>(det_sub);
            const __m128 det_c = simd::splat<2>(det_sub);
            const __m128 det_d = simd::splat<3>(det_sub);

            const __m128 d_c = simd::mat2_adj_mul(d, c);
            const __m128 a_b = simd::mat2_adj_mul(a, b);

            // adjugates of the inverse blocks
            __m128 x_ = _mm_sub_ps(_mm_mul_ps(det_d, a), simd::mat2_mul(b, d_c));
            __m128 w_ = _mm_sub_ps(_mm_mul_ps(det_a, d), simd::mat2_mul(c, a_b));
            __m128 y_ = _mm_sub_ps(_mm_mul_ps(det_b, c), simd::mat2_mul_adj(d, a_b));
            __m128 z_ = _mm_sub_ps(_mm_mul_ps(det_c, b), simd::mat2_mul_adj(a, d_c));

            // |m| = |a| |d| + |b| |c| - trace((a# b) (d# c))
            __m128 trace = _mm_mul_ps(a_b, _mm_permute_ps(d_c, _MM_SHUFFLE(3, 1, 2, 0)));
            trace        = _mm_hadd_ps(trace, trace);
            trace        = _mm_hadd_ps(trace, trace);
            const __m128 det = _mm_sub_ps(_mm_fmadd_ps(det_b, det_c, _mm_mul_ps(det_a, det_d)), trace);
            if (std::isnan(_mm_cvtss_f32(det)))
            {
                return Matrix::Identity;
            }

            const __m128 det_inv = _mm_div_ps(_mm_setr_ps(1.0f, -1.0f, -1.0f, 1.0f), det);
            x_ = _mm_mul_ps(x_, det_inv);
            y_ = _mm_mul_ps(y_, det_inv);
            z_ = _mm_mul_ps(z_, det_inv);
            w_ = _mm_mul_ps(w_, det_inv);

            return FromColumns(
                _mm_shuffle_ps(x_, y_, _MM_SHUFFLE(1, 3, 1, 3)),
                _mm_shuffle_ps(x_, y_, _MM_SHUFFLE(0, 2, 0, 2)),
                _mm_shuffle_ps(z_, w_, _MM_SHUFFLE(1, 3, 1, 3)),
                _mm_shuffle_ps(z_, w_, _MM_SHUFFLE(0, 2, 0, 2))
            );
        }

        void Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation) const
        {
            translation = GetTranslation();
            scale       = GetScale();
            rotation    = GetRotation();
        }

        void SetIdentity()
        {
            SetColumns(_mm_setr_ps(1.0f, 0.0f, 0.0f, 0.0f), _mm_setr_ps(0.0f, 1.0f, 0.0f, 0.0f), _mm_setr_ps(0.0f, 0.0f, 1.0f, 0.0f), _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f));
        }

        Matrix operator*(const Matrix& rhs) const
        {
            const __m128 l0 = Column(0);
            const __m128 l1 = Column(1);
            const __m128 l2 = Column(2);
            const __m128 l3 = Column(3);

            // each result column is the left columns weighted by one right column, summed as a tree to keep the dependency chain short
            const auto combine = [&](const __m128 r)
            {
                const __m128 low  = _mm_fmadd_ps(l1, simd::splat<1>(r), _mm_mul_ps(l0, simd::splat<0>(r)));
                const __m128 high = _mm_fmadd_ps(l3, simd::splat<3>(r), _mm_mul_ps(l2, simd::splat<2>(r)));
                return _mm_add_ps(low, high);
            };

            return FromColumns(combine(rhs.Column(0)), combine(rhs.Column(1)), combine(rhs.Column(2)), combine(rhs.Column(3)));
        }

        void operator*=(const Matrix& rhs) { (*this) = (*this) * rhs; }

        Vector3 operator*(const Vector3& rhs) const
        {
            // rows for row-vector multiplication
            __m128 row0 = Column(0);
            __m128 row1 = Column(1);
            __m128 row2 = Column(2);
            __m128 row3 = Column(3);
            _MM_TRANSPOSE4_PS(row0, row1, row2, row3);

            // row0 * x + row1 * y + row2 * z + row3
            __m128 result = _mm_fmadd_ps(row0, _mm_broadcast_ss(&rhs.x), row3);
            result        = _mm_fmadd_ps(row1, _mm_broadcast_ss(&rhs.y), result);
            result        = _mm_fmadd_ps(row2, _mm_broadcast_ss(&rhs.z), result);

            // perspective divide if needed
            const __m128 w = simd::splat<3>(result);
            if (_mm_cvtss_f32(w) != 1.0f)
            {
                result = _mm_div_ps(result, w);
            }

            return Vector3(result);
        }

        Vector4 operator*(const Vector4& rhs) const
        {
            // rows for row-vector multiplication
            __m128 row0 = Column(0);
            __m128 row1 = Column(1);
            __m128 row2 = Column(2);
            __m128 row3 = Column(3);
            _MM_TRANSPOSE4_PS(row0, row1, row2, row3);

            // row0 * x + row1 * y + row2 * z + row3 * w
            __m128 result = _mm_mul_ps(row0, _mm_broadcast_ss(&rhs.x));
            result        = _mm_fmadd_ps(row1, _mm_broadcast_ss(&rhs.y), result);
            result        = _mm_fmadd_ps(row2, _mm_broadcast_ss(&rhs.z), result);
            result        = _mm_fmadd_ps(row3, _mm_broadcast_ss(&rhs.w), result);

            return Vector4(result);
        }

        bool operator==(const Matrix& rhs) const
        {
            const __m256 cmp01 = _mm256_cmp_ps(_mm256_loadu_ps(&m00), _mm256_loadu_ps(&rhs.m00), _CMP_EQ_OQ);
            const __m256 cmp23 = _mm256_cmp_ps(_mm256_loadu_ps(&m02), _mm256_loadu_ps(&rhs.m02), _CMP_EQ_OQ);
            return _mm256_movemask_ps(_mm256_and_ps(cmp01, cmp23)) == 0xFF;
        }

        bool operator!=(const Matrix& rhs) const { return !(*this == rhs); }

        [[nodiscard]] const float* Data() const { return &m00; }
        [[nodiscard]] std::string ToString() const;

        [[nodiscard]] bool IsNaN() const
        {
            const __m256 c01 = _mm256_loadu_ps(&m00);
            const __m256 c23 = _mm256_loadu_ps(&m02);
            return _mm256_movemask_ps(_mm256_or_ps(_mm256_cmp_ps(c01, c01, _CMP_UNORD_Q), _mm256_cmp_ps(c23, c23, _CMP_UNORD_Q))) != 0;
        }

        [[nodiscard]] bool IsFinite() const
        {
            // nan fails the ordered compare, so it counts as not finite
            const __m256 sign = _mm256_set1_ps(-0.0f);
            const __m256 inf  = _mm256_set1_ps(std::numeric_limits<float>::infinity());
            const __m256 c01  = _mm256_cmp_ps(_mm256_andnot_ps(sign, _mm256_loadu_ps(&m00)), inf, _CMP_LT_OQ);
            const __m256 c23  = _mm256_cmp_ps(_mm256_andnot_ps(sign, _mm256_loadu_ps(&m02)), inf, _CMP_LT_OQ);
            return _mm256_movemask_ps(_mm256_and_ps(c01, c23)) == 0xFF;
        }

        float m00 = 0.0f, m10 = 0.0f, m20 = 0.0f, m30 = 0.0f;
        float m01 = 0.0f, m11 = 0.0f, m21 = 0.0f, m31 = 0.0f;
        float m02 = 0.0f, m12 = 0.0f, m22 = 0.0f, m32 = 0.0f;
        float m03 = 0.0f, m13 = 0.0f, m23 = 0.0f, m33 = 0.0f;

        static const Matrix Identity;

    private:
        // the three rotation columns of a unit quaternion, w lanes are zero
        static void RotationColumns(const Quaternion& rotation, __m128& c0, __m128& c1, __m128& c2)
        {
            const __m128 q  = rotation.Simd();
            const __m128 q2 = _mm_add_ps(q, q);

            // diagonal: (1 - 2(yy + zz), 1 - 2(xx + zz), 1 - 2(xx + yy), 0)
            const __m128 squares  = _mm_mul_ps(q, q2);
            const __m128 diagonal = _mm_blend_ps(
                _mm_sub_ps(_mm_sub_ps(_mm_set1_ps(1.0f), _mm_permute_ps(squares, _MM_SHUFFLE(3, 0, 0, 1))), _mm_permute_ps(squares, _MM_SHUFFLE(3, 1, 2, 2))),
                _mm_setzero_ps(), 0x8);

            // (2xz, 2xy, 2yz) and (2wy, 2wz, 2wx)
            const __m128 products = _mm_mul_ps(_mm_permute_ps(q, _MM_SHUFFLE(3, 1, 0, 0)), _mm_permute_ps(q2, _MM_SHUFFLE(3, 2, 1, 2)));
            const __m128 w_terms  = _mm_mul_ps(simd::splat<3>(q2), _mm_permute_ps(q, _MM_SHUFFLE(3, 0, 2, 1)));
            const __m128 sums     = _mm_add_ps(products, w_terms); // (2(xz + wy), 2(xy + wz), 2(yz + wx))
            const __m128 diffs    = _mm_sub_ps(products, w_terms); // (2(xz - wy), 2(xy - wz), 2(yz - wx))

            // c0 = (diagonal.x, diffs.y, sums.x, 0)
            // c1 = (sums.y, diagonal.y, diffs.z, 0)
            // c2 = (diffs.x, sums.z, diagonal.z, 0)
            c0 = _mm_blend_ps(_mm_blend_ps(diagonal, diffs, 0x2), simd::splat<0>(sums), 0x4);
            c1 = _mm_blend_ps(_mm_blend_ps(diagonal, simd::splat<1>(sums), 0x1), diffs, 0x4);
            c2 = _mm_blend_ps(_mm_blend_ps(diagonal, diffs, 0x1), simd::splat<2>(sums), 0x2);
        }
    };

    // reverse order operators
    inline Vector3 operator*(const Vector3& lhs, const Matrix& rhs) { return rhs * lhs; }
    inline Vector4 operator*(const Vector4& lhs, const Matrix& rhs) { return rhs * lhs; }
}
