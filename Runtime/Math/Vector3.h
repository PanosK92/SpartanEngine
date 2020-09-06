/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ===========================
#include <string>
#include "MathHelper.h"
#include "../Core/Spartan_Definitions.h"
//======================================

namespace Spartan::Math
{
    class Vector4;

    class SPARTAN_CLASS Vector3
    {
    public:
        // Constructor
        Vector3()
        {
            x = 0;
            y = 0;
            z = 0;
        }

        // Copy-constructor
        Vector3(const Vector3& vector)
        {
            x = vector.x;
            y = vector.y;
            z = vector.z;
        }

        // Copy-constructor
        Vector3(const Vector4& vector);

        // Construct from coordinates.
        Vector3(float x, float y, float z)
        {
            this->x = x;
            this->y = y;
            this->z = z;
        }

        // Construct from array.
        Vector3(float pos[3])
        {
            this->x = pos[0];
            this->y = pos[1];
            this->z = pos[2];
        }

        // Construct from single value.
        Vector3(float f)
        {
            x = f;
            y = f;
            z = f;
        }

        // Normalize
        void Normalize()
        {
            const auto length_squared = LengthSquared();
            if (!Helper::Equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
                const auto length_inverted = 1.0f / Helper::Sqrt(length_squared);
                x *= length_inverted;
                y *= length_inverted;
                z *= length_inverted;
            }
        };

        // Return normalized vector
        [[nodiscard]] Vector3 Normalized() const
        {
            const auto length_squared = LengthSquared();
            if (!Helper::Equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
                const auto length_inverted = 1.0f / Helper::Sqrt(length_squared);
                return (*this) * length_inverted;
            }
            else
                return *this;
        }

        // Returns normalized vector
        static inline Vector3 Normalize(const Vector3& v) { return v.Normalized(); }

        // Returns the dot product
        static inline float Dot(const Vector3& v1, const Vector3& v2)    { return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z); }

        // Returns the dot product
        [[nodiscard]] float Dot(const Vector3& rhs) const       { return x * rhs.x + y * rhs.y + z * rhs.z; }

        // Returns the cross product
        static inline Vector3 Cross(const Vector3& v1, const Vector3& v2)
        {
            return Vector3(
                v1.y * v2.z - v2.y * v1.z,
                -(v1.x * v2.z - v2.x * v1.z),
                v1.x * v2.y - v2.x * v1.y
            );
        }
        // Returns the cross product
        [[nodiscard]] Vector3 Cross(const Vector3& v2) const { return Cross(*this, v2); }

        // Returns the length
        [[nodiscard]] float Length() const          { return Helper::Sqrt(x * x + y * y + z * z); }
        // Returns the squared length
        [[nodiscard]] float LengthSquared() const   { return x * x + y * y + z * z; }

        // Returns a copy of /vector/ with its magnitude clamped to /maxLength/.
        inline void ClampMagnitude(float maxLength)
        {
            const float sqrmag = LengthSquared();

            if (sqrmag > maxLength * maxLength)
            {
                const float mag = Helper::Sqrt(sqrmag);

                //these intermediate variables force the intermediate result to be
                //of float precision. without this, the intermediate result can be of higher
                //precision, which changes behavior.

                const float normalized_x = x / mag;
                const float normalized_y = y / mag;
                const float normalized_z = z / mag;

                x = normalized_x * maxLength;
                y = normalized_y * maxLength;
                z = normalized_z * maxLength;
            }
        }

        // Returns the distance between to vectors
        static inline float Distance(const Vector3& a, const Vector3& b)           { return (b - a).Length(); }
        // Returns the squared distance between to vectors
        static inline float DistanceSquared(const Vector3& a, const Vector3& b)    { return (b - a).LengthSquared(); }

        // Floor
        void Floor()
        {
            x = floor(x);
            y = floor(y);
            z = floor(z);
        }

        // Return absolute vector
        [[nodiscard]] Vector3 Abs() const { return Vector3(Helper::Abs(x), Helper::Abs(y), Helper::Abs(z)); }

        Vector3 operator*(const Vector3& b) const
        {
            return Vector3(
                x * b.x,
                y * b.y,
                z * b.z
            );
        }

        void operator*=(const Vector3& b)
        {
            x *= b.x;
            y *= b.y;
            z *= b.z;
        }

        Vector3 operator*(const float value) const
        {
            return Vector3(
                x * value,
                y * value,
                z * value
            );
        }

        void operator*=(const float value)
        {
            x *= value;
            y *= value;
            z *= value;
        }

        Vector3 operator+(const Vector3& b) const   { return Vector3(x + b.x, y + b.y, z + b.z); }
        Vector3 operator+(const float value) const  { return Vector3(x + value, y + value, z + value); }

        void operator+=(const Vector3& b)
        {
            x += b.x;
            y += b.y;
            z += b.z;
        }

        void operator+=(const float value)
        {
            x += value;
            y += value;
            z += value;
        }

        Vector3 operator-(const Vector3& b) const   { return Vector3(x - b.x, y - b.y, z - b.z); }
        Vector3 operator-(const float value) const  { return Vector3(x - value, y - value, z - value); }

        void operator-=(const Vector3& rhs)
        {
            x -= rhs.x;
            y -= rhs.y;
            z -= rhs.z;
        }

        Vector3 operator/(const Vector3& rhs) const { return Vector3(x / rhs.x, y / rhs.y, z / rhs.z); }
        Vector3 operator/(const float rhs) const    { return Vector3(x / rhs, y / rhs, z / rhs); }

        void operator/=(const Vector3& rhs)
        {
            x /= rhs.x;
            y /= rhs.y;
            z /= rhs.z;
        }

        // Test for equality without using epsilon
        bool operator==(const Vector3& rhs) const
        {
            return x == rhs.x && y == rhs.y && z == rhs.z;
        }

        // Test for inequality without using epsilon
        bool operator!=(const Vector3& rhs) const
        {
            return !(*this == rhs);
        }

        // Return negation.
        Vector3 operator -() const { return Vector3(-x, -y, -z); }

        [[nodiscard]] std::string ToString() const;
        [[nodiscard]] const float* Data() const { return &x; }

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

    // Reverse order operators
    inline SPARTAN_CLASS Vector3 operator*(float lhs, const Vector3& rhs) { return rhs * lhs; }
}
