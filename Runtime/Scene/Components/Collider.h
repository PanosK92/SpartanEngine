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

//= INCLUDES ==================
#include "Component.h"
#include <memory>
#include "../../Math/Vector3.h"
//=============================

class btCollisionShape;

namespace Directus
{
	class Mesh;
	class MeshFilter;

	enum ColliderShape
	{
		ColliderShape_Box,
		ColliderShape_Sphere,
		ColliderShape_StaticPlane,
		ColliderShape_Cylinder,
		ColliderShape_Capsule,
		ColliderShape_Cone,
		ColliderShape_Mesh,
	};

	class ENGINE_CLASS Collider : public Component
	{
	public:
		Collider();
		~Collider();

		//= ICOMPONENT ===============================
		void Initialize() override;
		void Remove() override;
		void Update() override;
		void Serialize(FileStream* stream) override;
		void Deserialize(FileStream* stream) override;
		//============================================

		// Bounding box
		const Math::Vector3& GetBoundingBox() { return m_size; }
		void SetBoundingBox(const Math::Vector3& boundingBox);

		// Collider center
		const Math::Vector3& GetCenter() { return m_center; }
		void SetCenter(const Math::Vector3& center);

		// Collision shape type
		ColliderShape GetShapeType() { return m_shapeType; }
		void SetShapeType(ColliderShape type);

		// Collision shape
		std::shared_ptr<btCollisionShape> GetBtCollisionShape() { return m_collisionShape; }

		bool GetOptimize() { return m_optimize; }
		void SetOptimize(bool optimize);

	private:
		// Update the collision shape
		void UpdateShape();

		// Deletes the collision shape
		void ReleaseShape();

		// Set a collision shape
		void SetRigidBodyCollisionShape(std::shared_ptr<btCollisionShape> shape);

		ColliderShape m_shapeType;
		std::shared_ptr<btCollisionShape> m_collisionShape;
		Math::Vector3 m_size;
		Math::Vector3 m_center;
		Math::Vector3 m_lastKnownScale;
		unsigned int m_vertexLimit = 100000;
		bool m_optimize = true;
	};
}