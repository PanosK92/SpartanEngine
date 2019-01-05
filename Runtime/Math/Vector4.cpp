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

//= INCLUDES =======
#include "Vector4.h"
#include "Matrix.h"
//==================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus::Math
{
	const Vector4 Vector4::One(1.0f, 1.0f, 1.0f, 1.0f);
	const Vector4 Vector4::Zero(0.0f, 0.0f, 0.0f, 0.0f);

	Vector4::Vector4(const Vector3& value, float w)
	{
		this->x = value.x;
		this->y = value.y;
		this->z = value.z;
		this->w = w;
	}

	Vector4::Vector4(const Vector3& value)
	{
		this->x = value.x;
		this->y = value.y;
		this->z = value.z;
		this->w = 0.0f;
	}

	Vector4 Vector4::Transform(const Vector3& lhs, const Matrix& rhs)
	{
		Vector4 result;
		result.x = (lhs.x * rhs.m00) + (lhs.y * rhs.m10) + (lhs.z * rhs.m20) + rhs.m30;
		result.y = (lhs.x * rhs.m01) + (lhs.y * rhs.m11) + (lhs.z * rhs.m21) + rhs.m31;
		result.z = (lhs.x * rhs.m02) + (lhs.y * rhs.m12) + (lhs.z * rhs.m22) + rhs.m32;
		result.w = 1 / ((lhs.x * rhs.m03) + (lhs.y * rhs.m13) + (lhs.z * rhs.m23) + rhs.m33);

		return result;
	}

	string Vector4::ToString() const
	{
		char tempBuffer[200];
		sprintf_s(tempBuffer, "X:%f, Y:%f, Z:%f, W:%f", x, y, z, w);
		return string(tempBuffer);
	}
}
