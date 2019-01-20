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
#include "RayHit.h"
#include "BoundingBox.h"
#include "../World/Actor.h"
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

	Ray::~Ray()
	{

	}

	vector<RayHit> Ray::Trace(Context* context, const Vector3& start, const Vector3& end)
	{
		m_start		= start;
		m_end		= end;
		m_direction = (end - start).Normalized();

		// Find all the actors that the ray hits
		vector<RayHit> hits;
		const vector<shared_ptr<Actor>>& actors = context->GetSubsystem<World>()->Actors_GetAll();
		for (const auto& actor : actors)
		{
			// Make sure there actor has a mesh and exclude the SkyBox
			if (!actor->HasComponent<Renderable>() || actor->HasComponent<Skybox>())
				continue;

			// Get bounding box
			BoundingBox aabb = actor->GetComponent<Renderable>()->Geometry_AABB();

			// Compute hit distance
			float hitDistance = HitDistance(m_start, m_direction, aabb);

			// Don't store hit data if there was no hit
			if (hitDistance == INFINITY)
				continue;

			bool inside	= (hitDistance == 0.0f);
			hits.emplace_back(actor, hitDistance, inside);
		}

		// Sort by distance (ascending)
		sort(hits.begin(), hits.end(), [](const RayHit& a, const RayHit& b)
		{
			return a.m_distance < b.m_distance;
		});

		return hits;
	}

	float Ray::HitDistance(const Vector3& start, const Vector3& direction, const BoundingBox& box)
	{
		// If undefined, no hit (infinite distance)
		if (!box.Defined())
			return INFINITY;
		
		// Check for ray origin being inside the box
		if (box.IsInside(start))
			return 0.0f;

		float dist = INFINITY;

		// Check for intersecting in the X-direction
		if (start.x < box.GetMin().x && direction.x > 0.0f)
		{
			float x = (box.GetMin().x - start.x) / direction.x;
			if (x < dist)
			{
				Vector3 point = start + x * direction;
				if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}
		if (start.x > box.GetMax().x && direction.x < 0.0f)
		{
			float x = (box.GetMax().x - start.x) / direction.x;
			if (x < dist)
			{
				Vector3 point = start + x * direction;
				if (point.y >= box.GetMin().y && point.y <= box.GetMax().y && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}

		// Check for intersecting in the Y-direction
		if (start.y < box.GetMin().y && direction.y > 0.0f)
		{
			float x = (box.GetMin().y - start.y) / direction.y;
			if (x < dist)
			{
				Vector3 point = start + x * direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}
		if (start.y > box.GetMax().y && direction.y < 0.0f)
		{
			float x = (box.GetMax().y - start.y) / direction.y;
			if (x < dist)
			{
				Vector3 point = start + x * direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.z >= box.GetMin().z && point.z <= box.GetMax().z)
				{
					dist = x;
				}
			}
		}

		// Check for intersecting in the Z-direction
		if (start.z < box.GetMin().z && direction.z > 0.0f)
		{
			float x = (box.GetMin().z - start.z) / direction.z;
			if (x < dist)
			{
				Vector3 point = start + x * direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
				{
					dist = x;
				}
			}
		}
		if (start.z > box.GetMax().z && direction.z < 0.0f)
		{
			float x = (box.GetMax().z - start.z) / direction.z;
			if (x < dist)
			{
				Vector3 point = start + x * direction;
				if (point.x >= box.GetMin().x && point.x <= box.GetMax().x && point.y >= box.GetMin().y && point.y <= box.GetMax().y)
				{
					dist = x;
				}
			}
		}

		return dist;
	}
}