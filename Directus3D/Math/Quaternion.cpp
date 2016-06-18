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

//= INCLUDES ==========
#include "Quaternion.h"
#include "MathHelper.h"
#include <math.h>
#include "Vector3.h"

//=====================

//= Based on ==========================================================================//
// http://www.euclideanspace.com/maths/algebra/realNormedAlgebra/quaternions/index.htm //
// Heading	-> Yaw		-> Y-axis													   //
// Attitude	-> Pitch	-> X-axis													   //
// Bank		-> Roll		-> Z-axis													   //
//=====================================================================================//

namespace Directus
{
	namespace Math
	{
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

		Quaternion Quaternion::Identity()
		{
			return Quaternion(0, 0, 0, 1);
		}

		Quaternion Quaternion::Conjugate()
		{
			return Quaternion(-x, -y, -z, w);
		}

		float Quaternion::Magnitude()
		{
			return sqrtf(w * w + x * x + y * y + z * z);
		}

		Quaternion Quaternion::Normalize()
		{
			Quaternion q = Quaternion(x, y, z, w);
			float n = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;

			if (n != 1.0f)
			{
				float magnitude = Magnitude();
				q.w = q.w / magnitude;
				q.x = q.x / magnitude;
				q.y = q.y / magnitude;
				q.z = q.z / magnitude;
			}

			return q;
		}

		Quaternion Quaternion::Inverse()
		{
			return Quaternion(-x, -y, -z, w);
		}

		/*------------------------------------------------------------------------------
										[PROPERTIES]
		------------------------------------------------------------------------------*/
		float Quaternion::GetYaw()
		{
			return MathHelper::GetInstance().DegreesToRadians(ToEulerAngles().y);
		}

		float Quaternion::GetPitch()
		{
			return MathHelper::GetInstance().DegreesToRadians(ToEulerAngles().x);
		}

		float Quaternion::GetRoll()
		{
			return MathHelper::GetInstance().DegreesToRadians(ToEulerAngles().z);
		}

		/*------------------------------------------------------------------------------
										[CREATE FROM]
		------------------------------------------------------------------------------*/
		Quaternion Quaternion::FromEulerAngles(Vector3 rotation)
		{
			return FromEulerAngles(rotation.x, rotation.y, rotation.z);
		}


