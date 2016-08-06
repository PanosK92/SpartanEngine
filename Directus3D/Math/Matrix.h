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

//= INCLUDES ==========
#include "MathHelper.h"
#include "Vector3.h"
#include "Quaternion.h"
//=====================

namespace Directus
{
	namespace Math
	{
		class Quaternion;

		class __declspec(dllexport) Matrix
		{
		public:
			Matrix()
			{
				m00 = 1; m01 = 0; m02 = 0; m03 = 0;
				m10 = 0; m11 = 1; m12 = 0; m13 = 0;
				m20 = 0; m21 = 0; m22 = 1; m23 = 0;
				m30 = 0; m31 = 0; m32 = 0; m33 = 1;
			}

			Matrix(float m11, float m12, float m13, float m14, float m21, float m22, float m23, float m24, float m31, float m32, float m33, float m34, float m41, float m42, float m43, float m44)
			{
				this->m00 = m11; this->m01 = m12; this->m02 = m13; this->m03 = m14;
				this->m10 = m21; this->m11 = m22; this->m12 = m23; this->m13 = m24;
				this->m20 = m31; this->m21 = m32; this->m22 = m33; this->m23 = m34;
				this->m30 = m41; this->m31 = m42; this->m32 = m43; this->m33 = m44;
			}

			~Matrix() {}

			Matrix Transposed() const { return Transposed(*this); }

			static Matrix Transposed(const Matrix& matrix)
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


			Matrix Inverse() const { return Invert(*this); }

			static Matrix Invert(const Matrix& matrix)
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

			static Matrix CreateScale(float scale) { return CreateScale(scale, scale, scale); }
			static Matrix CreateScale(const Vector3& scale) { return CreateScale(scale.x, scale.y, scale.z); }
			static Matrix CreateScale(float scaleX, float scaleY, float ScaleZ)
			{
				return Matrix(
					scaleX, 0, 0, 0,
					0, scaleY, 0, 0,
					0, 0, ScaleZ, 0,
					0, 0, 0, 1
				);
			}

			static Matrix CreateTranslation(const Vector3& position);

			static Matrix CreateLookAtLH(const Vector3& cameraPosition, const Vector3& cameraTarget, const Vector3& cameraUpVector);

			static Matrix CreateOrthographicLH(float width, float height, float zNearPlane, float zFarPlane)
			{
				return Matrix(
					2 / width, 0, 0, 0,
					0, 2 / height, 0, 0,
					0, 0, 1 / (zFarPlane - zNearPlane), 0,
					0, 0, zNearPlane / (zNearPlane - zFarPlane), 1
				);
			}

			static Matrix CreatePerspectiveFieldOfViewLH(float fieldOfView, float aspectRatio, float nearPlaneDistance, float farPlaneDistance)
			{
				float yScale = Cot(fieldOfView / 2);
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

			void Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation);

			//= OPERATORS ======================================
			Matrix operator*(const Matrix& b)
			{
				return Multiply(*this, b);
			}

			Matrix operator*(const Matrix& b) const
			{
				return Multiply(*this, b);
			}

			bool operator==(const Matrix& b)
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

			bool operator!=(const Matrix& b)
			{
				return !(*this == b);
			}
			//==================================================

			float m00;
			float m01;
			float m02;
			float m03;
			float m10;
			float m11;
			float m12;
			float m13;
			float m20;
			float m21;
			float m22;
			float m23;
			float m30;
			float m31;
			float m32;
			float m33;

			static const Matrix Identity;

		private:
			static Matrix Multiply(const Matrix& matrix1, const Matrix&  matrix2)
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

				return Matrix(
					m11,
					m12,
					m13,
					m14,
					m21,
					m22,
					m23,
					m24,
					m31,
					m32,
					m33,
					m34,
					m41,
					m42,
					m43,
					m44
				);
			}
		};
	}
}
