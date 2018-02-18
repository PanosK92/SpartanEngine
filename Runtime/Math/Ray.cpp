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

//= INCLUDES ===========
#include "Ray.h"
#include "BoundingBox.h"
//======================

namespace Directus::Math
{
	Ray::Ray()
	{
		m_origin = Vector3::Zero;
		m_end = Vector3::Zero;
		m_direction = Vector3::Zero;
	}

	Ray::Ray(const Vector3& origin, const Vector3& end)
	{
		m_origin = origin;
		m_end = end;
		m_direction = (end - origin).Normalized();
	}

	Ray::~Ray()
	{

	}

	float Ray::HitDistance(const BoundingBox& box)
	{
		// If undefined, no hit (infinite distance)
		if (!box.Defined())
			return INFINITY;
		
		// Check for ray origin being inside the box
		if (box.IsInside(m_origin))
			return 0.0f;

		float dist = INFINITY;

		// Check for intersecting in the X-direction
		if (m_origin.x < box.min.x && m_direction.x > 0.0f)
		{
			float x = (box.min.x - m_origin.x) / m_direction.x;
			if (x < dist)
			{
				Vector3 point = m_origin + x * m_direction;
				if (point.y >= box.min.y && point.y <= box.max.y && point.z >= box.min.z && point.z <= box.max.z)
				{
					dist = x;
				}
			}
		}
		if (m_origin.x > box.max.x && m_direction.x < 0.0f)
		{
			float x = (box.max.x - m_origin.x) / m_direction.x;
			if (x < dist)
			{
				Vector3 point = m_origin + x * m_direction;
				if (point.y >= box.min.y && point.y <= box.max.y && point.z >= box.min.z && point.z <= box.max.z)
				{
					dist = x;
				}
			}
		}
		// Check for intersecting in the Y-direction
		if (m_origin.y < box.min.y && m_direction.y > 0.0f)
		{
			float x = (box.min.y - m_origin.y) / m_direction.y;
			if (x < dist)
			{
				Vector3 point = m_origin + x * m_direction;
				if (point.x >= box.min.x && point.x <= box.max.x && point.z >= box.min.z && point.z <= box.max.z)
				{
					dist = x;
				}
			}
		}
		if (m_origin.y > box.max.y && m_direction.y < 0.0f)
		{
			float x = (box.max.y - m_origin.y) / m_direction.y;
			if (x < dist)
			{
				Vector3 point = m_origin + x * m_direction;
				if (point.x >= box.min.x && point.x <= box.max.x && point.z >= box.min.z && point.z <= box.max.z)
				{
					dist = x;
				}
			}
		}
		// Check for intersecting in the Z-direction
		if (m_origin.z < box.min.z && m_direction.z > 0.0f)
		{
			float x = (box.min.z - m_origin.z) / m_direction.z;
			if (x < dist)
			{
				Vector3 point = m_origin + x * m_direction;
				if (point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y && point.y <= box.max.y)
				{
					dist = x;
				}
			}
		}
		if (m_origin.z > box.max.z && m_direction.z < 0.0f)
		{
			float x = (box.max.z - m_origin.z) / m_direction.z;
			if (x < dist)
			{
				Vector3 point = m_origin + x * m_direction;
				if (point.x >= box.min.x && point.x <= box.max.x && point.y >= box.min.y && point.y <= box.max.y)
				{
					dist = x;
				}
			}
		}

		return dist;
	}
}