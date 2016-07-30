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

//= INCLUDES ========
#include "Frustrum.h"
//===================

//= NAMESPACES ================
using namespace Directus::Math;

//=============================

Frustrum::Frustrum()
{
	m_projectionMatrix = Matrix::Identity;
	m_viewMatrix = Matrix::Identity;
}

Frustrum::~Frustrum()
{
}

void Frustrum::ConstructFrustum(float screenDepth)
{
	float zMinimum, r;
	Matrix matrix;

	// Calculate the minimum Z distance in the frustum.
	zMinimum = -m_projectionMatrix.m32 / m_projectionMatrix.m22;
	r = screenDepth / (screenDepth - zMinimum);
	m_projectionMatrix.m22 = r;
	m_projectionMatrix.m32 = -r * zMinimum;

	// Create the frustum matrix from the view matrix and updated projection matrix.
	matrix = m_viewMatrix * m_projectionMatrix;

	// Calculate near plane of frustum.
	m_planes[0].normal.x = matrix.m03 + matrix.m02;
	m_planes[0].normal.y = matrix.m13 + matrix.m12;
	m_planes[0].normal.z = matrix.m23 + matrix.m22;
	m_planes[0].d = matrix.m33 + matrix.m32;
	m_planes[0].Normalize();

	// Calculate far plane of frustum.
	m_planes[1].normal.x = matrix.m03 - matrix.m02;
	m_planes[1].normal.y = matrix.m13 - matrix.m12;
	m_planes[1].normal.z = matrix.m23 - matrix.m22;
	m_planes[1].d = matrix.m33 - matrix.m32;
	m_planes[1].Normalize();

	// Calculate left plane of frustum.
	m_planes[2].normal.x = matrix.m03 + matrix.m00;
	m_planes[2].normal.y = matrix.m13 + matrix.m10;
	m_planes[2].normal.z = matrix.m23 + matrix.m20;
	m_planes[2].d = matrix.m33 + matrix.m30;
	m_planes[2].Normalize();

	// Calculate right plane of frustum.
	m_planes[3].normal.x = matrix.m03 - matrix.m00;
	m_planes[3].normal.y = matrix.m13 - matrix.m10;
	m_planes[3].normal.z = matrix.m23 - matrix.m20;
	m_planes[3].d = matrix.m33 - matrix.m30;
	m_planes[3].Normalize();

	// Calculate top plane of frustum.
	m_planes[4].normal.x = matrix.m03 - matrix.m01;
	m_planes[4].normal.y = matrix.m13 - matrix.m11;
	m_planes[4].normal.z = matrix.m23 - matrix.m21;
	m_planes[4].d = matrix.m33 - matrix.m31;
	m_planes[4].Normalize();

	// Calculate bottom plane of frustum.
	m_planes[5].normal.x = matrix.m03 + matrix.m01;
	m_planes[5].normal.y = matrix.m13 + matrix.m11;
	m_planes[5].normal.z = matrix.m23 + matrix.m21;
	m_planes[5].d = matrix.m33 + matrix.m31;
	m_planes[5].Normalize();
}

FrustrumSpace Frustrum::CheckCube(Vector3 center, Vector3 extent)
{
	// Check if any one point of the cube is in the view frustum.
	FrustrumSpace result = Inside;
	for (int i = 0; i < 6; i++)
	{
		Plane plane = m_planes[i];
		Plane absolutePlane = Plane(plane.normal.Absolute(), plane.d);

		float d = center.x * plane.normal.x + center.y * plane.normal.y + center.z * plane.normal.z;
		float r = extent.x * absolutePlane.normal.x + extent.y * absolutePlane.normal.y + extent.z * absolutePlane.normal.z;

		float d_p_r = d + r;
		float d_m_r = d - r;

		if (d_p_r < -plane.d)
		{
			result = Outside;
			break;
		}

		if (d_m_r < -plane.d)
			result = Intersects;
	}

	return result;
}

FrustrumSpace Frustrum::CheckSphere(Vector3 center, float radius)
{
	// various distances
	float fDistance;

	// calculate our distances to each of the planes
	for (int i = 0; i < 6; ++i)
	{
		// find the distance to this plane
		fDistance = Vector3::Dot(m_planes[i].normal, center) + m_planes[i].d;

		// if this distance is < -sphere.radius, we are outside
		if (fDistance < -radius)
			return Outside;

		// else if the distance is between +- radius, then we intersect
		if ((float)fabs(fDistance) < radius)
			return Intersects;
	}

	// otherwise we are fully in view
	return Inside;
}

void Frustrum::SetViewMatrix(Matrix viewMatrix)
{
	m_viewMatrix = viewMatrix;
}

Matrix Frustrum::GetViewMatrix()
{
	return m_viewMatrix;
}

void Frustrum::SetProjectionMatrix(Matrix projectionMatrix)
{
	m_projectionMatrix = projectionMatrix;
}

Matrix Frustrum::GetProjectionMatrix()
{
	return m_projectionMatrix;
}
