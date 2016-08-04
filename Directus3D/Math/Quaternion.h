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

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace Directus
{
	namespace Math
	{
		class Matrix;
		class Vector3;

		class __declspec(dllexport) Quaternion
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
			static Quaternion CreateFromAxisAngle(const Vector3& axis, float angle);
			static Quaternion FromEulerAngles(const Vector3& eulerAngles);
			static Quaternion FromEulerAngles(float x, float y, float z);
			static Quaternion CreateFromRotationMatrix(const Matrix& matrix);	
			//===========================================================================

			//= TO ======================================================================
			Vector3 ToEulerAngles();
			float Yaw();
			float Pitch();
			float Roll();
			//===========================================================================

			Quaternion Conjugate();
			float Magnitude();
			Quaternion Normalize();
			Quaternion Inverse();
			Matrix RotationMatrix();

			/*------------------------------------------------------------------------------
										[OPERATORS]
			------------------------------------------------------------------------------*/
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
				
			bool Quaternion::operator==(const Quaternion& b)
			{
				if (x == b.x && y == b.y && z == b.z && w == b.w)
					return true;

				return false;
			}

			bool Quaternion::operator!=(const Quaternion& b)
			{
				if (x != b.x || y != b.y || z != b.z || w != b.w)
					return true;

				return false;
			}

			void Quaternion::operator*=(const Quaternion& b)
			{
				this->x *= b.x;
				this->y *= b.y;
				this->z *= b.z;
				this->w *= b.w;
			}

			static const Quaternion Identity;
		};

		inline __declspec(dllexport) Vector3 operator*(const Vector3& lhs, const Quaternion& rhs) { return rhs * lhs; }
	}
}
