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

		//= FROM =========================================================================================
		Quaternion Quaternion::FromAngleAxis(float angle, const Vector3& axis)
		{
			Vector3 normAxis = axis.Normalized();
			angle *= DEG_TO_RAD_2;
			float sinAngle = sinf(angle);
			float cosAngle = cosf(angle);

			return Quaternion(
				normAxis.x * sinAngle,
				normAxis.y * sinAngle,
				normAxis.z * sinAngle,
				cosAngle
			);

		}

		Quaternion Quaternion::FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis)
		{
			return Matrix(
				xAxis.x, yAxis.x, zAxis.x, 0.0f,
				xAxis.y, yAxis.y, zAxis.y, 0.0f,
				xAxis.z, yAxis.z, zAxis.z, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f
			).GetRotation();
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

			return Quaternion(
				cosY * sinX * cosZ + sinY * cosX * sinZ,
				sinY * cosX * cosZ - cosY * sinX * sinZ,
				cosY * cosX * sinZ - sinY * sinX * cosZ,
				cosY * cosX * cosZ + sinY * sinX * sinZ
			);
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
				return (1.0f / lenSquared) * Conjugate();

			// impemented this here because Identity (static)
			// doesnt play well with dllexport
			return Identity;
		}
	}
}
