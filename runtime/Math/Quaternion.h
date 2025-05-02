/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace spartan::math
{
    class Quaternion
    {
    public:
        // Constructs an identity quaternion
        Quaternion()
        {
            x = 0;
            y = 0;
            z = 0;
            w = 1;
        }

        // Constructs a new quaternion with the given components
        Quaternion(float x, float y, float z, float w)
        {
            this->x = x;
            this->y = y;
            this->z = z;
            this->w = w;
        }
        ~Quaternion() = default;

        // Creates a new Quaternion from the specified axis and angle.
        // The angle in radians.
        // The axis of rotation.
        static Quaternion FromAxisAngle(const Vector3& axis, float angle)
        {
            const float half = angle * 0.5f;
            const float sin  = sinf(half);
            const float cos  = cosf(half);

            return Quaternion(axis.x * sin, axis.y * sin, axis.z * sin, cos);
        }

        void FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);

        void ToAngleAxis(float& angle, Vector3& axis) const
        {
            // Normalize the quaternion to prevent inaccuracies
            Quaternion q = this->Normalized();

            // Calculate the angle
            angle = 2.0f * std::acos(q.w) * 180.0f / 3.14159265358979323846f;

            // Calculate the axis
            float s = std::sqrt(1.0f - q.w * q.w);
            if (s < 0.001f)
            {
                // If s is close to zero, the axis is not well-defined and
                // we can choose any arbitrary axis
                axis.x = q.x;
                axis.y = q.y;
                axis.z = q.z;
            }
            else
            {
                axis.x = q.x / s;
                axis.y = q.y / s;
                axis.z = q.z / s;
            }
        }

        // Creates a new Quaternion from the specified yaw, pitch and roll angles.
        // Yaw around the y axis in radians.
        // Pitch around the x axis in radians.
        // Roll around the z axis in radians.
        static inline Quaternion FromYawPitchRoll(float yaw, float pitch, float roll)
        {
            const float halfRoll  = roll * 0.5f;
            const float halfPitch = pitch * 0.5f;
            const float halfYaw   = yaw * 0.5f;

            const float sinRoll  = sin(halfRoll);
            const float cosRoll  = cos(halfRoll);
            const float sinPitch = sin(halfPitch);
            const float cosPitch = cos(halfPitch);
            const float sinYaw   = sin(halfYaw);
            const float cosYaw   = cos(halfYaw);

            return Quaternion(
                cosYaw * sinPitch * cosRoll + sinYaw * cosPitch * sinRoll,
                sinYaw * cosPitch * cosRoll - cosYaw * sinPitch * sinRoll,
                cosYaw * cosPitch * sinRoll - sinYaw * sinPitch * cosRoll,
                cosYaw * cosPitch * cosRoll + sinYaw * sinPitch * sinRoll
            );
        }

        static Quaternion FromToRotation(const Vector3& start, const Vector3& end)
        {
            const Vector3 normStart = start.Normalized();
            const Vector3 normEnd   = end.Normalized();
            const float d           = normStart.Dot(normEnd);

            if (d > -1.0f + std::numeric_limits<float>::epsilon())
            {
                const Vector3 c = normStart.Cross(normEnd);
                const float s = sqrtf((1.0f + d) * 2.0f);
                const float invS = 1.0f / s;

                return Quaternion(
                    c.x * invS,
                    c.y * invS,
                    c.z * invS,
                    0.5f * s);
            }
            else
            {
                Vector3 axis = Vector3::Right.Cross(normStart);
                if (axis.Length() < std::numeric_limits<float>::epsilon())
                {
                    axis = Vector3::Up.Cross(normStart);
                }

                return FromAxisAngle(axis, 180.0f * deg_to_rad);
            }
        }

        static Quaternion FromLookRotation(const Vector3& direction, const Vector3& up_direction = Vector3::Up)
        {
            Quaternion result;
            const Vector3 forward = direction.Normalized();

            Vector3 v = forward.Cross(up_direction);
            if (v.LengthSquared() >= std::numeric_limits<float>::min())
            {
                v.Normalize();
                const Vector3 up    = v.Cross(forward);
                const Vector3 right = up.Cross(forward);
                result.FromAxes(right, up, forward);
            }
            else
            {
                result = Quaternion::FromToRotation(Vector3::Forward, forward);
            }

            return result;
        }

        static Quaternion FromToRotation(const Quaternion& start, const Quaternion& end) { return start.Inverse() * end; }

        static Quaternion Lerp(const Quaternion& a, const Quaternion& b, const float t)
        {
            Quaternion quaternion;

            if (Dot(a, b) >= 0)
            {
                quaternion = a * (1 - t) + b * t;
            }
            else
            {
                quaternion = a * (1 - t) - b * t;
            }

            return quaternion.Normalized();
        }

        static Quaternion Multiply(const Quaternion& Qa, const Quaternion& Qb)
        {
            const float x     = Qa.x;
            const float y     = Qa.y;
            const float z     = Qa.z;
            const float w     = Qa.w;
            const float num4  = Qb.x;
            const float num3  = Qb.y;
            const float num2  = Qb.z;
            const float num   = Qb.w;
            const float num12 = (y * num2) - (z * num3);
            const float num11 = (z * num4) - (x * num2);
            const float num10 = (x * num3) - (y * num4);
            const float num9  = ((x * num4) + (y * num3)) + (z * num2);

            return Quaternion(
                ((x * num) + (num4 * w)) + num12,
                ((y * num) + (num3 * w)) + num11,
                ((z * num) + (num2 * w)) + num10,
                (w * num) - num9
            );
        }

        auto Conjugate() const      { return Quaternion(-x, -y, -z, w); }
        float LengthSquared() const { return (x * x) + (y * y) + (z * z) + (w * w); }

        // Normalizes the quaternion
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
        }

        Quaternion Normalized() const
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

        Quaternion Inverse() const 
        {
            const float length_squared = LengthSquared();
            if (length_squared == 1.0f)
            {
                return Conjugate();
            }
            else if (length_squared >= std::numeric_limits<float>::min())
            {
                return Conjugate() * (1.0f / length_squared);
            }
            else
            {
                return Identity;
            }
        }

        Vector3 ToEulerAngles() const
        {
            // Derivation from http://www.geometrictools.com/Documentation/EulerAngles.pdf
            // Order of rotations: Z first, then X, then Y
            const float check = 2.0f * (-y * z + w * x);

            if (check < -0.995f)
            {
                return Vector3
                (
                    -90.0f,
                    0.0f,
                    -atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * rad_to_deg
                );
            }

            if (check > 0.995f)
            {
                return Vector3
                (
                    90.0f,
                    0.0f,
                    atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * rad_to_deg
                );
            }

            return Vector3
            (
                asinf(check) * rad_to_deg,
                atan2f(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)) * rad_to_deg,
                atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z)) * rad_to_deg
            );
        }

        // euler angles to quaternion (input in degrees)
        static Quaternion FromEulerAngles(const Vector3& rotation)                           { return FromYawPitchRoll(rotation.y * deg_to_rad, rotation.x * deg_to_rad, rotation.z * deg_to_rad); }
        static Quaternion FromEulerAngles(float rotationX, float rotationY, float rotationZ) { return FromYawPitchRoll(rotationY * deg_to_rad,  rotationX * deg_to_rad,  rotationZ * deg_to_rad); }

        // Returns yaw in degrees
        float Yaw() const   { return ToEulerAngles().y; }
        // Returns pitch in degrees
        float Pitch() const { return ToEulerAngles().x; }
        // Returns roll in degrees
        float Roll() const  { return ToEulerAngles().z; }

        // Calculate dot product.
        float Dot(const Quaternion& rhs)                            const { return w * rhs.w + x * rhs.x + y * rhs.y + z * rhs.z; }
        static inline float Dot(const Quaternion& a, const Quaternion& b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }

        // Normalized linear interpolation with another quaternion.
        Quaternion lerp(const Quaternion& rhs, float t) { return ((*this) + ((rhs - (*this)) * t)).Normalized(); }

        // Operators
        Quaternion& operator=(const Quaternion& rhs) = default;
        Quaternion operator+(const Quaternion& rhs) const { return Quaternion(x + rhs.x, y + rhs.y, z + rhs.z, w + rhs.w); }
        Quaternion operator-(const Quaternion& rhs) const { return Quaternion(x - rhs.x, y - rhs.y, z - rhs.z, w - rhs.w); }
        Quaternion operator-()                      const { return Quaternion(-x, -y, -z, -w); }
        Quaternion operator*(const Quaternion& rhs) const { return Multiply(*this, rhs); }
        void operator*=(const Quaternion& rhs)            { *this = Multiply(*this, rhs); }
        Vector3 operator*(const Vector3& rhs) const
        {
            const Vector3 qVec(x, y, z);
            const Vector3 cross1(qVec.Cross(rhs));
            const Vector3 cross2(qVec.Cross(cross1));

            return rhs + 2.0f * (cross1 * w + cross2);
        }
        Quaternion& operator*=(float rhs)
        {            
            x *= rhs;
            y *= rhs;
            z *= rhs;
            w *= rhs;

            return *this;
        }
        Quaternion operator*(float rhs)             const { return Quaternion(x * rhs, y * rhs, z * rhs, w * rhs); }

        // Equality
        bool operator==(const Quaternion& rhs) const { return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w; }
        bool operator!=(const Quaternion& rhs) const { return !(*this == rhs); }
        bool Equals(const Quaternion& rhs)     const { return approximate_equals(x, rhs.x) && approximate_equals(y, rhs.y) && approximate_equals(z, rhs.z) && approximate_equals(w, rhs.w); }

        std::string ToString() const;
        float x, y, z, w;
        static const Quaternion Identity;
    };

    // Reverse order operators
    inline  Vector3 operator*(const Vector3& lhs, const Quaternion& rhs) { return rhs * lhs; }
    inline  Quaternion operator*(float lhs, const Quaternion& rhs)       { return rhs * lhs; }
}
