/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ======
#include <string>
#include "Helper.h"
#include "Simd.h"
//=================

namespace spartan::math
{
    class Vector3;
    class Matrix;

    class Vector4
    {
    public:
        Vector4()
        {
            x = 0;
            y = 0;
            z = 0;
            w = 0;
        }

        Vector4(float x, float y, float z, float w)
        {
            this->x = x;
            this->y = y;
            this->z = z;
            this->w = w;
        }

        Vector4(float value)
        {
            this->x = value;
            this->y = value;
            this->z = value;
            this->w = value;
        }

        Vector4(const Vector3& value, float w);
        Vector4(const Vector3& value);

        explicit Vector4(const __m128 v)
        {
            simd::store4(&x, v);
        }

        ~Vector4() = default;

        [[nodiscard]] __m128 Simd() const { return simd::load4(&x); }

        bool operator ==(const Vector4& rhs) const
        {
            return simd::equal_mask(Simd(), rhs.Simd()) == 0xF;
        }

        bool operator !=(const Vector4& rhs) const
        {
            return !(*this == rhs);
        }

        Vector4 operator*(const float value) const
        {
            return Vector4(_mm_mul_ps(Simd(), _mm_set1_ps(value)));
        }

        Vector4 operator*(const Vector4& Other) const
        {
            return Vector4(_mm_mul_ps(Simd(), Other.Simd()));
        }

        Vector4 operator-(const Vector4& Other) const
        {
            return Vector4(_mm_sub_ps(Simd(), Other.Simd()));
        }

        Vector4 operator-() const
        {
            return Vector4(simd::negate(Simd()));
        }

        void operator*=(const float value)
        {
            simd::store4(&x, _mm_mul_ps(Simd(), _mm_set1_ps(value)));
        }

        Vector4 operator /(const float rhs) const
        {
            return Vector4(_mm_div_ps(Simd(), _mm_set1_ps(rhs)));
        }

        Vector4 operator/(const Vector4& Other) const
        {
            return Vector4(_mm_div_ps(Simd(), Other.Simd()));
        }

        [[nodiscard]] float Length() const
        {
            const __m128 v = Simd();
            return _mm_cvtss_f32(_mm_sqrt_ss(simd::dot4_splat(v, v)));
        }

        [[nodiscard]] float LengthSquared() const
        {
            const __m128 v = Simd();
            return simd::dot4(v, v);
        }

        void Normalize()
        {
            const __m128 v              = Simd();
            const __m128 length_squared = simd::dot4_splat(v, v);
            const float length_squared_ = _mm_cvtss_f32(length_squared);
            if (!approximate_equals(length_squared_, 1.0f) && length_squared_ > 0.0f)
            {
                simd::store4(&x, _mm_div_ps(v, _mm_sqrt_ps(length_squared)));
            }
        };

        [[nodiscard]] Vector4 Normalized() const
        {
            Vector4 result = *this;
            result.Normalize();
            return result;
        }

        Vector4 operator+(const Vector4& rhs) const
        {
            return Vector4(_mm_add_ps(Simd(), rhs.Simd()));
        }

        void operator+=(const Vector4& rhs)
        {
            simd::store4(&x, _mm_add_ps(Simd(), rhs.Simd()));
        }

        [[nodiscard]] Vector4 Lerp(const Vector4& rhs, float t) const
        {
            return Lerp(*this, rhs, t);
        }

        [[nodiscard]] static Vector4 Lerp(const Vector4& a, const Vector4& b, float t)
        {
            const __m128 va = a.Simd();
            return Vector4(_mm_fmadd_ps(_mm_sub_ps(b.Simd(), va), _mm_set1_ps(t), va));
        }

        [[nodiscard]] static float Distance(const Vector4& a, const Vector4& b)        { return (b - a).Length(); }
        [[nodiscard]] static float DistanceSquared(const Vector4& a, const Vector4& b) { return (b - a).LengthSquared(); }

        std::string ToString() const;
        const float* Data() const { return &x; }

        float x, y, z, w;

        static const Vector4 One;
        static const Vector4 Zero;
        static const Vector4 Infinity;
        static const Vector4 InfinityNeg;
    };
}
