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

//= INCLUDES ==============
#include "Quaternion.h"
#include "MathHelper.h"
#include <math.h>
#include "Vector3.h"
#include "../Math/Matrix.h"
//=========================

//= NAMESPACES =====
using namespace std;
//==================

//= Based on Urho3D
//= Based also on =====================================================================//
// http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/index.htm //
// Heading	-> Yaw		-> Y-axis													   //
// Attitude	-> Pitch	-> X-axis													   //
// Bank		-> Roll		-> Z-axis													   //
//=====================================================================================//


namespace Directus
{
	namespace Math
	{
		const Quaternion Quaternion::Identity(0, 0, 0, 1);

		Quaternion::Quaternion()
		{
			this->x = 0;
			this->y = 0;
			this->z = 0;
			this->w = 1;
		}

		// Constructs new Quaternion with given x,y,z,w components.
		Quaternion::Quaternion(float x, float y, float z, float w)
		{
			this->x = x;
			this->y = y;
			this->z = z;
			this->w = w;
		}

		Quaternion::~Quaternion()
		{

		}

		//= FROM =========================================================================================
		Quaternion Quaternion::FromAngleAxis(float angle, const Vector3& axis)
		{
			Vector3 normAxis = axis.Normalized();
			angle *= DEG_TO_RAD_2;
			float sinAngle = sinf(angle);
			float cosAngle = cosf(angle);

			Quaternion q;
			q.w = cosAngle;
			q.x = normAxis.x * sinAngle;
			q.y = normAxis.y * sinAngle;
			q.z = normAxis.z * sinAngle;

			return q;
		}

		void Quaternion::FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis)
		{
			Matrix matrix(
				xAxis.x, yAxis.x, zAxis.x, 0,
				xAxis.y, yAxis.y, zAxis.y, 0,
				xAxis.z, yAxis.z, zAxis.z, 0,
				0, 0, 0, 1
			);

			FromRotationMatrix(matrix);
		}


		Quaternion Quaternion::FromEulerAngles(const Vector3& eulerAngles)
		{
			return FromEulerAngles(eulerAngles.x, eulerAngles.y, eulerAngles.z);
		}

		Quaternion Quaternion::FromEulerAngles(float x, float y, float z)
		{
			x *= DEG_TO_RAD_2;
			y *= DEG_TO_RAD_2;
			z *= DEG_TO_RAD_2;

			float sinX = sinf(x);
			float cosX = cosf(x);
			float sinY = sinf(y);
			float cosY = cosf(y);
			float sinZ = sinf(z);
			float cosZ = cosf(z);

			Quaternion q;
			q.w = cosY * cosX * cosZ + sinY * sinX * sinZ;
			q.x = cosY * sinX * cosZ + sinY * cosX * sinZ;
			q.y = sinY * cosX * cosZ - cosY * sinX * sinZ;
			q.z = cosY * cosX * sinZ - sinY * sinX * cosZ;

			return q;
		}

		Quaternion Quaternion::FromRotationMatrix(const Matrix& matrix)
		{
			Matrix mTransposed = matrix.Transposed();
			Quaternion q;

			float t = mTransposed.m00 + mTransposed.m11 + mTransposed.m22;

			if (t > 0.0f)
			{
				float invS = 0.5f / sqrtf(1.0f + t);

				q.x = (mTransposed.m21 - mTransposed.m12) * invS;
				q.y = (mTransposed.m02 - mTransposed.m20) * invS;
				q.z = (mTransposed.m10 - mTransposed.m01) * invS;
				q.w = 0.25f / invS;
			}
			else
			{
				if (mTransposed.m00 > mTransposed.m11 && mTransposed.m00 > mTransposed.m22)
				{
					float invS = 0.5f / sqrtf(1.0f + mTransposed.m00 - mTransposed.m11 - mTransposed.m22);

					q.x = 0.25f / invS;
					q.y = (mTransposed.m01 + mTransposed.m10) * invS;
					q.z = (mTransposed.m20 + mTransposed.m02) * invS;
					q.w = (mTransposed.m21 - mTransposed.m12) * invS;
				}
				else if (mTransposed.m11 > mTransposed.m22)
				{
					float invS = 0.5f / sqrtf(1.0f + mTransposed.m11 - mTransposed.m00 - mTransposed.m22);

					q.x = (mTransposed.m01 + mTransposed.m10) * invS;
					q.y = 0.25f / invS;
					q.z = (mTransposed.m12 + mTransposed.m21) * invS;
					q.w = (mTransposed.m02 - mTransposed.m20) * invS;
				}
				else
				{
					float invS = 0.5f / sqrtf(1.0f + mTransposed.m22 - mTransposed.m00 - mTransposed.m11);

					q.x = (mTransposed.m02 + mTransposed.m20) * invS;
					q.y = (mTransposed.m12 + mTransposed.m21) * invS;
					q.z = 0.25f / invS;
					q.w = (mTransposed.m10 - mTransposed.m01) * invS;
				}
			}

			return q;
		}
		//================================================================================

