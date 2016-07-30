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
#include <sstream>
#include "Vector3.h"
#include "Vector4.h"
#include "Matrix.h"
#include "Quaternion.h"
//=====================

namespace Directus
{
	namespace Math
	{
		const Vector3 Vector3::Zero(0.0f, 0.0f, 0.0f);
		const Vector3 Vector3::Left(-1.0f, 0.0f, 0.0f);
		const Vector3 Vector3::Right(1.0f, 0.0f, 0.0f);
		const Vector3 Vector3::Up(0.0f, 1.0f, 0.0f);
		const Vector3 Vector3::Down(0.0f, -1.0f, 0.0f);
		const Vector3 Vector3::Forward(0.0f, 0.0f, 1.0f);
		const Vector3 Vector3::Back(0.0f, 0.0f, -1.0f);
		const Vector3 Vector3::One(1.0f, 1.0f, 1.0f);
		const Vector3 Vector3::Infinity(INFINITY, INFINITY, INFINITY);
		const Vector3 Vector3::InfinityNeg(-INFINITY, -INFINITY, -INFINITY);

		Vector3 Vector3::Transform(Vector3 vector, Matrix matrix)
		{
			Vector4 vWorking;

			vWorking.x = (vector.x * matrix.m00) + (vector.y * matrix.m10) + (vector.z * matrix.m20) + matrix.m30;
			vWorking.y = (vector.x * matrix.m01) + (vector.y * matrix.m11) + (vector.z * matrix.m21) + matrix.m31;
			vWorking.z = (vector.x * matrix.m02) + (vector.y * matrix.m12) + (vector.z * matrix.m22) + matrix.m32;
			vWorking.w = 1 / ((vector.x * matrix.m03) + (vector.y * matrix.m13) + (vector.z * matrix.m23) + matrix.m33);

			return Vector3(vWorking.x * vWorking.w, vWorking.y * vWorking.w, vWorking.z * vWorking.w);
		}

		Vector3 Vector3::QuaternionToEuler(Quaternion quaternion)
		{
			Vector3 vec;
			double sqw = quaternion.w * quaternion.w;
			double sqx = quaternion.x * quaternion.x;
			double sqy = quaternion.y * quaternion.y;
			double sqz = quaternion.z * quaternion.z;

			vec.x = atan2l(2.0 * (quaternion.y * quaternion.z + quaternion.x * quaternion.w), (-sqx - sqy + sqz + sqw));
			vec.y = asinl(-2.0 * (quaternion.x * quaternion.z - quaternion.y * quaternion.w));
			vec.z = atan2l(2.0 * (quaternion.x * quaternion.y + quaternion.z * quaternion.w), (sqx - sqy - sqz + sqw));

			return vec;
		}

		std::string Vector3::ToString()
		{
			std::ostringstream os;
			os << x << ", " << y << ", " << z;

			return os.str();
		}

		Vector3 Vector3::operator*(const Quaternion& b)
		{
			float num = b.x * 2.0f;
			float num2 = b.y * 2.0f;
			float num3 = b.z * 2.0f;
			float num4 = b.x * num;
			float num5 = b.y * num2;
			float num6 = b.z * num3;
			float num7 = b.x * num2;
			float num8 = b.x * num3;
			float num9 = b.y * num3;
			float num10 = b.w * num;
			float num11 = b.w * num2;
			float num12 = b.w * num3;

			Vector3 result;
			result.x = (1.0f - (num5 + num6)) * x + (num7 - num12) * y + (num8 + num11) * z;
			result.y = (num7 + num12) * x + (1.0f - (num4 + num6)) * y + (num9 - num10) * z;
			result.z = (num8 - num11) * x + (num9 + num10) * y + (1.0f - (num4 + num5)) * z;
			return result;
		}
	}
}
