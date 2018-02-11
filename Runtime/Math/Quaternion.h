/*
Copyright(c) 2016-2018 Panos Karabelas

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
		class ENGINE_CLASS Quaternion
		{
		public:
			// Constructs an identity quaternion
			Quaternion::Quaternion()
			{
				x = 0;
				y = 0;
				z = 0;
				w = 1;
			}

			// Constructs a new quaternion with the given components
			Quaternion::Quaternion(float x, float y, float z, float w)
			{
				this->x = x;
				this->y = y;
				this->z = z;
				this->w = w;
			}

			~Quaternion() {}

			//= FROM ====================================================================
			static Quaternion FromAngleAxis(float angle, const Vector3& axis)
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

			void Quaternion::FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);
			static Quaternion FromEulerAngles(const Vector3& eulerAngles) { return FromEulerAngles(eulerAngles.x, eulerAngles.y, eulerAngles.z); }

			static Quaternion FromEulerAngles(float x, float y, float z)
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
			//===========================================================================

			//= TO ======================================================================
			Vector3 ToEulerAngles() const
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

			float Yaw() const { return ToEulerAngles().y; }
			float Pitch() const { return ToEulerAngles().x; }
			float Roll() const { return ToEulerAngles().z; }
			//===========================================================================

			void FromRotationTo(const Vector3& start, const Vector3& end)
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
					{
						axis = Vector3::Up.Cross(normStart);
					}

					FromAngleAxis(180.0f, axis);
				}
			}

			bool FromLookRotation(const Vector3& direction, const Vector3& upDirection) const
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

			Quaternion Conjugate() const { return Quaternion(w, -x, -y, -z); }
			float LengthSquared() const { return w * w + x * x + y * y + z * z; }

			//= NORMALIZATION ============================================================================
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
			//============================================================================================

			//= ASSIGNMENT ===============================================================================
			Quaternion& operator =(const Quaternion& rhs)
			{
				w = rhs.w;
				x = rhs.x;
				y = rhs.y;
				z = rhs.z;

				return *this;
			}
			//============================================================================================

			//= MULTIPLICATION ===========================================================================
			Quaternion operator*(const Quaternion& rhs) const
			{
				return Quaternion(
					w * rhs.w - x * rhs.x - y * rhs.y - z * rhs.z,
					w * rhs.x + x * rhs.w + y * rhs.z - z * rhs.y,
					w * rhs.y + y * rhs.w + z * rhs.x - x* rhs.z,
					w * rhs.z + z * rhs.w + x * rhs.y - y * rhs.x
				);
			}

			void operator*=(const Quaternion& rhs)
			{
				x *= rhs.x;
				y *= rhs.y;
				z *= rhs.z;
				w *= rhs.w;
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
			//===============================================================================================================

			//= COMPARISON ==================================================================================================
			bool operator==(const Quaternion& b) const{return (x == b.x && y == b.y && z == b.z && w == b.w) ? true : false;}
			bool operator!=(const Quaternion& b) const { return !(*this == b); }
			//===============================================================================================================

			std::string ToString() const;

			float x, y, z, w;

			static const Quaternion Identity;
		};

		// Reverse order operators
		inline ENGINE_CLASS Vector3 operator*(const Vector3& lhs, const Quaternion& rhs) { return rhs * lhs; }
		inline ENGINE_CLASS Quaternion operator*(float lhs, const Quaternion& rhs) { return rhs * lhs; }
	}
}
