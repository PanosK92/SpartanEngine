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
#include "MathHelper.h"

//======================

namespace Directus
{
	namespace Math
	{
		Matrix::Matrix()
		{
			this->m00 = 1;
			this->m01 = 0;
			this->m02 = 0;
			this->m03 = 0;
			this->m10 = 0;
			this->m11 = 1;
			this->m12 = 0;
			this->m13 = 0;
			this->m20 = 0;
			this->m21 = 0;
			this->m22 = 1;
			this->m23 = 0;
			this->m30 = 0;
			this->m31 = 0;
			this->m32 = 0;
			this->m33 = 1;
		}

		Matrix::Matrix(float m11, float m12, float m13, float m14, float m21, float m22, float m23, float m24, float m31, float m32, float m33, float m34, float m41, float m42, float m43, float m44)
		{
			this->m00 = m11;
			this->m01 = m12;
			this->m02 = m13;
			this->m03 = m14;
			this->m10 = m21;
			this->m11 = m22;
			this->m12 = m23;
			this->m13 = m24;
			this->m20 = m31;
			this->m21 = m32;
			this->m22 = m33;
			this->m23 = m34;
			this->m30 = m41;
			this->m31 = m42;
			this->m32 = m43;
			this->m33 = m44;
		}

		Matrix::~Matrix()
		{
		}

		Matrix Matrix::Identity()
		{
			return Matrix(
				1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				0, 0, 0, 1
			);
		}

		Matrix Matrix::Transpose()
		{
			return Transpose(*this);
		}

		Matrix Matrix::Transpose(Matrix matrix)
		{
			Matrix result;

			result.m00 = matrix.m00;
			result.m01 = matrix.m10;
			result.m02 = matrix.m20;
			result.m03 = matrix.m30;

			result.m10 = matrix.m01;
			result.m11 = matrix.m11;
			result.m12 = matrix.m21;
			result.m13 = matrix.m31;

			result.m20 = matrix.m02;
			result.m21 = matrix.m12;
			result.m22 = matrix.m22;
			result.m23 = matrix.m32;

			result.m30 = matrix.m03;
			result.m31 = matrix.m13;
			result.m32 = matrix.m23;
			result.m33 = matrix.m33;

			return result;
		}

		Matrix Matrix::Inverse()
		{
			return Invert(*this);
		}

