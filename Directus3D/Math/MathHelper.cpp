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

//= INCLUDES ==========
#include "MathHelper.h"
#include <math.h>

//=====================

namespace Directus
{
	namespace Math
	{
		double MathHelper::Cot(float x)
		{
			return cos(x) / sin(x);
		}

		float MathHelper::CotF(float x)
		{
			return cosf(x) / sinf(x);
		}

		float MathHelper::DegreesToRadians(float degrees)
		{
			return degrees * DEG_TO_RAD;
		}

		float MathHelper::RadiansToDegrees(float radians)
		{
			return radians * RAD_TO_DEG;
		}

		float MathHelper::Clamp(float x, float a, float b)
		{
			return x < a ? a : (x > b ? b : x);
		}

		float MathHelper::Lerp(float a, float b, float amount)
		{
			return a + (b - a) * amount;
		}
	}
}
