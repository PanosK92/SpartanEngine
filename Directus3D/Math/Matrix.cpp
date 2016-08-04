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

//= INCLUDES ===========
#include "Matrix.h"
#include "Vector3.h"
#include "Quaternion.h"
//======================

namespace Directus
{
	namespace Math
	{
		const Matrix Matrix::Identity
		(
			1, 0, 0, 0,
			0, 1, 0, 0,
			0, 0, 1, 0,
			0, 0, 0, 1
		);

		Matrix Matrix::CreateScale(const Vector3& scale)
		{
			return CreateScale(scale.x, scale.y, scale.z);
		}

		Matrix Matrix::CreateTranslation(const Vector3& position)
		{
			return Matrix(
				1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				position.x, position.y, position.z, 1
			);
		}

		Matrix Matrix::CreateLookAtLH(const Vector3& eye, const Vector3& at, const Vector3& up)
		{
			Vector3 zaxis = Vector3::Normalize(at - eye);
			Vector3 xaxis = Vector3::Normalize(Vector3::Cross(up, zaxis));
			Vector3 yaxis = Vector3::Cross(zaxis, xaxis);

			return Matrix(
				xaxis.x, yaxis.x, zaxis.x, 0,
				xaxis.y, yaxis.y, zaxis.y, 0,
				xaxis.z, yaxis.z, zaxis.z, 0,
				-Vector3::Dot(xaxis, eye), -Vector3::Dot(yaxis, eye), -Vector3::Dot(zaxis, eye), 1.0f
			);
		}

		// based on D3DXMatrixDecompose
		void Matrix::Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation)
		{
			Matrix pm = *this;

			Matrix normalized;
			Vector3 vec;

			// compute the scaling part
			vec.x = pm.m00;
			vec.y = pm.m01;
			vec.z = pm.m02;
			scale.x = vec.Length();

			vec.x = pm.m10;
			vec.y = pm.m11;
			vec.z = pm.m12;
			scale.y = vec.Length();

			vec.x = pm.m20;
			vec.y = pm.m21;
			vec.z = pm.m22;
			scale.z = vec.Length();

			// compute the translation part
			translation.x = pm.m30;
			translation.y = pm.m31;
			translation.z = pm.m32;

			// let's calculate the rotation now
			if ((scale.x == 0.0f) || (scale.y == 0.0f) || (scale.z == 0.0f))
			{
				rotation = Quaternion::Identity;
				return;
			}

			normalized.m00 = pm.m00 / scale.x;
			normalized.m01 = pm.m01 / scale.x;
			normalized.m02 = pm.m02 / scale.x;
			normalized.m10 = pm.m10 / scale.y;
			normalized.m11 = pm.m11 / scale.y;
			normalized.m12 = pm.m12 / scale.y;
			normalized.m20 = pm.m20 / scale.z;
			normalized.m21 = pm.m21 / scale.z;
			normalized.m22 = pm.m22 / scale.z;

			rotation = Quaternion::FromRotationMatrix(normalized);
		}
	}
}
