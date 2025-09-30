/*
Copyright(c) 2015-2025 Panos Karabelas

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

        ~Vector4() = default;

        bool operator ==(const Vector4& rhs) const
        {
            return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
        }

        bool operator !=(const Vector4& rhs) const
        {
            return !(*this == rhs);
        }

        Vector4 operator*(const float value) const
        {
            return Vector4(
                x * value,
                y * value,
                z * value,
                w * value
            );
        }

        void operator*=(const float value)
        {
            x *= value;
            y *= value;
            z *= value;
            w *= value;
        }

        Vector4 operator /(const float rhs) const
        {
            return Vector4(x / rhs, y / rhs, z / rhs, w / rhs);
        }

        [[nodiscard]] float Length()        const { return sqrt(x * x + y * y + z * z + w * w); }
        [[nodiscard]] float LengthSquared() const { return x * x + y * y + z * z + w * w; }

        void Normalize()
        {
            const auto length_squared = LengthSquared();
            if (!approximate_equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
                const auto length_inverted = 1.0f / sqrt(length_squared);
                x *= length_inverted;
                y *= length_inverted;
                z *= length_inverted;
                w *= length_inverted;
            }
        };

        [[nodiscard]] Vector4 Normalized() const
        {
            const auto length_squared = LengthSquared();
            if (!approximate_equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
                const auto length_inverted = 1.0f / sqrt(length_squared);
                return (*this) * length_inverted;
            }
            else
                return *this;
        }

        Vector4 operator+(const Vector4& rhs) const
        {
            return Vector4(
                x + rhs.x,
                y + rhs.y,
                z + rhs.z,
                w + rhs.w
            );
        }

        void operator+=(const Vector4& rhs)
        {
            x += rhs.x;
            y += rhs.y;
            z += rhs.z;
            w += rhs.w;
        }

        [[nodiscard]] Vector4 Lerp(const Vector4& rhs, float t) const
        {
            return (*this) * (1.0f - t) + rhs * t;
        }

        [[nodiscard]] static Vector4 Lerp(const Vector4& a, const Vector4& b, float t)
        {
            return a * (1.0f - t) + b * t;
        }

        std::string ToString() const;
        const float* Data() const { return &x; }

        float x, y, z, w;

        static const Vector4 One;
        static const Vector4 Zero;
        static const Vector4 Infinity;
        static const Vector4 InfinityNeg;
    };
}
