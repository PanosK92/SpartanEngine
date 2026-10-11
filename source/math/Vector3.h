/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDE ==========
#include "Helper.h"
#include "Simd.h"
//====================

namespace spartan::math
{
    class Vector4;

    class Vector3
    {
    public:
        Vector3()
        {
            x = 0;
            y = 0;
            z = 0;
        }

        Vector3(const Vector3& vector) = default;

        Vector3(const Vector4& vector);

        Vector3(float x, float y, float z)
        {
            this->x = x;
            this->y = y;
            this->z = z;
        }

        Vector3(float pos[3])
        {
            this->x = pos[0];
            this->y = pos[1];
            this->z = pos[2];
        }

        Vector3(float f)
        {
            x = f;
            y = f;
            z = f;
        }

        explicit Vector3(const __m128 v)
        {
            simd::store3(&x, v);
        }

        // x, y, z in the low lanes, w is zero
        [[nodiscard]] __m128 Simd() const { return simd::load3(&x); }

        void Normalize()
        {
            const __m128 v              = Simd();
            const __m128 length_squared = simd::dot3_splat(v, v);
            const float length_squared_ = _mm_cvtss_f32(length_squared);
            if (!approximate_equals(length_squared_, 1.0f) && length_squared_ > 0.0f)
            {
                // reciprocal square root with one newton-raphson step: r' = 0.5 * r * (3 - d * r * r)
                const __m128 r       = _mm_rsqrt_ps(length_squared);
                const __m128 refined = _mm_mul_ps(_mm_mul_ps(_mm_set1_ps(0.5f), r), _mm_fnmadd_ps(length_squared, _mm_mul_ps(r, r), _mm_set1_ps(3.0f)));
                simd::store3(&x, _mm_mul_ps(v, refined));
            }
        }

        [[nodiscard]] Vector3 Normalized() const
        {
            Vector3 v = *this;
            v.Normalize();
            return v;
        }

        static Vector3 Normalize(const Vector3& v) { return v.Normalized(); }

        bool IsNormalized() const
        {
            static const float THRESH_VECTOR_NORMALIZED = 0.01f;
            return (abs(1.0f - LengthSquared()) < THRESH_VECTOR_NORMALIZED);
        }

        [[nodiscard]] static float Dot(const Vector3& v1, const Vector3& v2)
        {
            return simd::dot3(v1.Simd(), v2.Simd());
        }

        [[nodiscard]] float Dot(const Vector3& rhs) const 
        {
            return Dot(*this, rhs);
        }

        [[nodiscard]] static Vector3 Cross(const Vector3& v1, const Vector3& v2)
        {
            return Vector3(simd::cross3(v1.Simd(), v2.Simd()));
        }

        [[nodiscard]] Vector3 Cross(const Vector3& v2) const { return Cross(*this, v2); }

        [[nodiscard]] float Length() const
        {
            const __m128 v = Simd();
            return _mm_cvtss_f32(_mm_sqrt_ss(simd::dot3_splat(v, v)));
        }

        [[nodiscard]] float LengthSquared() const
        {
            const __m128 v = Simd();
            return simd::dot3(v, v);
        }

        // returns a copy of /vector/ with its magnitude clamped to /max_length/
        void ClampMagnitude(float max_length)
        {
            const __m128 v              = Simd();
            const __m128 length_squared = simd::dot3_splat(v, v);
            if (_mm_cvtss_f32(length_squared) > max_length * max_length)
            {
                // normalize and scale in a single multiply
                const __m128 scale = _mm_div_ps(_mm_set1_ps(max_length), _mm_sqrt_ps(length_squared));
                simd::store3(&x, _mm_mul_ps(v, scale));
            }
        }

        // builds an orthonormal basis around this vector, seeded from whichever cardinal axis is least parallel to it
        void FindBestAxisVectors(Vector3& axis_1, Vector3& axis_2) const
        {
            const Vector3 a = Abs();

            axis_1 = (a.z > a.x && a.z > a.y) ? Vector3(1, 0, 0) : Vector3(0, 0, 1);
            axis_1 = (axis_1 - *this * (axis_1.Dot(*this))).Normalized();
            axis_2 = axis_1.Cross(*this);
        }

