/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ======
#include <string>
#include "Helper.h"
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

        ~Vector2() = default;
        Vector2 operator+(const Vector2& b) const
        {
            return Vector2
            (
                this->x + b.x,
                this->y + b.y
            );
        }

        void operator+=(const Vector2& b)
        {
            this->x += b.x;
            this->y += b.y;
        }

        Vector2 operator*(const Vector2& b) const
        {
            return Vector2(x * b.x, y * b.y);
        }

        void operator*=(const Vector2& b)
        {
            x *= b.x;
            y *= b.y;
        }

        Vector2 operator*(const float value) const
        {
            return Vector2(x * value, y * value);
        }

        void operator*=(const float value)
        {
            x *= value;
            y *= value;
        }

        Vector2 operator-(const Vector2& b) const { return Vector2(x - b.x, y - b.y); }
        Vector2 operator-(const float value) const { return Vector2(x - value, y - value); }

        void operator-=(const Vector2& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
        }

        Vector2 operator/(const Vector2& rhs) const { return Vector2(x / rhs.x, y / rhs.y); }
        Vector2 operator/(const float rhs) const { return Vector2(x / rhs, y / rhs); }

        void operator/=(const Vector2& rhs)
        {
            x /= rhs.x;
            y /= rhs.y;
        }

         static Vector2 Min(const Vector2& a, const Vector2& b)
        {
            return Vector2(
                a.x < b.x ? a.x : b.x,
                a.y < b.y ? a.y : b.y
            );
        }
        static Vector2 Max(const Vector2& a, const Vector2& b)
        {
            return Vector2(
                a.x > b.x ? a.x : b.x,
                a.y > b.y ? a.y : b.y
            );
        }

        [[nodiscard]] float Length() const { return sqrt(x * x + y * y); }
        [[nodiscard]] float LengthSquared() const { return x * x + y * y; }

        static inline float Distance(const Vector2& a, const Vector2& b) { return (b - a).Length(); }
        static inline float DistanceSquared(const Vector2& a, const Vector2& b) { return (b - a).LengthSquared(); }

        void Normalize()
        {
            float length = Length();
            if (length > 0.0001f) // Avoid division by zero
            {
                x /= length;
                y /= length;
            }
            else
            {
                x = 0.0f;
                y = 0.0f;
            }
        }

        [[nodiscard]] Vector2 Normalized() const
        {
            Vector2 result = *this;
            result.Normalize();
            return result;
        }

        bool operator==(const Vector2& b) const
        {
            return x == b.x && y == b.y;
        }

        bool operator!=(const Vector2& b) const
        {
            return x != b.x || y != b.y;
        }

        // return negation
        Vector2 operator -() const { return Vector2(-x, -y); }

        [[nodiscard]] const float* Data() const { return &x; }
        std::string ToString() const;

        float x;
        float y;
        static const Vector2 Zero;
        static const Vector2 One;
    };
}
