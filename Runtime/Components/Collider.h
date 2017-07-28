/*
Copyright(c) 2016-2017 Panos Karabelas

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
#include "Component.h"
#include <memory>
#include "../Math/Vector3.h"
//==========================

class btCollisionShape;

namespace Directus
{
	class Mesh;
	class MeshFilter;

	enum ColliderShape
	{
		Box,
		Sphere,
		Static_Plane,
		Cylinder,
		Capsule,
		Cone
	};

	class DLL_API Collider : public Component
	{
	public:
		Collider();
		~Collider();

		//= ICOMPONENT =============
		virtual void Reset();
		virtual void Start();
		virtual void OnDisable();
		virtual void Remove();
		virtual void Update();
		virtual void Serialize();
		virtual void Deserialize();
		//==========================

		// Bounding box
		const Math::Vector3& GetBoundingBox() { return m_extents; }
		void SetBoundingBox(const Math::Vector3& boundingBox);

		// Collider center
		const Math::Vector3& GetCenter() { return m_center; }
		void SetCenter(const Math::Vector3& center) { m_center = center; }

		// Collision shape type
		ColliderShape GetShapeType() { return m_shapeType; }
		void SetShapeType(ColliderShape type) { m_shapeType = type; }

		// Collision shape
		std::shared_ptr<btCollisionShape> GetBtCollisionShape() { return m_shape; }

		void UpdateShape();

	private:
		// Deletes the collision shape
		void ReleaseShape();

		// Set a collision shape
		void SetRigidBodyCollisionShape(std::shared_ptr<btCollisionShape> shape) const;

		ColliderShape m_shapeType;
		std::shared_ptr<btCollisionShape> m_shape;
		Math::Vector3 m_extents;
		Math::Vector3 m_center;
		Math::Vector3 m_lastKnownScale;
		std::weak_ptr<Mesh> m_mesh;
	};
}