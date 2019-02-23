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

//= INCLUDES ==============================
#include "Ray.h"
#include <algorithm>
#include "RayHit.h"
#include "BoundingBox.h"
#include "../World/Entity.h"
#include "../World/Components/Renderable.h"
#include "../World/Components/Skybox.h"
//=========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Directus::Math
{
	Ray::Ray()
	{

	}

	Ray::Ray(const Vector3& start, const Vector3& end)
	{
		m_start		= start;
		m_end		= end;
		m_direction = (end - start).Normalized();
	}

	Ray::~Ray()
	{

	}

	vector<RayHit> Ray::Trace(Context* context)
	{
		// Find all the entities that the ray hits
		vector<RayHit> hits;
		const vector<shared_ptr<Entity>>& entities = context->GetSubsystem<World>()->Entities_GetAll();
		for (const auto& entity : entities)
		{
			// Make sure there entity has a mesh and exclude the SkyBox
			if (!entity->HasComponent<Renderable>() || entity->HasComponent<Skybox>())
				continue;

			// Get bounding box
			BoundingBox aabb = entity->GetComponent<Renderable>()->Geometry_AABB();

			// Compute hit distance
			float hitDistance = HitDistance(aabb);

			// Don't store hit data if there was no hit
			if (hitDistance == INFINITY)
				continue;

			bool inside	= (hitDistance == 0.0f);
			hits.emplace_back(entity, hitDistance, inside);
		}

		// Sort by distance (ascending)
		sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b)
		{
			return a.m_distance < b.m_distance;
		});

		return hits;
	}

	float Ray::HitDistance(const BoundingBox& box)
	{
		// If undefined, no hit (infinite distance)
		if (!box.Defined())
			return INFINITY;
		
		// Check for ray origin being inside the box
		if (box.IsInside(m_start))
			return 0.0f;

		float dist = INFINITY;

		// Check for intersecting in the X-direction
		if (m_start.x < box.GetMin().x && m_direction.x > 0.0f)
		{
			float x = (box.GetMin().x - m_start.x) / m_direction.x;
			if (x < dist)
			{
				Vector3 point = m_start + x * m_direction;
				if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}
		if (m_start.x > box.GetMax().x && m_direction.x < 0.0f)
		{
			float x = (box.GetMax().x - m_start.x) / m_direction.x;
			if (x < dist)
			{
				Vector3 point = m_start + x * m_direction;
				if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}

		// Check for intersecting in the Y-direction
		if (m_start.y < box.GetMin().y && m_direction.y > 0.0f)
		{
			float x = (box.GetMin().y - m_start.y) / m_direction.y;
			if (x < dist)
			{
				Vector3 point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}
		if (m_start.y > box.GetMax().y && m_direction.y < 0.0f)
		{
			float x = (box.GetMax().y - m_start.y) / m_direction.y;
			if (x < dist)
			{
				Vector3 point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}

		// Check for intersecting in the Z-direction
		if (m_start.z < box.GetMin().z && m_direction.z > 0.0f)
		{
			float x = (box.GetMin().z - m_start.z) / m_direction.z;
			if (x < dist)
			{
				Vector3 point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
				{
					dist = x;
				}
			}
		}
		if (m_start.z > box.GetMax().z && m_direction.z < 0.0f)
		{
			float x = (box.GetMax().z - m_start.z) / m_direction.z;
			if (x < dist)
			{
				Vector3 point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
				{
					dist = x;
				}
			}
		}

		return dist;
	}
}