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

//= INCLUDE ==========
#include "Helper.h"
#include <immintrin.h>
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

        void Normalize()
        {
            const auto length_squared = LengthSquared();
            if (!approximate_equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
            #ifdef __AVX2__
                // load x, y, z into an AVX vector (set w component to 0)
                __m128 vec = _mm_set_ps(0.0f, z, y, x);
                
                // calculate the length squared (dot product of vec with itself)
                __m128 dot = _mm_dp_ps(vec, vec, 0x7F); // only sum x, y, z and leave w as 0
                
                // calculate reciprocal square root of the length
                __m128 inv_sqrt = _mm_rsqrt_ps(dot);
                
                // normalize vec by multiplying with inv_sqrt
                vec = _mm_mul_ps(vec, inv_sqrt);
                
                // store back the normalized values
                x = _mm_cvtss_f32(vec);
                y = _mm_cvtss_f32(_mm_shuffle_ps(vec, vec, _MM_SHUFFLE(1, 1, 1, 1)));
                z = _mm_cvtss_f32(_mm_shuffle_ps(vec, vec, _MM_SHUFFLE(2, 2, 2, 2)));
            #else
                // fallback to scalar path
                const auto length_inverted = 1.0f / sqrt(length_squared);
                x *= length_inverted;
                y *= length_inverted;
                z *= length_inverted;
            #endif
            }
        };

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

        float Max()
        {
            return std::max(std::max(x, y), z);
        }

        Vector3 Max(const Vector3& other)
        {
            return {std::max(x, other.x),
                       std::max(y, other.y),
                       std::max(z, other.z)};
        }

        [[nodiscard]] static float Dot(const Vector3& v1, const Vector3& v2)
        {
            return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z);
        }

        [[nodiscard]] float Dot(const Vector3& rhs) const
        {
            return Dot(*this, rhs);
        }


        [[nodiscard]] static Vector3 Cross(const Vector3& v1, const Vector3& v2)
        {
            return Vector3(
                v1.y * v2.z - v2.y * v1.z,
                -(v1.x * v2.z - v2.x * v1.z),
                v1.x * v2.y - v2.x * v1.y
            );
        }

        [[nodiscard]] Vector3 Cross(const Vector3& v2) const { return Cross(*this, v2); }

        [[nodiscard]] float Length() const
        {
        #ifdef __AVX2__
            // Load x, y, z, and 0.0f into an AVX register
            __m128 vec = _mm_set_ps(0.0f, z, y, x);
        
            // Calculate squared length (dot product of vec with itself)
            __m128 dot = _mm_dp_ps(vec, vec, 0x7F); // only sum x, y, z and leave w as 0
        
            // Take the square root of the dot product
            __m128 length = _mm_sqrt_ps(dot);
        
            // Extract the result as a scalar float
            return _mm_cvtss_f32(length);
        #else
            // Fallback to scalar path
            return sqrt(x * x + y * y + z * z);
        #endif
        }
        
        [[nodiscard]] float LengthSquared() const
        {
        #ifdef __AVX2__
            // Load x, y, z, and 0.0f into an AVX register
            __m128 vec = _mm_set_ps(0.0f, z, y, x);
        
            // Calculate squared length (dot product of vec with itself)
            __m128 dot = _mm_dp_ps(vec, vec, 0x7F); // only sum x, y, z and leave w as 0
        
            // Extract the result as a scalar float
            return _mm_cvtss_f32(dot);
        #else
            // Fallback to scalar path
            return x * x + y * y + z * z;
        #endif
        }

        // Returns a copy of /vector/ with its magnitude clamped to /maxLength/
        void ClampMagnitude(float max_length)
        {
            const float sqrmag = LengthSquared();

            if (sqrmag > max_length * max_length)
            {
                const float mag = sqrt(sqrmag);

                // these intermediate variables force the intermediate result to be
                // of float precision. without this, the intermediate result can be of higher
                // precision, which changes behavior.

                const float normalized_x = x / mag;
                const float normalized_y = y / mag;
                const float normalized_z = z / mag;

                x = normalized_x * max_length;
                y = normalized_y * max_length;
                z = normalized_z * max_length;
            }
        }

        void FindBestAxisVectors(Vector3& Axis1, Vector3& Axis2) const
        {
            const float NX = abs(x);
            const float NY = abs(y);
            const float NZ = abs(z);

            // find best basis vectors
            if (NZ > NX && NZ > NY)	Axis1 = Vector3(1, 0, 0);
            else                    Axis1 = Vector3(0, 0, 1);

            Axis1 = (Axis1 - *this * (Axis1.Dot(*this))).Normalized();
            Axis2 = Axis1.Cross(*this);
        }

        // distance
        inline float Distance(const Vector3& x_from)                          { return ((*this) - x_from).Length(); }
        inline float DistanceSquared(const Vector3& x_from)                   { return ((*this) - x_from).LengthSquared(); }
        static float Distance(const Vector3& a, const Vector3& b)        { return (b - a).Length(); }
        static float DistanceSquared(const Vector3& a, const Vector3& b) { return (b - a).LengthSquared(); }

        void Floor()
        {
            x = floor(x);
            y = floor(y);
            z = floor(z);
        }

        static Vector3 Round(const Vector3& vec)
        {
            return Vector3{
                std::round(vec.x),
                std::round(vec.y),
                std::round(vec.z)
            };
        }


        // return absolute vector
        [[nodiscard]] Vector3 Abs() const { return Vector3(abs(x), abs(y), abs(z)); }

        // linear interpolation with another vector
        Vector3 Lerp(const Vector3& v, float t) const                          { return *this * (1.0f - t) + v * t; }
        static Vector3 Lerp(const Vector3& a, const Vector3& b, const float t) { return a + (b - a) * t; }

        Vector3 operator*(const Vector3& b) const
        {
        #ifdef __AVX2__
            // create an __m128 vector from the components of this Vector3, with padding
            __m128 thisVec = _mm_set_ps(0.0f, z, y, x);
            // create an __m128 vector from the components of the input Vector3, with padding
            __m128 bVec    = _mm_set_ps(0.0f, b.z, b.y, b.x);
            // oerform element-wise multiplication of the two vectors
            __m128 result  = _mm_mul_ps(thisVec, bVec);
            
            // temporary array to hold the result (we need to extract the data from the SIMD register)
            float res[4];
            // store the result from the SIMD register into the array
            _mm_storeu_ps(res, result);

            // return a new Vector3 constructed from the result, ignoring the padding (res[3])
            return Vector3(res[0], res[1], res[2]);
        #else
            return Vector3(
                x * b.x,
                y * b.y,
                z * b.z
            );
        #endif
        }
        
        static Vector3 Min(const Vector3& a, const Vector3& b)
        {
            return Vector3(
                a.x < b.x ? a.x : b.x,
                a.y < b.y ? a.y : b.y,
                a.z < b.z ? a.z : b.z
            );
        }
        
        static Vector3 Max(const Vector3& a, const Vector3& b)
        {
            return Vector3(
                a.x > b.x ? a.x : b.x,
                a.y > b.y ? a.y : b.y,
                a.z > b.z ? a.z : b.z
            );
        }

        void operator*=(const Vector3& b)
        {
            *this = *this * b;
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

        // test for equality without using epsilon
        bool operator==(const Vector3& rhs) const
        {
            return x == rhs.x && y == rhs.y && z == rhs.z;
        }

        // test for inequality without using epsilon
        bool operator!=(const Vector3& rhs) const
        {
            return !(*this == rhs);
        }

        // return negation
        Vector3 operator -() const { return Vector3(-x, -y, -z); }

        bool IsNaN() const    { return std::isnan(x) || std::isnan(y) || std::isnan(z); }
        bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }
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

    // Reverse order operators
    inline  Vector3 operator*(float lhs, const Vector3& rhs) { return rhs * lhs; }
}
