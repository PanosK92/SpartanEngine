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

//= INCLUDES ===============
#include "../Math/Matrix.h"
#include "../Math/Vector2.h"
//==========================

namespace Directus
{
	struct Struct_Matrix
	{
		Math::Matrix matrix;
	};

	struct Struct_Matrix_Vector4
	{
		Math::Matrix matrix;
		Math::Vector4 vector4;
	};

	struct Struct_Matrix_Vector3
	{
		Math::Matrix matrix;
		Math::Vector3 vector3;
		float padding;
	};

	struct Struct_Matrix_Vector2
	{
		Math::Matrix matrix;
		Math::Vector2 vector2;
		Math::Vector2 padding;
	};

	struct Struct_Shadowing
	{
		Math::Matrix wvpOrtho;
		Math::Matrix wvpInv;
		Math::Matrix view;
		Math::Matrix projection;
		Math::Matrix projectionInverse;
		Math::Matrix mLightViewProjection[3];
		Math::Vector4 shadowSplits;
		Math::Vector3 lightDir;
		float shadowMapResolution;
		Math::Vector2 resolution;
		float nearPlane;
		float farPlane;
		float doShadowMapping;
		Math::Vector3 padding;
	};

	struct Struct_Matrix_Matrix_Matrix
	{
		Math::Matrix m1;
		Math::Matrix m2;
		Math::Matrix m3;
	};

	struct Struct_Matrix_Vector3_Vector3
	{
		Math::Matrix matrix;
		Math::Vector3 vector3A;
		float padding;
		Math::Vector3 vector3B;
		float padding2;
	};
}