		Quaternion Quaternion::FromEulerAngles(float x, float y, float z)
		{
			// Order of rotations: Z first, then X, then Y (mimics typical FPS camera with gimbal lock at top/bottom)
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

		Quaternion Quaternion::CreateFromRotationMatrix(Matrix matrix)
		{
			matrix = Matrix::Transpose(matrix);
			Quaternion q;

			float t = matrix.m00 + matrix.m11 + matrix.m22;

			if (t > 0.0f)
			{
				float invS = 0.5f / sqrtf(1.0f + t);

				q.x = (matrix.m21 - matrix.m12) * invS;
				q.y = (matrix.m02 - matrix.m20) * invS;
				q.z = (matrix.m10 - matrix.m01) * invS;
				q.w = 0.25f / invS;
			}
			else
			{
				if (matrix.m00 > matrix.m11 && matrix.m00 > matrix.m22)
				{
					float invS = 0.5f / sqrtf(1.0f + matrix.m00 - matrix.m11 - matrix.m22);

					q.x = 0.25f / invS;
					q.y = (matrix.m01 + matrix.m10) * invS;
					q.z = (matrix.m20 + matrix.m02) * invS;
					q.w = (matrix.m21 - matrix.m12) * invS;
				}
				else if (matrix.m11 > matrix.m22)
				{
					float invS = 0.5f / sqrtf(1.0f + matrix.m11 - matrix.m00 - matrix.m22);

					q.x = (matrix.m01 + matrix.m10) * invS;
					q.y = 0.25f / invS;
					q.z = (matrix.m12 + matrix.m21) * invS;
					q.w = (matrix.m02 - matrix.m20) * invS;
				}
				else
				{
					float invS = 0.5f / sqrtf(1.0f + matrix.m22 - matrix.m00 - matrix.m11);

					q.x = (matrix.m02 + matrix.m20) * invS;
					q.y = (matrix.m12 + matrix.m21) * invS;
					q.z = 0.25f / invS;
					q.w = (matrix.m10 - matrix.m01) * invS;
				}
			}

			return q;
		}

		Quaternion Quaternion::CreateFromAxisAngle(Vector3 axis, float angle)
		{
			Vector3 normAxis = Vector3::Normalize(axis);
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

		Quaternion Quaternion::CreateFromYawPitchRoll(float yaw, float pitch, float roll)
		{
			float c1 = cosf(yaw / 2);
			float s1 = sinf(yaw / 2);
			float c2 = cosf(pitch / 2);
			float s2 = sinf(pitch / 2);
			float c3 = cosf(roll / 2);
			float s3 = sinf(roll / 2);
			float c1c2 = c1 * c2;
			float s1s2 = s1 * s2;

			Quaternion q;
			q.w = c1c2 * c3 - s1s2 * s3;
			q.x = c1c2 * s3 + s1s2 * c3;
			q.y = s1 * c2 * c3 + c1 * s2 * s3;
			q.z = c1 * s2 * c3 - s1 * c2 * s3;

			return q.Normalize();
		}

		/*------------------------------------------------------------------------------
										[CONVERT TO]
		------------------------------------------------------------------------------*/
		// Returns the euler angle representation of the rotation.
		Vector3 Quaternion::ToEulerAngles()
		{
			// Derivation from http://www.geometrictools.com/Documentation/EulerAngles.pdf
			// Order of rotations: Z first, then X, then Y
			float check = 2.0f * (-y * z + w * x);

			if (check < -0.995f)
			{
				return Vector3(
					-90.0f,
					0.0f,
					-atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * RAD_TO_DEG
				);
			}
			else if (check > 0.995f)
			{
				return Vector3(
					90.0f,
					0.0f,
					atan2f(2.0f * (x * z - w * y), 1.0f - 2.0f * (y * y + z * z)) * RAD_TO_DEG
				);
			}
			else
			{
				return Vector3(
					asinf(check) * RAD_TO_DEG,
					atan2f(2.0f * (x * z + w * y), 1.0f - 2.0f * (x * x + y * y)) * RAD_TO_DEG,
					atan2f(2.0f * (x * y + w * z), 1.0f - 2.0f * (x * x + z * z)) * RAD_TO_DEG
				);
			}
		}

		/*------------------------------------------------------------------------------
											[OPERATORS]
		------------------------------------------------------------------------------*/
		Quaternion Quaternion::operator*(Quaternion q)
		{
			Quaternion tmp;

			tmp.w = (q.w * w) - (q.x * x) - (q.y * y) - (q.z * z);
			tmp.x = (q.w * x) + (q.x * w) + (q.y * z) - (q.z * y);
			tmp.y = (q.w * y) + (q.y * w) + (q.z * x) - (q.x * z);
			tmp.z = (q.w * z) + (q.z * w) + (q.x * y) - (q.y * x);

			return tmp;
		}

		Vector3 Quaternion::operator*(Vector3 v)
		{
			float num = x * 2.0f;
			float num2 = y * 2.0f;
			float num3 = z * 2.0f;
			float num4 = x * num;
			float num5 = y * num2;
			float num6 = z * num3;
			float num7 = x * num2;
			float num8 = x * num3;
			float num9 = y * num3;
			float num10 = w * num;
			float num11 = w * num2;
			float num12 = w * num3;

			Vector3 result;
			result.x = (1.0f - (num5 + num6)) * v.x + (num7 - num12) * v.y + (num8 + num11) * v.z;
			result.y = (num7 + num12) * v.x + (1.0f - (num4 + num6)) * v.y + (num9 - num10) * v.z;
			result.z = (num8 - num11) * v.x + (num9 + num10) * v.y + (1.0f - (num4 + num5)) * v.z;
			return result;
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
	}
}