        // distance
        inline float Distance(const Vector3& x_from)                     { return ((*this) - x_from).Length(); }
        inline float DistanceSquared(const Vector3& x_from)              { return ((*this) - x_from).LengthSquared(); }
        static float Distance(const Vector3& a, const Vector3& b)        { return (b - a).Length(); }
        static float DistanceSquared(const Vector3& a, const Vector3& b) { return (b - a).LengthSquared(); }

        // return absolute vector
        [[nodiscard]] Vector3 Abs() const
        {
            return Vector3(simd::abs(Simd()));
        }

        // linear interpolation with another vector
        Vector3 Lerp(const Vector3& v, float t) const { return Lerp(*this, v, t); }
        static Vector3 Lerp(const Vector3& a, const Vector3& b, const float t)
        {
            const __m128 va = a.Simd();
            return Vector3(_mm_fmadd_ps(_mm_sub_ps(b.Simd(), va), _mm_set1_ps(t), va));
        }

        Vector3 operator*(const Vector3& b) const
        {
            return Vector3(_mm_mul_ps(Simd(), b.Simd()));
        }

        static Vector3 Min(const Vector3& a, const Vector3& b)
        {
            return Vector3(_mm_min_ps(a.Simd(), b.Simd()));
        }

        static Vector3 Max(const Vector3& a, const Vector3& b)
        {
            return Vector3(_mm_max_ps(a.Simd(), b.Simd()));
        }

        void operator*=(const Vector3& b)
        {
            simd::store3(&x, _mm_mul_ps(Simd(), b.Simd()));
        }

        Vector3 operator*(const float value) const
        {
            return Vector3(_mm_mul_ps(Simd(), _mm_set1_ps(value)));
        }

        void operator*=(const float value)
        {
            simd::store3(&x, _mm_mul_ps(Simd(), _mm_set1_ps(value)));
        }

        Vector3 operator+(const Vector3& b) const
        {
            return Vector3(_mm_add_ps(Simd(), b.Simd()));
        }

        Vector3 operator+(const float value) const
        {
            return Vector3(_mm_add_ps(Simd(), _mm_set1_ps(value)));
        }

        void operator+=(const Vector3& b)
        {
            simd::store3(&x, _mm_add_ps(Simd(), b.Simd()));
        }

        void operator+=(const float value)
        {
            simd::store3(&x, _mm_add_ps(Simd(), _mm_set1_ps(value)));
        }

        Vector3 operator-(const Vector3& b) const
        {
            return Vector3(_mm_sub_ps(Simd(), b.Simd()));
        }

        Vector3 operator-(const float value) const
        {
            return Vector3(_mm_sub_ps(Simd(), _mm_set1_ps(value)));
        }

        void operator-=(const Vector3& rhs)
        {
            simd::store3(&x, _mm_sub_ps(Simd(), rhs.Simd()));
        }

        Vector3 operator/(const Vector3& rhs) const
        {
            return Vector3(_mm_div_ps(Simd(), simd::pad_one<0x8>(rhs.Simd())));
        }

        Vector3 operator/(const float rhs) const
        {
            return Vector3(_mm_div_ps(Simd(), _mm_set1_ps(rhs)));
        }

        void operator/=(const Vector3& rhs)
        {
            simd::store3(&x, _mm_div_ps(Simd(), simd::pad_one<0x8>(rhs.Simd())));
        }

        // test for equality without using epsilon
        bool operator==(const Vector3& rhs) const
        {
            return (simd::equal_mask(Simd(), rhs.Simd()) & 0x7) == 0x7;
        }

        // test for inequality without using epsilon
        bool operator!=(const Vector3& rhs) const
        {
            return !(*this == rhs);
        }

        // return negation
        Vector3 operator -() const { return Vector3(simd::negate(Simd())); }

        bool IsNaN() const    { return (simd::nan_mask(Simd()) & 0x7) != 0; }
        bool IsFinite() const { return (simd::finite_mask(Simd()) & 0x7) == 0x7; }
        std::string ToString() const;
        const float* Data() const { return &x; }

        float x;
        float y;
        float z;

        static const Vector3 Zero;
        static const Vector3 Left;
        static const Vector3 Right;
        static const Vector3 Up;
        static const Vector3 Down;
        static const Vector3 Forward;
        static const Vector3 Backward;
        static const Vector3 One;
        static const Vector3 Infinity;
        static const Vector3 InfinityNeg;
    };

    // reverse order operators
    inline  Vector3 operator*(float lhs, const Vector3& rhs) { return rhs * lhs; }
}
