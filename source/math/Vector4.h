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

        Vector4 operator*(const Vector4& Other) const
        {
            return Vector4(x * Other.x, y * Other.y, z * Other.z, w * Other.w);
        }

        Vector4 operator-(const Vector4& Other) const
        {
            return Vector4(x - Other.x, y - Other.y, z - Other.z, w - Other.w);
        }

        Vector4 operator-() const
        {
            return Vector4(-x, -y, -z, -w);
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

        Vector4 operator/(const Vector4& Other) const
        {
            return Vector4(x / Other.x, y / Other.y, z / Other.z, w / Other.w);
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
            {
                return *this;
            }
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