		Matrix Matrix::Invert(Matrix matrix)
		{
			float num1 = matrix.m00;
			float num2 = matrix.m01;
			float num3 = matrix.m02;
			float num4 = matrix.m03;
			float num5 = matrix.m10;
			float num6 = matrix.m11;
			float num7 = matrix.m12;
			float num8 = matrix.m13;
			float num9 = matrix.m20;
			float num10 = matrix.m21;
			float num11 = matrix.m22;
			float num12 = matrix.m23;
			float num13 = matrix.m30;
			float num14 = matrix.m31;
			float num15 = matrix.m32;
			float num16 = matrix.m33;
			float num17 = (float)((double)num11 * (double)num16 - (double)num12 * (double)num15);
			float num18 = (float)((double)num10 * (double)num16 - (double)num12 * (double)num14);
			float num19 = (float)((double)num10 * (double)num15 - (double)num11 * (double)num14);
			float num20 = (float)((double)num9 * (double)num16 - (double)num12 * (double)num13);
			float num21 = (float)((double)num9 * (double)num15 - (double)num11 * (double)num13);
			float num22 = (float)((double)num9 * (double)num14 - (double)num10 * (double)num13);
			float num23 = (float)((double)num6 * (double)num17 - (double)num7 * (double)num18 + (double)num8 * (double)num19);
			float num24 = (float)-((double)num5 * (double)num17 - (double)num7 * (double)num20 + (double)num8 * (double)num21);
			float num25 = (float)((double)num5 * (double)num18 - (double)num6 * (double)num20 + (double)num8 * (double)num22);
			float num26 = (float)-((double)num5 * (double)num19 - (double)num6 * (double)num21 + (double)num7 * (double)num22);
			float num27 = (float)(1.0 / ((double)num1 * (double)num23 + (double)num2 * (double)num24 + (double)num3 * (double)num25 + (double)num4 * (double)num26));

			Matrix result;
			result.m00 = num23 * num27;
			result.m10 = num24 * num27;
			result.m20 = num25 * num27;
			result.m30 = num26 * num27;
			result.m01 = (float)-((double)num2 * (double)num17 - (double)num3 * (double)num18 + (double)num4 * (double)num19) * num27;
			result.m11 = (float)((double)num1 * (double)num17 - (double)num3 * (double)num20 + (double)num4 * (double)num21) * num27;
			result.m21 = (float)-((double)num1 * (double)num18 - (double)num2 * (double)num20 + (double)num4 * (double)num22) * num27;
			result.m31 = (float)((double)num1 * (double)num19 - (double)num2 * (double)num21 + (double)num3 * (double)num22) * num27;
			float num28 = (float)((double)num7 * (double)num16 - (double)num8 * (double)num15);
			float num29 = (float)((double)num6 * (double)num16 - (double)num8 * (double)num14);
			float num30 = (float)((double)num6 * (double)num15 - (double)num7 * (double)num14);
			float num31 = (float)((double)num5 * (double)num16 - (double)num8 * (double)num13);
			float num32 = (float)((double)num5 * (double)num15 - (double)num7 * (double)num13);
			float num33 = (float)((double)num5 * (double)num14 - (double)num6 * (double)num13);
			result.m02 = (float)((double)num2 * (double)num28 - (double)num3 * (double)num29 + (double)num4 * (double)num30) * num27;
			result.m12 = (float)-((double)num1 * (double)num28 - (double)num3 * (double)num31 + (double)num4 * (double)num32) * num27;
			result.m22 = (float)((double)num1 * (double)num29 - (double)num2 * (double)num31 + (double)num4 * (double)num33) * num27;
			result.m32 = (float)-((double)num1 * (double)num30 - (double)num2 * (double)num32 + (double)num3 * (double)num33) * num27;
			float num34 = (float)((double)num7 * (double)num12 - (double)num8 * (double)num11);
			float num35 = (float)((double)num6 * (double)num12 - (double)num8 * (double)num10);
			float num36 = (float)((double)num6 * (double)num11 - (double)num7 * (double)num10);
			float num37 = (float)((double)num5 * (double)num12 - (double)num8 * (double)num9);
			float num38 = (float)((double)num5 * (double)num11 - (double)num7 * (double)num9);
			float num39 = (float)((double)num5 * (double)num10 - (double)num6 * (double)num9);
			result.m03 = (float)-((double)num2 * (double)num34 - (double)num3 * (double)num35 + (double)num4 * (double)num36) * num27;
			result.m13 = (float)((double)num1 * (double)num34 - (double)num3 * (double)num37 + (double)num4 * (double)num38) * num27;
			result.m23 = (float)-((double)num1 * (double)num35 - (double)num2 * (double)num37 + (double)num4 * (double)num39) * num27;
			result.m33 = (float)((double)num1 * (double)num36 - (double)num2 * (double)num38 + (double)num3 * (double)num39) * num27;

			return result;
		}

		Matrix Matrix::CreateScale(float scaleX, float scaleY, float ScaleZ)
		{
			return Matrix(
				scaleX, 0, 0, 0,
				0, scaleY, 0, 0,
				0, 0, ScaleZ, 0,
				0, 0, 0, 1
			);
		}

		Matrix Matrix::CreateScale(float scale)
		{
			return CreateScale(scale, scale, scale);
		}

		Matrix Matrix::CreateScale(Vector3 scale)
		{
			return CreateScale(scale.x, scale.y, scale.z);
		}

