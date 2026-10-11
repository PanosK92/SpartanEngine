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
    class Vector2
    {
    public:
        Vector2()
        {
            x = 0;
            y = 0;
        }

        Vector2(const Vector2& vector) = default;

        Vector2(float x, float y)
        {
            this->x = x;
            this->y = y;
        }

        Vector2(int x, int y)
        {
            this->x = static_cast<float>(x);
            this->y = static_cast<float>(y);
        }

        Vector2(uint32_t x, uint32_t y)
        {
            this->x = static_cast<float>(x);
            this->y = static_cast<float>(y);
        }
        
        Vector2(float x)
        {
            this->x = x;
            this->y = x;
        }

        explicit Vector2(const __m128 v)
        {
            simd::store2(&x, v);
        }

        ~Vector2() = default;

        // x, y in the low lanes, z and w are zero
        [[nodiscard]] __m128 Simd() const { return simd::load2(&x); }

        Vector2 operator+(const Vector2& b) const
        {
            return Vector2(_mm_add_ps(Simd(), b.Simd()));
        }

        void operator+=(const Vector2& b)
        {
            simd::store2(&x, _mm_add_ps(Simd(), b.Simd()));
        }

        Vector2 operator*(const Vector2& b) const
        {
            return Vector2(_mm_mul_ps(Simd(), b.Simd()));
        }

        void operator*=(const Vector2& b)
        {
            simd::store2(&x, _mm_mul_ps(Simd(), b.Simd()));
        }

        Vector2 operator*(const float value) const
        {
            return Vector2(_mm_mul_ps(Simd(), _mm_set1_ps(value)));
        }

        void operator*=(const float value)
        {
            simd::store2(&x, _mm_mul_ps(Simd(), _mm_set1_ps(value)));
        }

        Vector2 operator-(const Vector2& b) const { return Vector2(_mm_sub_ps(Simd(), b.Simd())); }
        Vector2 operator-(const float value) const { return Vector2(_mm_sub_ps(Simd(), _mm_set1_ps(value))); }

        void operator-=(const Vector2& rhs)
        {
            simd::store2(&x, _mm_sub_ps(Simd(), rhs.Simd()));
        }

        Vector2 operator/(const Vector2& rhs) const { return Vector2(_mm_div_ps(Simd(), simd::pad_one<0xC>(rhs.Simd()))); }
        Vector2 operator/(const float rhs) const { return Vector2(_mm_div_ps(Simd(), _mm_set1_ps(rhs))); }

        void operator/=(const Vector2& rhs)
        {
            simd::store2(&x, _mm_div_ps(Simd(), simd::pad_one<0xC>(rhs.Simd())));
        }

        [[nodiscard]] float Length() const
        {
            const __m128 v = Simd();
            return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(simd::dot2(v, v))));
        }

        [[nodiscard]] float LengthSquared() const
        {
            const __m128 v = Simd();
            return simd::dot2(v, v);
        }

        static inline float Distance(const Vector2& a, const Vector2& b) { return (b - a).Length(); }
        static inline float DistanceSquared(const Vector2& a, const Vector2& b) { return (b - a).LengthSquared(); }

        void Normalize()
        {
            const __m128 v      = Simd();
            const __m128 length = _mm_sqrt_ps(_mm_set1_ps(simd::dot2(v, v)));
            // avoid division by zero
            if (_mm_cvtss_f32(length) > 0.0001f)
            {
                simd::store2(&x, _mm_div_ps(v, length));
            }
            else
            {
                x = 0.0f;
                y = 0.0f;
            }
        }

        bool IsFinite() const { return (simd::finite_mask(Simd()) & 0x3) == 0x3; }

        [[nodiscard]] Vector2 Normalized() const
        {
            Vector2 result = *this;
            result.Normalize();
            return result;
        }

        bool operator==(const Vector2& b) const
        {
            return (simd::equal_mask(Simd(), b.Simd()) & 0x3) == 0x3;
        }

        bool operator!=(const Vector2& b) const
        {
            return !(*this == b);
        }

        // return negation
        Vector2 operator -() const { return Vector2(simd::negate(Simd())); }

        [[nodiscard]] const float* Data() const { return &x; }
        std::string ToString() const;

        float x;
        float y;
        static const Vector2 Zero;
        static const Vector2 One;
    };
}
