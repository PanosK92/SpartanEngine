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

//= INCLUDES ==================
#include "../Core/EngineDefs.h"
#include <string>
//=============================

namespace Directus::Math
{
	class ENGINE_CLASS Vector2
	{
	public:
		Vector2()
		{
			x = 0;
			y = 0;
		}

		Vector2(const Vector2& vector)
		{
			this->x = vector.x;
			this->y = vector.y;
		}

		Vector2(float x, float y)
		{
			this->x = x;
			this->y = y;
		}

		Vector2(int x, int y)
		{
			this->x = (float)x;
			this->y = (float)y;
		}

		Vector2(unsigned int x, unsigned int y)
		{
			this->x = (float)x;
			this->y = (float)y;
		}
		
		Vector2(float x)
		{
			this->x = x;
			this->y = x;
		}

		~Vector2() {}

		//= ADDITION ===============================
		Vector2 operator+(const Vector2& b)
		{
			return Vector2
			(
				this->x + b.x,
				this->y + b.y
			);
		}

		void operator+=(const Vector2& b)
		{
			this->x += b.x;
			this->y += b.y;
		}
		//==========================================

		//= MULTIPLICATION =======================================================================================
		Vector2 operator*(const Vector2& b) const
		{
			return Vector2(x * b.x, y * b.y);
		}

		void operator*=(const Vector2& b)
		{
			x *= b.x;
			y *= b.y;
		}

		Vector2 operator*(const float value) const
		{
			return Vector2(x * value, y * value);
		}

		void operator*=(const float value)
		{
			x *= value;
			y *= value;
		}
		//=======================================================================================================


		//= SUBTRACTION ===============================================================
		Vector2 operator-(const Vector2& b) const { return Vector2(x - b.x, y - b.y); }
		Vector2 operator-(const float value) { return Vector2(x - value, y - value); }

		void operator-=(const Vector2& rhs)
		{
			x -= rhs.x;
			y -= rhs.y;
		}
		//=============================================================================

		//= DIVISION ========================================================================
		Vector2 operator/(const Vector2& rhs) const { return Vector2(x / rhs.x, y / rhs.y); }
		Vector2 operator/(const float rhs) { return Vector2(x / rhs, y / rhs); }

		void operator/=(const Vector2& rhs)
		{
			x /= rhs.x;
			y /= rhs.y;
		}
		//===================================================================================

		bool operator==(const Vector2& b)
		{
			return x == b.x && y == b.y;
		}

		bool operator!=(const Vector2& b)
		{
			return x != b.x || y != b.y;
		}

		std::string ToString() const;

		float x;
		float y;
		static const Vector2 Zero;
		static const Vector2 One;
	};
}