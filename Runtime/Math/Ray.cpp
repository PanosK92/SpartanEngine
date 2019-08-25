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
#include "../Core/Context.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/Environment.h"
#include "../World/Components/Renderable.h"
//=========================================

//= NAMESPACES =====
using namespace std;
//==================

namespace Spartan::Math
{
	Ray::Ray(const Vector3& start, const Vector3& end)
	{
		m_start                 = start;
		m_end                   = end;
        Vector3 start_to_end    = (end - start);
        m_length                = start_to_end.Length();
		m_direction             = start_to_end.Normalized();
	}

	vector<RayHit> Ray::Trace(Context* context) const
	{
		// Find all the entities that the ray hits
		vector<RayHit> hits;
		const auto& entities = context->GetSubsystem<World>()->EntityGetAll();
		for (const auto& entity : entities)
		{
			// Make sure there entity has renderable
			if (!entity->HasComponent<Renderable>())
				continue;

			// Get object oriented bounding box
			const auto& aabb = entity->GetComponent<Renderable>()->GetAabb();

			// Compute hit distance
			auto distance = HitDistance(aabb);

			// Don't store hit data if there was no hit
			if (distance == INFINITY)
				continue;

            auto& hit_position = m_start + distance * m_direction;
			hits.emplace_back(
                entity,          // Entity
                hit_position,    // Position
                distance,        // Distance
                distance == 0.0f // Inside
            );
		}

		// Sort by distance (ascending)
		sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b)
		{
			return a.m_distance < b.m_distance;
		});

		return hits;
	}

	float Ray::HitDistance(const BoundingBox& box) const
	{
		// If undefined, no hit (infinite distance)
		if (!box.Defined())
			return INFINITY;
		
		// Check for ray origin being inside the box
		if (box.IsInside(m_start))
			return 0.0f;

		auto dist = INFINITY;

		// Check for intersecting in the X-direction
		if (m_start.x < box.GetMin().x && m_direction.x > 0.0f)
		{
			auto x = (box.GetMin().x - m_start.x) / m_direction.x;
			if (x < dist)
			{
				const auto point = m_start + x * m_direction;
				if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}
		if (m_start.x > box.GetMax().x && m_direction.x < 0.0f)
		{
			const auto x = (box.GetMax().x - m_start.x) / m_direction.x;
			if (x < dist)
			{
				const auto point = m_start + x * m_direction;
				if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}

		// Check for intersecting in the Y-direction
		if (m_start.y < box.GetMin().y && m_direction.y > 0.0f)
		{
			const auto x = (box.GetMin().y - m_start.y) / m_direction.y;
			if (x < dist)
			{
				const auto point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}
		if (m_start.y > box.GetMax().y && m_direction.y < 0.0f)
		{
			const auto x = (box.GetMax().y - m_start.y) / m_direction.y;
			if (x < dist)
			{
				const auto point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}

		// Check for intersecting in the Z-direction
		if (m_start.z < box.GetMin().z && m_direction.z > 0.0f)
		{
			const auto x = (box.GetMin().z - m_start.z) / m_direction.z;
			if (x < dist)
			{
				const auto point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
				{
					dist = x;
				}
			}
		}
		if (m_start.z > box.GetMax().z && m_direction.z < 0.0f)
		{
			const auto x = (box.GetMax().z - m_start.z) / m_direction.z;
			if (x < dist)
			{
				const auto point = m_start + x * m_direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
				{
					dist = x;
				}
			}
		}

		return dist;
	}
}
