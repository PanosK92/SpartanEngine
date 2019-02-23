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

//= INCLUDES =======
#include "Vector3.h"
//==================

namespace Directus::Math
{
	class ENGINE_CLASS Quaternion
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
		~Quaternion() {}

		// Creates a new Quaternion from the specified axis and angle.	
		// The angle in radians.
		// The axis of rotation.
		static Quaternion FromAngleAxis(float angle, const Vector3& axis)
		{
			float half	= angle * 0.5f;
			float sin	= sinf(half);
			float cos	= cosf(half);
			return Quaternion(axis.x * sin, axis.y * sin, axis.z * sin, cos);
		}
		void FromAxes(const Vector3& xAxis, const Vector3& yAxis, const Vector3& zAxis);

		// Creates a new Quaternion from the specified yaw, pitch and roll angles.
		// Yaw around the y axis in radians.
		// Pitch around the x axis in radians.
		// Roll around the z axis in radians.
		static Quaternion FromYawPitchRoll(float yaw, float pitch, float roll)
		{
			float halfRoll	= roll * 0.5f;
			float halfPitch = pitch * 0.5f;
			float halfYaw	= yaw * 0.5f;

			float sinRoll	= sin(halfRoll);
			float cosRoll	= cos(halfRoll);
			float sinPitch	= sin(halfPitch);
			float cosPitch	= cos(halfPitch);
			float sinYaw	= sin(halfYaw);
			float cosYaw	= cos(halfYaw);

			return Quaternion(
				cosYaw * sinPitch * cosRoll + sinYaw * cosPitch * sinRoll,
				sinYaw * cosPitch * cosRoll - cosYaw * sinPitch * sinRoll,
				cosYaw * cosPitch * sinRoll - sinYaw * sinPitch * cosRoll,
				cosYaw * cosPitch * cosRoll + sinYaw * sinPitch * sinRoll
			);
		}
		// Euler angles to quaternion (input in degrees)
		static Quaternion FromEulerAngles(const Vector3& rotation) { return FromYawPitchRoll(rotation.y * Helper::DEG_TO_RAD, rotation.x * Helper::DEG_TO_RAD, rotation.z * Helper::DEG_TO_RAD); }
		static Quaternion FromEulerAngles(float rotationX, float rotationY, float rotationZ) { return FromYawPitchRoll(rotationY * Helper::DEG_TO_RAD, rotationX * Helper::DEG_TO_RAD, rotationZ * Helper::DEG_TO_RAD); }

		// Returns Euler angles in degrees
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
		float Yaw() const	{ return ToEulerAngles().y; }

		// Returns pitch in degrees
		float Pitch() const { return ToEulerAngles().x; }

		// Returns roll in degrees
		float Roll() const	{ return ToEulerAngles().z; }

		static Quaternion FromToRotation(const Vector3& start, const Vector3& end)
		{
			Vector3 normStart	= start.Normalized();
			Vector3 normEnd		= end.Normalized();
			float d				= normStart.Dot(normEnd);

			if (d > -1.0f + Helper::M_EPSILON)
			{
				Vector3 c = normStart.Cross(normEnd);
				float s = sqrtf((1.0f + d) * 2.0f);
				float invS = 1.0f / s;

				return Quaternion(
					c.x * invS,
					c.y * invS,
					c.z * invS,
					0.5f * s);
			}
			else
			{
				Vector3 axis = Vector3::Right.Cross(normStart);
				if (axis.Length() < Helper::M_EPSILON)
				{
					axis = Vector3::Up.Cross(normStart);
				}

				return FromAngleAxis(180.0f, axis);
			}
		}

		static Quaternion FromLookRotation(const Vector3& direction, const Vector3& upDirection = Vector3::Up)
		{
			Quaternion ret;
			Vector3 forward = direction.Normalized();

			Vector3 v = forward.Cross(upDirection);
			if (v.LengthSquared() >= Helper::M_EPSILON)
			{
				v.Normalize();
				Vector3 up = v.Cross(forward);
				Vector3 right = up.Cross(forward);
				ret.FromAxes(right, up, forward);
			}
			else
				ret.FromToRotation(Vector3::Forward, forward);

			return ret;
		}

		static Quaternion FromToRotation(const Quaternion& start, const Quaternion& end)
		{
			return Inverse(start) * end;
		}

