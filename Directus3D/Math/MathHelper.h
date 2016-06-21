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

#define PI				((float)3.141592654f)
#define PI_2			((float)6.283185307f)
#define PI_DIV_2		((float)1.570796327f)
#define PI_DIV_4		((float)0.785398163f) 
#define PI_INV			((float)0.318309886f) 
#define DEG_TO_RAD		(float)PI / 180.0f
#define DEG_TO_RAD_2	(float)PI / 360.0f;
#define RAD_TO_DEG		(float)180.0 / PI


//= INCLUDE =====
#include <math.h>
//===============

namespace Directus
{
	namespace Math
	{
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
