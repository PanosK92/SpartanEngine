/*
Copyright(c) 2016 Panos Karabelas

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

//= INCLUDES ==========
#include "Vector3.h"
#include "MathHelper.h"
//=====================

namespace Directus
{
	namespace Math
	{
		class Matrix;
		class Vector3;

		class DllExport Quaternion
		{
		public:
			float x;
			float y;
			float z;
			float w;

			Quaternion();
			Quaternion(float x, float y, float z, float w);
			~Quaternion();

			//= FROM ====================================================================
			static Quaternion FromAngleAxis(float angle, const Vector3& axis);
			void FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);
			static Quaternion FromEulerAngles(const Vector3& eulerAngles);
			static Quaternion FromEulerAngles(float x, float y, float z);
			static Quaternion FromRotationMatrix(const Matrix& matrix);
			//===========================================================================

			//= TO ======================================================================
			Vector3 ToEulerAngles() const;
			Matrix RotationMatrix() const;

			float Yaw() const { return ToEulerAngles().y; }
			float Pitch() const { return ToEulerAngles().x; }
			float Roll() const { return ToEulerAngles().z; }
			//===========================================================================

			void FromRotationTo(const Vector3& start, const Vector3& end);
			bool FromLookRotation(const Vector3& direction, const Vector3& upDirection) const;

			Quaternion Conjugate() const { return Quaternion(w, -x, -y, -z); }
			float LengthSquared() const { return w * w + x * x + y * y + z * z; }

			void Normalize()
			{
				float lenSquared = LengthSquared();
				if (!Equals(lenSquared, 1.0f) && lenSquared > 0.0f)
				{
					float invLen = 1.0f / sqrtf(lenSquared);
					w *= invLen;
					x *= invLen;
					y *= invLen;
					z *= invLen;
				}
			}

			Quaternion Normalized() const
			{
				float lenSquared = LengthSquared();
				if (!Equals(lenSquared, 1.0f) && lenSquared > 0.0f)
				{
					float invLen = 1.0f / sqrtf(lenSquared);
					return *this * invLen;
				}

				return *this;
			}

			Quaternion Inverse() const;

			/*------------------------------------------------------------------------------
										[OPERATORS]
			------------------------------------------------------------------------------*/
			Quaternion& operator =(const Quaternion& rhs)
			{
				w = rhs.w;
				x = rhs.x;
				y = rhs.y;
				z = rhs.z;

				return *this;
			}

			Quaternion operator*(const Quaternion& rhs) const
			{
				return Quaternion(
					w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
					w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
					w * rhs.y + y * rhs.w + z * rhs.x - x* rhs.z,
					w * rhs.z + z * rhs.w + x * rhs.y - y * rhs.x
				);
			}

			Vector3 operator*(const Vector3& rhs) const
			{
				Vector3 qVec(x, y, z);
				Vector3 cross1(qVec.Cross(rhs));
				Vector3 cross2(qVec.Cross(cross1));

				return rhs + 2.0f * (cross1 * w + cross2);
			}

			Quaternion& operator *=(float rhs)
			{
				w *= rhs;
				x *= rhs;
				y *= rhs;
				z *= rhs;

				return *this;
			}

			Quaternion operator *(float rhs) const { return Quaternion(w * rhs, x * rhs, y * rhs, z * rhs); }

			bool operator==(const Quaternion& b) const
			{
				if (x == b.x && y == b.y && z == b.z && w == b.w)
					return true;

				return false;
			}

			bool operator!=(const Quaternion& b) const
			{
				if (x != b.x || y != b.y || z != b.z || w != b.w)
					return true;

				return false;
			}

			void operator*=(const Quaternion& b)
			{
				this->x *= b.x;
				this->y *= b.y;
				this->z *= b.z;
				this->w *= b.w;
			}

			static const Quaternion Identity;
		};

		inline DllExport Vector3 operator*(const Vector3& lhs, const Quaternion& rhs) { return rhs * lhs; }
		inline DllExport Quaternion operator*(float lhs, const Quaternion& rhs) { return rhs * lhs; }
	}
}