		Matrix Matrix::CreateFromQuaternion(Quaternion q)
		{
			return CreateFromYawPitchRoll(q.GetYaw(), q.GetPitch(), q.GetRoll());
		}

		Matrix Matrix::CreateFromYawPitchRoll(float yaw, float pitch, float roll)
		{
			float sroll, croll, spitch, cpitch, syaw, cyaw;

			sroll = sinf(roll);
			croll = cosf(roll);
			spitch = sinf(pitch);
			cpitch = cosf(pitch);
			syaw = sinf(yaw);
			cyaw = cosf(yaw);

			return Matrix(
				sroll * spitch * syaw + croll * cyaw, sroll * cpitch, sroll * spitch * cyaw - croll * syaw, 0.0f,
				croll * spitch * syaw - sroll * cyaw, croll * cpitch, croll * spitch * cyaw + sroll * syaw, 0.0f,
				cpitch * syaw, -spitch, cpitch * cyaw, 0.0f,
				0.0f, 0.0f, 0.0f, 1.0f
			);
		}

		Matrix Matrix::CreateTranslation(Vector3 position)
		{
			return Matrix(
				1, 0, 0, 0,
				0, 1, 0, 0,
				0, 0, 1, 0,
				position.x, position.y, position.z, 1
			);
		}

		Matrix Matrix::CreateLookAtLH(Vector3 eye, Vector3 at, Vector3 up)
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

		Matrix Matrix::CreateOrthographicLH(float width, float height, float zNearPlane, float zFarPlane)
		{
			return Matrix(
				2 / width, 0, 0, 0,
				0, 2 / height, 0, 0,
				0, 0, 1 / (zFarPlane - zNearPlane), 0,
				0, 0, zNearPlane / (zNearPlane - zFarPlane), 1
			);
		}

