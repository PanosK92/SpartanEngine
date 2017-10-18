/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ==============
#include "../Core/Helper.h"
#include <string>
//=========================

namespace Directus
{
	namespace Math
	{
		class DLL_API Vector2
		{
		public:
			Vector2::Vector2()
			{
				x = 0;
				y = 0;
			}

			Vector2::Vector2(const Vector2& vector)
			{
				this->x = vector.x;
				this->y = vector.y;
			}

			Vector2::Vector2(float x, float y)
			{
				this->x = x;
				this->y = y;
			}

			Vector2::~Vector2() {}

			Vector2 Vector2::operator+(const Vector2& b)
			{
				return Vector2
				(
					this->x + b.x,
					this->y + b.y
				);
			}

			void Vector2::operator+=(const Vector2& b)
			{
				this->x += b.x;
				this->y += b.y;
			}

			bool operator==(const Vector2& b)
			{
				if (x == b.x && y == b.y)
					return true;

				return false;
			}

			bool operator!=(const Vector2& b)
			{
				if (x != b.x || y != b.y)
					return true;

				return false;
			}

			std::string ToString() const;

			float x;
			float y;
			static const Vector2 Zero;
		};
	}
}
