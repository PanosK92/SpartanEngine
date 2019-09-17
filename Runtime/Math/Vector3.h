/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==================
#include <string>
#include "../Core/EngineDefs.h"
#include "MathHelper.h"
//=============================

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
            if (!Equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
                const auto length_inverted = 1.0f / Sqrt(length_squared);
                x *= length_inverted;
                y *= length_inverted;
                z *= length_inverted;
            }
        };

        // Return normalized vector
        [[nodiscard]] Vector3 Normalized() const
		{
            const auto length_squared = LengthSquared();
            if (!Equals(length_squared, 1.0f) && length_squared > 0.0f)
            {
                const auto length_inverted = 1.0f / Sqrt(length_squared);
                return (*this) * length_inverted;
            }
            else
                return *this;
		}

        // Returns normalized vector
		static Vector3 Normalize(const Vector3& v) { return v.Normalized(); }

        // Returns the dot product
		static float Dot(const Vector3& v1, const Vector3& v2)	{ return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z); }
        // Returns the dot product
        [[nodiscard]] float Dot(const Vector3& rhs) const						{ return x * rhs.x + y * rhs.y + z * rhs.z; }

        // Returns the cross product
		static Vector3 Cross(const Vector3& v1, const Vector3& v2)
		{
            const float x = v1.y * v2.z - v2.y * v1.z;
            const float y = -(v1.x * v2.z - v2.x * v1.z);
            const float z = v1.x * v2.y - v2.x * v1.y;

			return Vector3(x, y, z);
		}
        // Returns the cross product
        [[nodiscard]] Vector3 Cross(const Vector3& v2) const { return Cross(*this, v2); }

        // Returns the length
        [[nodiscard]] float Length() const		    { return Sqrt(x * x + y * y + z * z); }
        // Returns the squared length
        [[nodiscard]] float LengthSquared() const   { return x * x + y * y + z * z; }

        // Returns the distance between to vectors
        static float Distance(const Vector3& a, const Vector3& b)           { return (b - a).Length(); }
        // Returns the squared distance between to vectors
        static float DistanceSquared(const Vector3& a, const Vector3& b)    { return (b - a).LengthSquared(); }

        // Floor
		void Floor()
		{
            x = floor(x);
            y = floor(y);
            z = floor(z);
		}

        // Return absolute vector
        [[nodiscard]] Vector3 Absolute() const { return Vector3(Abs(x), Abs(y), Abs(z)); }

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
