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

//= INCLUDES =======
#include "Frustum.h"
#include "Plane.h"
//==================

//= NAMESPACES ========================
using namespace Directus::Math::Helper;
//=====================================

namespace Directus::Math
{
	void Frustum::Construct(const Matrix& mView, const Matrix& mProjection, float screenDepth)
	{
		// Calculate the minimum Z distance in the frustum.
		float zMinimum = -mProjection.m32 / mProjection.m22;
		float r = screenDepth / (screenDepth - zMinimum);
		Matrix mProjectionUpdated = mProjection;
		mProjectionUpdated.m22 = r;
		mProjectionUpdated.m32 = -r * zMinimum;

		// Create the frustum matrix from the view matrix and updated projection matrix.
		Matrix viewProjection = mView * mProjectionUpdated;

		// Calculate near plane of frustum.
		m_planes[0].normal.x = viewProjection.m03 + viewProjection.m02;
		m_planes[0].normal.y = viewProjection.m13 + viewProjection.m12;
		m_planes[0].normal.z = viewProjection.m23 + viewProjection.m22;
		m_planes[0].d = viewProjection.m33 + viewProjection.m32;
		m_planes[0].Normalize();

		// Calculate far plane of frustum.
		m_planes[1].normal.x = viewProjection.m03 - viewProjection.m02;
		m_planes[1].normal.y = viewProjection.m13 - viewProjection.m12;
		m_planes[1].normal.z = viewProjection.m23 - viewProjection.m22;
		m_planes[1].d = viewProjection.m33 - viewProjection.m32;
		m_planes[1].Normalize();

		// Calculate left plane of frustum.
		m_planes[2].normal.x = viewProjection.m03 + viewProjection.m00;
		m_planes[2].normal.y = viewProjection.m13 + viewProjection.m10;
		m_planes[2].normal.z = viewProjection.m23 + viewProjection.m20;
		m_planes[2].d = viewProjection.m33 + viewProjection.m30;
		m_planes[2].Normalize();

		// Calculate right plane of frustum.
		m_planes[3].normal.x = viewProjection.m03 - viewProjection.m00;
		m_planes[3].normal.y = viewProjection.m13 - viewProjection.m10;
		m_planes[3].normal.z = viewProjection.m23 - viewProjection.m20;
		m_planes[3].d = viewProjection.m33 - viewProjection.m30;
		m_planes[3].Normalize();

		// Calculate top plane of frustum.
		m_planes[4].normal.x = viewProjection.m03 - viewProjection.m01;
		m_planes[4].normal.y = viewProjection.m13 - viewProjection.m11;
		m_planes[4].normal.z = viewProjection.m23 - viewProjection.m21;
		m_planes[4].d = viewProjection.m33 - viewProjection.m31;
		m_planes[4].Normalize();

		// Calculate bottom plane of frustum.
		m_planes[5].normal.x = viewProjection.m03 + viewProjection.m01;
		m_planes[5].normal.y = viewProjection.m13 + viewProjection.m11;
		m_planes[5].normal.z = viewProjection.m23 + viewProjection.m21;
		m_planes[5].d = viewProjection.m33 + viewProjection.m31;
		m_planes[5].Normalize();
	}

	Intersection Frustum::CheckCube(const Vector3& center, const Vector3& extent)
	{
		// Check if any one point of the cube is in the view frustum.
		Intersection result = Inside;
		for (const auto& plane : m_planes)
		{
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
			{
				result = Intersects;
			}
		}

		return result;
	}

	Intersection Frustum::CheckSphere(const Vector3& center, float radius)
	{
		// calculate our distances to each of the planes
		for (const auto& plane : m_planes)
		{
			// find the distance to this plane
			float fDistance = Vector3::Dot(plane.normal, center) + plane.d;

			// if this distance is < -sphere.radius, we are outside
			if (fDistance < -radius)
			{
				return Outside;
			}

			// else if the distance is between +- radius, then we intersect
			if ((float)fabs(fDistance) < radius)
			{
				return Intersects;
			}
		}

		// otherwise we are fully in view
		return Inside;
	}
}