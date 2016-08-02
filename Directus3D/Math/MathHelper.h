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

//= INCLUDES ====
// It's not used here but it will provide functionality
// to any code that includes this header.
#include <math.h> 
//===============

namespace Directus
{
	namespace Math
	{
		static const float M_EPSILON	= 0.000001f;
		static const float PI			= 3.14159265358979323846264338327950288f;
		static const float PI_2			= 6.283185307f;
		static const float PI_DIV_2		= 1.570796327f;
		static const float PI_DIV_4		= 0.785398163f;
		static const float PI_INV		= 0.318309886f;
		static const float DEG_TO_RAD	= PI / 180.0f;
		static const float DEG_TO_RAD_2 = PI / 360.0f;
		static const float RAD_TO_DEG	= 180.0f / PI;

		class __declspec(dllexport) MathHelper
		{
		public:
			MathHelper()
			{
			}

			~MathHelper()
			{
			}

			static MathHelper& GetInstance()
			{
				static MathHelper instance;
				return instance;
			}

			double Cot(float x);
			float CotF(float x);
			float DegreesToRadians(float degrees);
			float RadiansToDegrees(float radians);
			float Clamp(float x, float a, float b);
			float Lerp(float a, float b, float f);
		};
	}
}