		Quaternion Conjugate() const	{ return Quaternion(w, -x, -y, -z); }
		float LengthSquared() const		{ return (x * x) + (y * y) + (z * z) + (w * w); }

		// Scales the quaternion magnitude to unit length
		void Normalize()
		{
			float inverseLength = 1.0f / (float)Helper::Sqrt(LengthSquared());
			x *= inverseLength;
			y *= inverseLength;
			z *= inverseLength;
			w *= inverseLength;
		}

		// Scales the quaternion magnitude to unit length
		Quaternion Normalized() const
		{	
			float inverseLength = 1.0f / (float)Helper::Sqrt(LengthSquared());
			return Quaternion(x * inverseLength, y * inverseLength, z * inverseLength, w * inverseLength);
		}

		// Returns the inverse quaternion which represents the opposite rotation
		static Quaternion Inverse(const Quaternion& q)
		{
			float inverseLength = 1.0f / (float)Helper::Sqrt(q.LengthSquared());
			return Quaternion(
				-q.x * inverseLength,
				-q.y * inverseLength,
				-q.z * inverseLength,
				q.w * inverseLength
			);
		}

		// Returns the inverse quaternion which represents the opposite rotation
		Quaternion Inverse() const 
		{ 
			float inverseLength = 1.0f / (float)Helper::Sqrt(LengthSquared());
			return Quaternion(
				-x * inverseLength,
				-y * inverseLength,
				-z * inverseLength,
				w * inverseLength
			);
		}

		//= ASSIGNMENT ==============================
		Quaternion& operator =(const Quaternion& rhs)
		{
			w = rhs.w;
			x = rhs.x;
			y = rhs.y;
			z = rhs.z;

			return *this;
		}
		//===========================================

		//= MULTIPLICATION ==============================================================================
		Quaternion operator*(const Quaternion& rhs) const
		{
			float num4	= rhs.x;
			float num3	= rhs.y;
			float num2	= rhs.z;
			float num	= rhs.w;
			float num12 = (y * num2) - (z * num3);
			float num11 = (z * num4) - (x * num2);
			float num10 = (x * num3) - (y * num4);
			float num9 = ((x * num4) + (y * num3)) + (z * num2);
			Quaternion quaternion;
			quaternion.x = ((x * num) + (num4 * w)) + num12;
			quaternion.y = ((y * num) + (num3 * w)) + num11;
			quaternion.z = ((z * num) + (num2 * w)) + num10;
			quaternion.w = (w * num) - num9;
			return quaternion;
		}

		void operator*=(const Quaternion& rhs)
		{
			float num4 = rhs.x;
			float num3 = rhs.y;
			float num2 = rhs.z;
			float num = rhs.w;
			float num12 = (y * num2) - (z * num3);
			float num11 = (z * num4) - (x * num2);
			float num10 = (x * num3) - (y * num4);
			float num9 = ((x * num4) + (y * num3)) + (z * num2);
			x	= ((x * num) + (num4 * w)) + num12;
			y	= ((y * num) + (num3 * w)) + num11;
			z	= ((z * num) + (num2 * w)) + num10;
			w	= (w * num) - num9;
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
		//===============================================================================================

		//= COMPARISON =============================================================================
		// Test for equality using epsilon
		bool operator ==(const Quaternion& rhs) const
		{
			return Helper::Equals(w, rhs.w) && Helper::Equals(x, rhs.x) && Helper::Equals(y, rhs.y) && Helper::Equals(z, rhs.z);
		}

		// Test for inequality using epsilon
		bool operator !=(const Quaternion& rhs) const 
		{ 
			return !Helper::Equals(w, rhs.w) || !Helper::Equals(x, rhs.x) || !Helper::Equals(y, rhs.y) || !Helper::Equals(z, rhs.z);
		}
		//==========================================================================================

		std::string ToString() const;

		float x, y, z, w;

		static const Quaternion Identity;
	};

	// Reverse order operators
	inline ENGINE_CLASS Vector3 operator*(const Vector3& lhs, const Quaternion& rhs) { return rhs * lhs; }
	inline ENGINE_CLASS Quaternion operator*(float lhs, const Quaternion& rhs) { return rhs * lhs; }
}