		//= TO ===========================================================================
		// Returns the euler angle representation of the rotation.
		Vector3 Quaternion::ToEulerAngles() const
		{
			// Derivation from http://www.geometrictools.com/Documentation/EulerAngles.pdf
			// Order of rotations: Z first, then X, then Y
			float check = 2.0f * (-y * z + w * x);

			if (check < -0.995f)
			{
				return Vector3
				(
					-90.0f,
					0.0f,
					-atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * RAD_TO_DEG
				);
			}

			if (check > 0.995f)
			{
				return Vector3
				(
					90.0f,
					0.0f,
					atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * RAD_TO_DEG
				);
			}

			return Vector3
			(
				asinf(check) * RAD_TO_DEG,
				atan2f(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)) * RAD_TO_DEG,
				atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z)) * RAD_TO_DEG
			);
		}

		Matrix Quaternion::RotationMatrix() const
		{
			return Matrix(
				1.0f - 2.0f * y * y - 2.0f * z * z,
				2.0f * x * y + 2.0f * w * z,
				2.0f * x * z - 2.0f * w * y,
				0.0f,
				2.0f * x * y - 2.0f * w * z,
				1.0f - 2.0f * x * x - 2.0f * z * z,
				2.0f * y * z + 2.0f * w * x,
				0.0f,
				2.0f * x * z + 2.0f * w * y,
				2.0f * y * z - 2.0f * w * x,
				1.0f - 2.0f * x * x - 2.0f * y * y,
				0.0f,
				0.0f,
				0.0f,
				0.0f,
				1.0f
			);
		}
		//================================================================================

		void Quaternion::FromRotationTo(const Vector3& start, const Vector3& end)
		{
			Vector3 normStart = start.Normalized();
			Vector3 normEnd = end.Normalized();
			float d = normStart.Dot(normEnd);

			if (d > -1.0f + M_EPSILON)
			{
				Vector3 c = normStart.Cross(normEnd);
				float s = sqrtf((1.0f + d) * 2.0f);
				float invS = 1.0f / s;

				x = c.x * invS;
				y = c.y * invS;
				z = c.z * invS;
				w = 0.5f * s;
			}
			else
			{
				Vector3 axis = Vector3::Right.Cross(normStart);
				if (axis.Length() < M_EPSILON)
					axis = Vector3::Up.Cross(normStart);

				FromAngleAxis(180.0f, axis);
			}
		}

		bool Quaternion::FromLookRotation(const Vector3& direction, const Vector3& upDirection) const
		{
			Quaternion ret;
			Vector3 forward = direction.Normalized();

			Vector3 v = forward.Cross(upDirection);
			if (v.LengthSquared() >= M_EPSILON)
			{
				v.Normalize();
				Vector3 up = v.Cross(forward);
				Vector3 right = up.Cross(forward);
				ret.FromAxes(right, up, forward);
			}
			else
				ret.FromRotationTo(Vector3::Forward, forward);

			return true;
		}

		Quaternion Quaternion::Inverse() const
		{
			float lenSquared = LengthSquared();

			if (lenSquared == 1.0f)
				return Conjugate();

			if (lenSquared >= M_EPSILON)
				return Conjugate() * (1.0f / lenSquared);

			// impemented this here because Identity (static)
			// doesnt play well with dllexport
			return Identity;
		}
	}
}
