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

namespace Directus::Math
{
	class ENGINE_CLASS Vector3
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

		//= NORMALIZATION ===================================================
		Vector3 Normalized() const
		{
			float fentity = Length();
			fentity = 1.0f / fentity;
			return Vector3(x * fentity, y * fentity, z * fentity);
		}

		void Normalize()
		{
			float fentity = Length();
			fentity = 1.0f / fentity;
			x *= fentity;
			y *= fentity;
			z *= fentity;
		};
		static Vector3 Normalize(const Vector3& v) { return v.Normalized(); }
		//===================================================================

		//= DOT PRODUCT =============================================================================================
		static float Dot(const Vector3& v1, const Vector3& v2)	{ return (v1.x * v2.x + v1.y * v2.y + v1.z * v2.z); }
		float Dot(const Vector3& rhs) const						{ return x * rhs.x + y * rhs.y + z * rhs.z; }
		//===========================================================================================================

		//= CROSS PRODUCT =================================================
		static Vector3 Cross(const Vector3& v1, const Vector3& v2)
		{
			float x = v1.y * v2.z - v2.y * v1.z;
			float y = -(v1.x * v2.z - v2.x * v1.z);
			float z = v1.x * v2.y - v2.x * v1.y;

			return Vector3(x, y, z);
		}
		Vector3 Cross(const Vector3& v2) const { return Cross(*this, v2); }
		//=================================================================

		//= LENGTH ================================================================
		float Length() const		{ return Helper::Sqrt(x * x + y * y + z * z); }
		float LengthSquared() const { return x * x + y * y + z * z; }
		//=========================================================================

		//= MISC ===========================================================
		void Floor()
		{
			x = floorf(x);
			y = floorf(y);
			z = floorf(z);
		}
		Vector3 Absolute() const { return Vector3(abs(x), abs(y), abs(z)); }
		float Volume() const { return x * y * z; }
		//==================================================================

		//= MULTIPLICATION =======================
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

		//========================================

		//= ADDITION ==================================================================================
		Vector3 operator+(const Vector3& b) const { return Vector3(x + b.x, y + b.y, z + b.z); }
		Vector3 operator+(const float value) const { return Vector3(x + value, y + value, z + value); }

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
		//=============================================================================================

		//= SUBTRACTION =========================================================================
		Vector3 operator-(const Vector3& b) const { return Vector3(x - b.x, y - b.y, z - b.z); }
		Vector3 operator-(const float value) { return Vector3(x - value, y - value, z - value); }

		void operator-=(const Vector3& rhs)
		{
			x -= rhs.x;
			y -= rhs.y;
			z -= rhs.z;
		}
		//=======================================================================================

		//= DIVISION ===================================================================================
		Vector3 operator/(const Vector3& rhs) const { return Vector3(x / rhs.x, y / rhs.y, z / rhs.z); }
		Vector3 operator/(const float rhs) { return Vector3(x / rhs, y / rhs, z / rhs); }

		void operator/=(const Vector3& rhs)
		{
			x /= rhs.x;
			y /= rhs.y;
			z /= rhs.z;
		}
		//==============================================================================================

		//= COMPARISON =============================================================================
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

		// Test for equality using epsilon
		bool Equals(const Vector3& rhs) const
		{
			return Helper::Equals(x, rhs.x) && Helper::Equals(y, rhs.y) && Helper::Equals(z, rhs.z);
		}
		//==========================================================================================

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
		static const Vector3 Back;
		static const Vector3 One;
		static const Vector3 Infinity;
		static const Vector3 InfinityNeg;
	};

	// Reverse order operators
	inline ENGINE_CLASS Vector3 operator*(float lhs, const Vector3& rhs) { return rhs * lhs; }
}
