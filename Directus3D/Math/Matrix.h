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

namespace Directus
{
	namespace Math
	{
		class Quaternion;
		class Vector3;

		class __declspec(dllexport) Matrix
		{
		public:
			Matrix();
			Matrix(float m11, float m12, float m13, float m14, float m21, float m22, float m23, float m24, float m31, float m32, float m33, float m34, float m41, float m42, float m43, float m44);
			~Matrix();

			static Matrix Identity();
			Matrix Transpose();
			static Matrix Transpose(Matrix matrix);
			Matrix Inverse();
			static Matrix Invert(Matrix matrix);
			static Matrix CreateScale(float scaleX, float scaleY, float ScaleZ);
			static Matrix CreateScale(float scale);
			static Matrix CreateScale(Vector3 scale);
			static Matrix CreateFromQuaternion(Quaternion q);
			static Matrix CreateFromYawPitchRoll(float yaw, float pitch, float roll);
			static Matrix CreateTranslation(Vector3 position);
			static Matrix CreateLookAtLH(Vector3 cameraPosition, Vector3 cameraTarget, Vector3 cameraUpVector);
			static Matrix CreateOrthographicLH(float width, float height, float zNearPlane, float zFarPlane);
			static Matrix CreatePerspectiveFieldOfViewLH(float fieldOfView, float aspectRatio, float nearPlaneDistance, float farPlaneDistance);
			void Decompose(Vector3& scale, Quaternion& rotation, Vector3& translation);

			Matrix operator*(const Matrix& b);
			bool operator==(const Matrix& b);
			bool operator!=(const Matrix& b);

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

		private:
			static Matrix Multiply(Matrix matrix1, Matrix matrix2);
		};
	}
}
