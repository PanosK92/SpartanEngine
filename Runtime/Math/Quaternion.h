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

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace Spartan::Math
{
    class SPARTAN_CLASS Quaternion
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
        static inline Quaternion FromAngleAxis(float angle, const Vector3& axis)
        {
            const float half    = angle * 0.5f;
            const float sin    = sinf(half);
            const float cos    = cosf(half);
            return Quaternion(axis.x * sin, axis.y * sin, axis.z * sin, cos);
        }
        void FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);

        // Creates a new Quaternion from the specified yaw, pitch and roll angles.
        // Yaw around the y axis in radians.
        // Pitch around the x axis in radians.
        // Roll around the z axis in radians.
        static inline Quaternion FromYawPitchRoll(float yaw, float pitch, float roll)
        {
            const float halfRoll    = roll * 0.5f;
            const float halfPitch = pitch * 0.5f;
            const float halfYaw    = yaw * 0.5f;

            const float sinRoll    = sin(halfRoll);
            const float cosRoll    = cos(halfRoll);
            const float sinPitch    = sin(halfPitch);
            const float cosPitch    = cos(halfPitch);
            const float sinYaw    = sin(halfYaw);
            const float cosYaw    = cos(halfYaw);

            return Quaternion(
                cosYaw * sinPitch * cosRoll + sinYaw * cosPitch * sinRoll,
                sinYaw * cosPitch * cosRoll - cosYaw * sinPitch * sinRoll,
                cosYaw * cosPitch * sinRoll - sinYaw * sinPitch * cosRoll,
                cosYaw * cosPitch * cosRoll + sinYaw * sinPitch * sinRoll
            );
        }
        // Euler angles to quaternion (input in degrees)
        static inline auto FromEulerAngles(const Vector3& rotation)                              { return FromYawPitchRoll(rotation.y * Helper::DEG_TO_RAD, rotation.x * Helper::DEG_TO_RAD, rotation.z * Helper::DEG_TO_RAD); }
        static inline auto FromEulerAngles(float rotationX, float rotationY, float rotationZ)    { return FromYawPitchRoll(rotationY * Helper::DEG_TO_RAD, rotationX * Helper::DEG_TO_RAD, rotationZ * Helper::DEG_TO_RAD); }

        // Returns Euler angles in degrees
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
                    -atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * Helper::RAD_TO_DEG
                );
            }

            if (check > 0.995f)
            {
                return Vector3
                (
                    90.0f,
                    0.0f,
                    atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * Helper::RAD_TO_DEG
                );
            }

            return Vector3
            (
                asinf(check) * Helper::RAD_TO_DEG,
                atan2f(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)) * Helper::RAD_TO_DEG,
                atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z)) * Helper::RAD_TO_DEG
            );
        }

        // Returns yaw in degrees
        auto Yaw() const    { return ToEulerAngles().y; }
        // Returns pitch in degrees
        auto Pitch() const  { return ToEulerAngles().x; }
        // Returns roll in degrees
        auto Roll() const   { return ToEulerAngles().z; }

        static inline Quaternion FromToRotation(const Vector3& start, const Vector3& end)
        {
            const Vector3 normStart    = start.Normalized();
            const Vector3 normEnd   = end.Normalized();
            const float d           = normStart.Dot(normEnd);

            if (d > -1.0f + Helper::EPSILON)
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
                if (axis.Length() < Helper::EPSILON)
                {
                    axis = Vector3::Up.Cross(normStart);
                }

                return FromAngleAxis(180.0f * Helper::DEG_TO_RAD, axis);
            }
        }

        static inline Quaternion FromLookRotation(const Vector3& direction, const Vector3& upDirection = Vector3::Up)
        {
            Quaternion ret;
            const Vector3 forward = direction.Normalized();

            Vector3 v = forward.Cross(upDirection);
            if (v.LengthSquared() >= Helper::EPSILON)
            {
                v.Normalize();
                const Vector3 up = v.Cross(forward);
                const Vector3 right = up.Cross(forward);
                ret.FromAxes(right, up, forward);
            }
            else
                ret = Quaternion::FromToRotation(Vector3::Forward, forward);

            return ret;
        }

        static inline Quaternion FromToRotation(const Quaternion& start, const Quaternion& end)
        {
            return start.Inverse() * end;
        }

        auto Conjugate() const        { return Quaternion(-x, -y, -z, w); }
        float LengthSquared() const    { return (x * x) + (y * y) + (z * z) + (w * w); }

        // Normalizes the quaternion
        void Normalize()
        {
            const auto length_squared = LengthSquared();
            if (!Helper::Equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
                const auto length_inverted = 1.0f / Helper::Sqrt(length_squared);
                x *= length_inverted;
                y *= length_inverted;
                z *= length_inverted;
                w *= length_inverted;
            }
        }

        // Returns a normalized version of the quaternion
        Quaternion Normalized() const
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

        // Returns the inverse
        Quaternion Inverse() const 
        {
            const float length_squared = LengthSquared();
            if (length_squared == 1.0f)
                return Conjugate();
            else if (length_squared >= Helper::EPSILON)
                return Conjugate() * (1.0f / length_squared);
            else
                return Identity;
        }

        Quaternion& operator =(const Quaternion& rhs)
        = default;

        static inline Quaternion Multiply(const Quaternion& Qa, const Quaternion& Qb)
        {
            const float x = Qa.x;
            const float y = Qa.y;
            const float z = Qa.z;
            const float w = Qa.w;
            const float num4 = Qb.x;
            const float num3 = Qb.y;
            const float num2 = Qb.z;
            const float num = Qb.w;
            const float num12 = (y * num2) - (z * num3);
            const float num11 = (z * num4) - (x * num2);
            const float num10 = (x * num3) - (y * num4);
            const float num9 = ((x * num4) + (y * num3)) + (z * num2);

            return Quaternion(
                ((x * num) + (num4 * w)) + num12,
                ((y * num) + (num3 * w)) + num11,
                ((z * num) + (num2 * w)) + num10,
                (w * num) - num9
            );
        }

        Quaternion operator*(const Quaternion& rhs) const
        {
            return Multiply(*this, rhs);
        }

        void operator*=(const Quaternion& rhs)
        {
            *this = Multiply(*this, rhs);
        }

        Vector3 operator*(const Vector3& rhs) const
        {
            const Vector3 qVec(x, y, z);
            const Vector3 cross1(qVec.Cross(rhs));
            const Vector3 cross2(qVec.Cross(cross1));

            return rhs + 2.0f * (cross1 * w + cross2);
        }

        Quaternion& operator *=(float rhs)
        {            
            x *= rhs;
            y *= rhs;
            z *= rhs;
            w *= rhs;

            return *this;
        }

        Quaternion operator *(float rhs) const { return Quaternion(x * rhs, y * rhs, z * rhs, w * rhs); }

        // Test for equality with a quaternion
        bool operator ==(const Quaternion& rhs) const
        {
            return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w;
        }

        // Test for inequality with a quaternion
        bool operator!=(const Quaternion& rhs) const { return !(*this == rhs); }

        // Test for equality with a quaternion, using epsilon
        bool Equals(const Quaternion& rhs) const
        {
            return Helper::Equals(x, rhs.x) && Helper::Equals(y, rhs.y) && Helper::Equals(z, rhs.z) && Helper::Equals(w, rhs.w);
        }

        std::string ToString() const;
        float x, y, z, w;
        static const Quaternion Identity;
    };

    // Reverse order operators
    inline SPARTAN_CLASS Vector3 operator*(const Vector3& lhs, const Quaternion& rhs) { return rhs * lhs; }
    inline SPARTAN_CLASS Quaternion operator*(float lhs, const Quaternion& rhs) { return rhs * lhs; }
}
