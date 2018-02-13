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

//= INCLUDES ==================
#include "../Core/EngineDefs.h"
#include <string>
//=============================

namespace Directus
{
	namespace Math
	{
		class Vector3;
		class Matrix;

		class ENGINE_CLASS Vector4
		{
		public:
			Vector4::Vector4()
			{
				x = 0;
				y = 0;
				z = 0;
				w = 0;
			}

			Vector4::Vector4(float x, float y, float z, float w)
			{
				this->x = x;
				this->y = y;
				this->z = z;
				this->w = w;
			}

			Vector4::Vector4(float value)
			{
				this->x = value;
				this->y = value;
				this->z = value;
				this->w = value;
			}

			Vector4::Vector4(const Vector3& value, float w);
			Vector4::Vector4(const Vector3& value);

			Vector4::~Vector4(){}

			bool Vector4::operator==(const Vector4& b)
			{
				if (this->x == b.x && this->y == b.y && this->z == b.z && this->w == b.w)
					return true;

				return false;
			}

			bool Vector4::operator!=(const Vector4& b)
			{
				if (this->x != b.x || this->y != b.y || this->z != b.z || this->w != b.w)
					return true;

				return false;
			}

			static Vector4 Transform(const Vector3& lhs, const Matrix& rhs);

			std::string ToString() const;

			float x, y, z, w;

			const float* Data() const { return &x; }

			static const Vector4 One;
			static const Vector4 Zero;
		};
	}
}
