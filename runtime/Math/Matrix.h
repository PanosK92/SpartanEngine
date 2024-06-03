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
#include "Quaternion.h"
#include "Vector3.h"
#include "Vector4.h"
//=====================

namespace Spartan::Math
{
    class SP_CLASS Matrix
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
            const Matrix mRotation = CreateRotation(rotation);

            m00 = scale.x * mRotation.m00;  m01 = scale.x * mRotation.m01;  m02 = scale.x * mRotation.m02;  m03 = 0.0f;
            m10 = scale.y * mRotation.m10;  m11 = scale.y * mRotation.m11;  m12 = scale.y * mRotation.m12;  m13 = 0.0f;
            m20 = scale.z * mRotation.m20;  m21 = scale.z * mRotation.m21;  m22 = scale.z * mRotation.m22;  m23 = 0.0f;
            m30 = translation.x;            m31 = translation.y;            m32 = translation.z;            m33 = 1.0f;
        }

        ~Matrix() = default;

        // generate a matrix with row first memory alignment, we need this for compatibility reasons with imgui
        [[nodiscard]] static Matrix GenerateRowFirst(const Vector3& position, const Quaternion& rotation, const Vector3& scale)
        {
            const Matrix mRotation = CreateRotation(rotation).Transposed();

            return Matrix
            (
                scale.x * mRotation.m00, scale.y * mRotation.m01, scale.z * mRotation.m02, position.x,
                scale.x * mRotation.m10, scale.y * mRotation.m11, scale.z * mRotation.m12, position.y,
                scale.x * mRotation.m20, scale.y * mRotation.m21, scale.z * mRotation.m22, position.z,
                0.0f,                    0.0f                   , 0.0f,                    1.0f
            );
        }

        [[nodiscard]] Vector3 GetTranslation() const { return Vector3(m30, m31, m32); }

        static inline Matrix CreateTranslation(const Vector3& translation)
        {
            return Matrix(
                1, 0, 0, 0,
                0, 1, 0, 0,
                0, 0, 1, 0,
                translation.x, translation.y, translation.z, 1.0f
            );
        }

        static inline Matrix CreateRotation(const Quaternion& rotation)
        {
            const float num9 = rotation.x * rotation.x;
            const float num8 = rotation.y * rotation.y;
            const float num7 = rotation.z * rotation.z;
            const float num6 = rotation.x * rotation.y;
            const float num5 = rotation.z * rotation.w;
            const float num4 = rotation.z * rotation.x;
            const float num3 = rotation.y * rotation.w;
            const float num2 = rotation.y * rotation.z;
            const float num  = rotation.x * rotation.w;

            return Matrix(
                1.0f - (2.0f * (num8 + num7)),
                2.0f * (num6 + num5),
                2.0f * (num4 - num3),
                0.0f,
                2.0f * (num6 - num5),
                1.0f - (2.0f * (num7 + num9)),
                2.0f * (num2 + num),
                0.0f,
                2.0f * (num4 + num3),
                2.0f * (num2 - num),
                1.0f - (2.0f * (num8 + num9)),
                0.0f,
                0.0f,
                0.0f,
                0.0f,
                1.0f
            );
        }

        [[nodiscard]] Quaternion GetRotation() const
        {
            const Vector3 scale = GetScale();

            // avoid division by zero (we'll divide to remove scaling)
            if (scale.x == 0.0f || scale.y == 0.0f || scale.z == 0.0f)
                return Quaternion::Identity;

            // extract rotation and remove scaling
            Matrix normalized;
            normalized.m00 = m00 / scale.x; normalized.m01 = m01 / scale.x; normalized.m02 = m02 / scale.x; normalized.m03 = 0.0f;
            normalized.m10 = m10 / scale.y; normalized.m11 = m11 / scale.y; normalized.m12 = m12 / scale.y; normalized.m13 = 0.0f;
            normalized.m20 = m20 / scale.z; normalized.m21 = m21 / scale.z; normalized.m22 = m22 / scale.z; normalized.m23 = 0.0f;
            normalized.m30 = 0;             normalized.m31 = 0;             normalized.m32 = 0;             normalized.m33 = 1.0f;

            return RotationMatrixToQuaternion(normalized);
        }

        static inline Quaternion RotationMatrixToQuaternion(const Matrix& mRot)
        {
            Quaternion quaternion;
            float sqrt;
            float half;
            const float scale = mRot.m00 + mRot.m11 + mRot.m22;

            if (scale > 0.0f)
            {
                sqrt = Helper::Sqrt(scale + 1.0f);
                quaternion.w = sqrt * 0.5f;
                sqrt = 0.5f / sqrt;

                quaternion.x = (mRot.m12 - mRot.m21) * sqrt;
                quaternion.y = (mRot.m20 - mRot.m02) * sqrt;
                quaternion.z = (mRot.m01 - mRot.m10) * sqrt;

                return quaternion;
            }
            if ((mRot.m00 >= mRot.m11) && (mRot.m00 >= mRot.m22))
            {
                sqrt = Helper::Sqrt(1.0f + mRot.m00 - mRot.m11 - mRot.m22);
                half = 0.5f / sqrt;

                quaternion.x = 0.5f * sqrt;
                quaternion.y = (mRot.m01 + mRot.m10) * half;
                quaternion.z = (mRot.m02 + mRot.m20) * half;
                quaternion.w = (mRot.m12 - mRot.m21) * half;

                return quaternion;
            }
            if (mRot.m11 > mRot.m22)
            {
                sqrt = Helper::Sqrt(1.0f + mRot.m11 - mRot.m00 - mRot.m22);
                half = 0.5f / sqrt;

                quaternion.x = (mRot.m10 + mRot.m01) * half;
                quaternion.y = 0.5f * sqrt;
                quaternion.z = (mRot.m21 + mRot.m12) * half;
                quaternion.w = (mRot.m20 - mRot.m02) * half;

                return quaternion;
            }
            sqrt = Helper::Sqrt(1.0f + mRot.m22 - mRot.m00 - mRot.m11);
            half = 0.5f / sqrt;

            quaternion.x = (mRot.m20 + mRot.m02) * half;
            quaternion.y = (mRot.m21 + mRot.m12) * half;
            quaternion.z = 0.5f * sqrt;
            quaternion.w = (mRot.m01 - mRot.m10) * half;

            return quaternion;
        }

        [[nodiscard]] Vector3 GetScale() const
        {
            const int xs = (Helper::Sign(m00 * m01 * m02 * m03) < 0) ? -1 : 1;
            const int ys = (Helper::Sign(m10 * m11 * m12 * m13) < 0) ? -1 : 1;
            const int zs = (Helper::Sign(m20 * m21 * m22 * m23) < 0) ? -1 : 1;

            return Vector3(
                static_cast<float>(xs) * Helper::Sqrt(m00 * m00 + m01 * m01 + m02 * m02),
                static_cast<float>(ys) * Helper::Sqrt(m10 * m10 + m11 * m11 + m12 * m12),
                static_cast<float>(zs) * Helper::Sqrt(m20 * m20 + m21 * m21 + m22 * m22)
            );
        }

        static inline Matrix CreateScale(float scale) { return CreateScale(scale, scale, scale); }
        static inline Matrix CreateScale(const Vector3& scale) { return CreateScale(scale.x, scale.y, scale.z); }
        static inline Matrix CreateScale(float scaleX, float scaleY, float ScaleZ)
        {
            return Matrix(
                scaleX, 0, 0, 0,
                0, scaleY, 0, 0,
                0, 0, ScaleZ, 0,
                0, 0, 0, 1
            );
        }

        static inline Matrix CreateLookAtLH(const Vector3& position, const Vector3& target, const Vector3& up)
        {
            const Vector3 zAxis = Vector3::Normalize(target - position);
            const Vector3 xAxis = Vector3::Normalize(Vector3::Cross(up, zAxis));
            const Vector3 yAxis = Vector3::Cross(zAxis, xAxis);

            return Matrix(
                xAxis.x, yAxis.x, zAxis.x, 0,
                xAxis.y, yAxis.y, zAxis.y, 0,
                xAxis.z, yAxis.z, zAxis.z, 0,
                -Vector3::Dot(xAxis, position), -Vector3::Dot(yAxis, position), -Vector3::Dot(zAxis, position), 1.0f
            );
        }

        static inline Matrix CreateOrthographicLH(float width, float height, float zNearPlane, float zFarPlane)
        {
            return Matrix(
                2 / width, 0, 0, 0,
                0, 2 / height, 0, 0,
                0, 0, 1 / (zFarPlane - zNearPlane), 0,
                0, 0, zNearPlane / (zNearPlane - zFarPlane), 1
            );
        }

        static inline Matrix CreateOrthoOffCenterLH(float left, float right, float bottom, float top, float zNearPlane, float zFarPlane)
        {
            return Matrix(
                2 / (right - left), 0, 0, 0,
                0, 2 / (top - bottom), 0, 0,
                0, 0, 1 / (zFarPlane - zNearPlane), 0,
                (left + right) / (left - right), (top + bottom) / (bottom - top), zNearPlane / (zNearPlane - zFarPlane), 1
            );
        }

        static inline Matrix CreatePerspectiveFieldOfViewLH(float fov_y_radians, float aspect_ratio, float near_plane, float far_plane)
        {
            const float scale_x = Helper::CotF(fov_y_radians / 2);
            const float scale_y = scale_x / aspect_ratio;

            return Matrix(
                scale_y, 0,       0,                                                  0,
                0,       scale_x, 0,                                                  0,
                0,       0,       far_plane / (far_plane - near_plane),               1,
                0,       0,       -near_plane * far_plane / (far_plane - near_plane), 0
            );
        }

        [[nodiscard]] Matrix Transposed() const { return Transpose(*this); }
        void Transpose() { *this = Transpose(*this); }
        static inline Matrix Transpose(const Matrix& matrix)
        {
            return Matrix(
                matrix.m00, matrix.m10, matrix.m20, matrix.m30,
                matrix.m01, matrix.m11, matrix.m21, matrix.m31,
                matrix.m02, matrix.m12, matrix.m22, matrix.m32,
                matrix.m03, matrix.m13, matrix.m23, matrix.m33
            );
        }

        [[nodiscard]] Matrix Inverted() const { return Invert(*this); }
        static inline Matrix Invert(const Matrix& matrix)
        {
            float v0 = matrix.m20 * matrix.m31 - matrix.m21 * matrix.m30;
            float v1 = matrix.m20 * matrix.m32 - matrix.m22 * matrix.m30;
            float v2 = matrix.m20 * matrix.m33 - matrix.m23 *matrix.m30;
            float v3 = matrix.m21 * matrix.m32 - matrix.m22 * matrix.m31;
            float v4 = matrix.m21 * matrix.m33 - matrix.m23 * matrix.m31;
            float v5 = matrix.m22 * matrix.m33 - matrix.m23 * matrix.m32;

            float i00 = (v5 * matrix.m11 - v4 * matrix.m12 + v3 * matrix.m13);
            float i10 = -(v5 * matrix.m10 - v2 * matrix.m12 + v1 * matrix.m13);
            float i20 = (v4 * matrix.m10 - v2 * matrix.m11 + v0 * matrix.m13);
            float i30 = -(v3 * matrix.m10 - v1 * matrix.m11 + v0 * matrix.m12);

            const float invDet = 1.0f / (i00 * matrix.m00 + i10 * matrix.m01 + i20 * matrix.m02 + i30 * matrix.m03);

            i00 *= invDet;
            i10 *= invDet;
            i20 *= invDet;
            i30 *= invDet;

            const float i01 = -(v5 * matrix.m01 - v4 * matrix.m02 + v3 * matrix.m03) * invDet;
            const float i11 = (v5 * matrix.m00 - v2 * matrix.m02 + v1 * matrix.m03) * invDet;
            const float i21 = -(v4 * matrix.m00 - v2 * matrix.m01 + v0 * matrix.m03) * invDet;
            const float i31 = (v3 * matrix.m00 - v1 * matrix.m01 + v0 * matrix.m02) * invDet;

            v0 = matrix.m10 * matrix.m31 - matrix.m11 * matrix.m30;
            v1 = matrix.m10 * matrix.m32 - matrix.m12 * matrix.m30;
            v2 = matrix.m10 * matrix.m33 - matrix.m13 * matrix.m30;
            v3 = matrix.m11 * matrix.m32 - matrix.m12 * matrix.m31;
            v4 = matrix.m11 * matrix.m33 - matrix.m13 * matrix.m31;
            v5 = matrix.m12 * matrix.m33 - matrix.m13 * matrix.m32;

            const float i02 = (v5 * matrix.m01 - v4 * matrix.m02 + v3 * matrix.m03) * invDet;
            const float i12 = -(v5 * matrix.m00 - v2 * matrix.m02 + v1 * matrix.m03) * invDet;
            const float i22 = (v4 * matrix.m00 - v2 * matrix.m01 + v0 * matrix.m03) * invDet;
            const float i32 = -(v3 * matrix.m00 - v1 * matrix.m01 + v0 * matrix.m02) * invDet;

            v0 = matrix.m21 * matrix.m10 - matrix.m20 * matrix.m11;
            v1 = matrix.m22 * matrix.m10 - matrix.m20 * matrix.m12;
            v2 = matrix.m23 * matrix.m10 - matrix.m20 * matrix.m13;
            v3 = matrix.m22 * matrix.m11 - matrix.m21 * matrix.m12;
            v4 = matrix.m23 * matrix.m11 - matrix.m21 * matrix.m13;
            v5 = matrix.m23 * matrix.m12 - matrix.m22 * matrix.m13;

            const float i03 = -(v5 * matrix.m01 - v4 * matrix.m02 + v3 * matrix.m03) * invDet;
            const float i13 = (v5 * matrix.m00 - v2 * matrix.m02 + v1 * matrix.m03) * invDet;
            const float i23 = -(v4 * matrix.m00 - v2 * matrix.m01 + v0 * matrix.m03) * invDet;
            const float i33 = (v3 * matrix.m00 - v1 * matrix.m01 + v0 * matrix.m02) * invDet;

            return Matrix(
                i00, i01, i02, i03,
                i10, i11, i12, i13,
                i20, i21, i22, i23,
                i30, i31, i32, i33);
        }

        void Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation) const
        {
            translation = GetTranslation();
            scale        = GetScale();
            rotation    = GetRotation();
        }

        void SetIdentity()
        {
            m00 = 1; m01 = 0; m02 = 0; m03 = 0;
            m10 = 0; m11 = 1; m12 = 0; m13 = 0;
            m20 = 0; m21 = 0; m22 = 1; m23 = 0;
            m30 = 0; m31 = 0; m32 = 0; m33 = 1;
        }

        Matrix operator*(const Matrix& rhs) const
        {
            return Matrix(
                m00 * rhs.m00 + m01 * rhs.m10 + m02 * rhs.m20 + m03 * rhs.m30,
                m00 * rhs.m01 + m01 * rhs.m11 + m02 * rhs.m21 + m03 * rhs.m31,
                m00 * rhs.m02 + m01 * rhs.m12 + m02 * rhs.m22 + m03 * rhs.m32,
                m00 * rhs.m03 + m01 * rhs.m13 + m02 * rhs.m23 + m03 * rhs.m33,
                m10 * rhs.m00 + m11 * rhs.m10 + m12 * rhs.m20 + m13 * rhs.m30,
                m10 * rhs.m01 + m11 * rhs.m11 + m12 * rhs.m21 + m13 * rhs.m31,
                m10 * rhs.m02 + m11 * rhs.m12 + m12 * rhs.m22 + m13 * rhs.m32,
                m10 * rhs.m03 + m11 * rhs.m13 + m12 * rhs.m23 + m13 * rhs.m33,
                m20 * rhs.m00 + m21 * rhs.m10 + m22 * rhs.m20 + m23 * rhs.m30,
                m20 * rhs.m01 + m21 * rhs.m11 + m22 * rhs.m21 + m23 * rhs.m31,
                m20 * rhs.m02 + m21 * rhs.m12 + m22 * rhs.m22 + m23 * rhs.m32,
                m20 * rhs.m03 + m21 * rhs.m13 + m22 * rhs.m23 + m23 * rhs.m33,
                m30 * rhs.m00 + m31 * rhs.m10 + m32 * rhs.m20 + m33 * rhs.m30,
                m30 * rhs.m01 + m31 * rhs.m11 + m32 * rhs.m21 + m33 * rhs.m31,
                m30 * rhs.m02 + m31 * rhs.m12 + m32 * rhs.m22 + m33 * rhs.m32,
                m30 * rhs.m03 + m31 * rhs.m13 + m32 * rhs.m23 + m33 * rhs.m33
            );
        }

        void operator*=(const Matrix& rhs) { (*this) = (*this) * rhs; }

        Vector3 operator*(const Vector3& rhs) const
        {
            float x = (rhs.x * m00) + (rhs.y * m10) + (rhs.z * m20) + m30;
            float y = (rhs.x * m01) + (rhs.y * m11) + (rhs.z * m21) + m31;
            float z = (rhs.x * m02) + (rhs.y * m12) + (rhs.z * m22) + m32;
            float w = (rhs.x * m03) + (rhs.y * m13) + (rhs.z * m23) + m33;

            // to ensure the perspective divide, divide each component by w
            if (w != 1.0f)
            {
                x /= w;
                y /= w;
                z /= w;
            }

            return Vector3(x, y, z);
        }

        Vector4 operator*(const Vector4& rhs) const
        {
            return Vector4
            (
                (rhs.x * m00) + (rhs.y * m10) + (rhs.z * m20) + (rhs.w * m30),
                (rhs.x * m01) + (rhs.y * m11) + (rhs.z * m21) + (rhs.w * m31),
                (rhs.x * m02) + (rhs.y * m12) + (rhs.z * m22) + (rhs.w * m32),
                (rhs.x * m03) + (rhs.y * m13) + (rhs.z * m23) + (rhs.w * m33)
            );
        }

        bool operator==(const Matrix& rhs) const
        {
            const float* data_left    = Data();
            const float* data_right    = rhs.Data();

            for (unsigned i = 0; i < 16; ++i)
            {
                if (data_left[i] != data_right[i])
                    return false;
            }

            return true;
        }

        bool operator!=(const Matrix& rhs) const { return !(*this == rhs); }

        // Test for equality with another matrix with epsilon.
        bool Equals(const Matrix& rhs)
        {
            const float* data_left  = Data();
            const float* data_right = rhs.Data();

            for (unsigned i = 0; i < 16; ++i)
            {
                if (!Helper::Equals(data_left[i], data_right[i]))
                    return false;
            }

            return true;
        }

        [[nodiscard]] const float* Data() const { return &m00; }
        [[nodiscard]] std::string ToString() const;

        // column-major memory representation
        float m00 = 0.0f, m10 = 0.0f, m20 = 0.0f, m30 = 0.0f;
        float m01 = 0.0f, m11 = 0.0f, m21 = 0.0f, m31 = 0.0f;
        float m02 = 0.0f, m12 = 0.0f, m22 = 0.0f, m32 = 0.0f;
        float m03 = 0.0f, m13 = 0.0f, m23 = 0.0f, m33 = 0.0f;
        // note: directx compiler assumes column-major as a default,
        // we go with it so that we can map directly map matrices to the gpu

        static const Matrix Identity;
    };

    // reverse order operators
    inline SP_CLASS Vector3 operator*(const Vector3& lhs, const Matrix& rhs) { return rhs * lhs; }
    inline SP_CLASS Vector4 operator*(const Vector4& lhs, const Matrix& rhs) { return rhs * lhs; }
}