		Matrix Matrix::CreatePerspectiveFieldOfViewLH(float fieldOfView, float aspectRatio, float nearPlaneDistance, float farPlaneDistance)
		{
			float yScale = MathHelper::GetInstance().Cot(fieldOfView / 2);
			float xScale = yScale / aspectRatio;
			float zn = nearPlaneDistance;
			float zf = farPlaneDistance;

			return Matrix(
				xScale, 0, 0, 0,
				0, yScale, 0, 0,
				0, 0, zf / (zf - zn), 1,
				0, 0, -zn * zf / (zf - zn), 0
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
				rotation = Quaternion::Identity();
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

			rotation = Quaternion::CreateFromRotationMatrix(normalized);
		}

		Matrix Matrix::operator*(const Matrix& b)
		{
			return Multiply(*this, b);
		}

		bool Matrix::operator==(const Matrix& b)
		{
			if (this->m00 != b.m00)
				return false;

			if (this->m01 != b.m01)
				return false;

			if (this->m01 != b.m01)
				return false;

			if (this->m01 != b.m01)
				return false;

			if (this->m10 != b.m00)
				return false;

			if (this->m11 != b.m11)
				return false;

			if (this->m11 != b.m11)
				return false;

			if (this->m11 != b.m11)
				return false;

			if (this->m20 != b.m20)
				return false;

			if (this->m21 != b.m21)
				return false;

			if (this->m21 != b.m21)
				return false;

			if (this->m21 != b.m21)
				return false;

			if (this->m30 != b.m30)
				return false;

			if (this->m31 != b.m31)
				return false;

			if (this->m31 != b.m31)
				return false;

			if (this->m31 != b.m31)
				return false;

			return true;
		}

		bool Matrix::operator!=(const Matrix& b)
		{
			return !(*this == b);
		}

		Matrix Matrix::Multiply(Matrix matrix1, Matrix matrix2)
		{
			float m11 = (((matrix1.m00 * matrix2.m00) + (matrix1.m01 * matrix2.m10)) + (matrix1.m02 * matrix2.m20)) + (matrix1.m03 * matrix2.m30);
			float m12 = (((matrix1.m00 * matrix2.m01) + (matrix1.m01 * matrix2.m11)) + (matrix1.m02 * matrix2.m21)) + (matrix1.m03 * matrix2.m31);
			float m13 = (((matrix1.m00 * matrix2.m02) + (matrix1.m01 * matrix2.m12)) + (matrix1.m02 * matrix2.m22)) + (matrix1.m03 * matrix2.m32);
			float m14 = (((matrix1.m00 * matrix2.m03) + (matrix1.m01 * matrix2.m13)) + (matrix1.m02 * matrix2.m23)) + (matrix1.m03 * matrix2.m33);

			float m21 = (((matrix1.m10 * matrix2.m00) + (matrix1.m11 * matrix2.m10)) + (matrix1.m12 * matrix2.m20)) + (matrix1.m13 * matrix2.m30);
			float m22 = (((matrix1.m10 * matrix2.m01) + (matrix1.m11 * matrix2.m11)) + (matrix1.m12 * matrix2.m21)) + (matrix1.m13 * matrix2.m31);
			float m23 = (((matrix1.m10 * matrix2.m02) + (matrix1.m11 * matrix2.m12)) + (matrix1.m12 * matrix2.m22)) + (matrix1.m13 * matrix2.m32);
			float m24 = (((matrix1.m10 * matrix2.m03) + (matrix1.m11 * matrix2.m13)) + (matrix1.m12 * matrix2.m23)) + (matrix1.m13 * matrix2.m33);

			float m31 = (((matrix1.m20 * matrix2.m00) + (matrix1.m21 * matrix2.m10)) + (matrix1.m22 * matrix2.m20)) + (matrix1.m23 * matrix2.m30);
			float m32 = (((matrix1.m20 * matrix2.m01) + (matrix1.m21 * matrix2.m11)) + (matrix1.m22 * matrix2.m21)) + (matrix1.m23 * matrix2.m31);
			float m33 = (((matrix1.m20 * matrix2.m02) + (matrix1.m21 * matrix2.m12)) + (matrix1.m22 * matrix2.m22)) + (matrix1.m23 * matrix2.m32);
			float m34 = (((matrix1.m20 * matrix2.m03) + (matrix1.m21 * matrix2.m13)) + (matrix1.m22 * matrix2.m23)) + (matrix1.m23 * matrix2.m33);

			float m41 = (((matrix1.m30 * matrix2.m00) + (matrix1.m31 * matrix2.m10)) + (matrix1.m32 * matrix2.m20)) + (matrix1.m33 * matrix2.m30);
			float m42 = (((matrix1.m30 * matrix2.m01) + (matrix1.m31 * matrix2.m11)) + (matrix1.m32 * matrix2.m21)) + (matrix1.m33 * matrix2.m31);
			float m43 = (((matrix1.m30 * matrix2.m02) + (matrix1.m31 * matrix2.m12)) + (matrix1.m32 * matrix2.m22)) + (matrix1.m33 * matrix2.m32);
			float m44 = (((matrix1.m30 * matrix2.m03) + (matrix1.m31 * matrix2.m13)) + (matrix1.m32 * matrix2.m23)) + (matrix1.m33 * matrix2.m33);

			matrix1.m00 = m11;
			matrix1.m01 = m12;
			matrix1.m02 = m13;
			matrix1.m03 = m14;

			matrix1.m10 = m21;
			matrix1.m11 = m22;
			matrix1.m12 = m23;
			matrix1.m13 = m24;

			matrix1.m20 = m31;
			matrix1.m21 = m32;
			matrix1.m22 = m33;
			matrix1.m23 = m34;

			matrix1.m30 = m41;
			matrix1.m31 = m42;
			matrix1.m32 = m43;
			matrix1.m33 = m44;

			return matrix1;
		}
	}
}
