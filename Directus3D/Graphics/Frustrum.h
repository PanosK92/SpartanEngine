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

//= INCLUDES ===============
#include "../Math/Vector3.h"
#include "../Math/Plane.h"
#include "../Math/Matrix.h"
//==========================

enum FrustrumSpace
{
	Outside,
	Inside,
	Intersects
};

class Frustrum
{
public:
	Frustrum();
	~Frustrum();

	void ConstructFrustum(float screenDepth);
	FrustrumSpace CheckCube(Directus::Math::Vector3 center, Directus::Math::Vector3 extent);
	FrustrumSpace CheckSphere(Directus::Math::Vector3 center, float radius);

	void SetViewMatrix(Directus::Math::Matrix viewMatrix);
	Directus::Math::Matrix GetViewMatrix();

	void SetProjectionMatrix(Directus::Math::Matrix projectionMatrix);
	Directus::Math::Matrix GetProjectionMatrix();


private:
	Directus::Math::Plane m_planes[6];

	Directus::Math::Matrix m_viewMatrix;
	Directus::Math::Matrix m_projectionMatrix;
};
