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

//= INCLUDES ==================================================
#include "Collider.h"
#include "Transform.h"
#include "RigidBody.h"
#include "Renderable.h"
#include "../Entity.h"
#include "../../IO/FileStream.h"
#include "../../Physics/BulletPhysicsHelper.h"
#include "../../Logging/Log.h"
#pragma warning(push, 0) // Hide warnings which belong to Bullet
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#pragma warning(pop)
//=============================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Collider::Collider(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_shapeType = ColliderShape_Box;
		m_center	= Vector3::Zero;
		m_size		= Vector3::One;
		m_shape			= nullptr;

		REGISTER_ATTRIBUTE_VALUE_VALUE(m_size, Vector3);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_center, Vector3);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_vertexLimit, unsigned int);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_optimize, bool);
		REGISTER_ATTRIBUTE_VALUE_SET(m_shapeType, SetShapeType, ColliderShape);
	}

	Collider::~Collider()
	{

	}

	void Collider::OnInitialize()
	{
		// If there is a mesh, use it's bounding box
		if (auto renderable = GetEntity_PtrRaw()->GetRenderable_PtrRaw())
		{
			m_center	= Vector3::Zero;
			m_size		= renderable->Geometry_AABB().GetSize();
		}

		Shape_Update();
	}

	void Collider::OnRemove()
	{
		Shape_Release();
	}

	void Collider::OnTick()
	{

	}

	void Collider::Serialize(FileStream* stream)
	{
		stream->Write(int(m_shapeType));
		stream->Write(m_size);
		stream->Write(m_center);
	}

	void Collider::Deserialize(FileStream* stream)
	{
		m_shapeType = ColliderShape(stream->ReadInt());
		stream->Read(&m_size);
		stream->Read(&m_center);

		Shape_Update();
	}

	void Collider::SetBoundingBox(const Vector3& boundingBox)
	{
		if (m_size == boundingBox)
			return;

		m_size = boundingBox;
		m_size.x = Clamp(m_size.x, M_EPSILON, INFINITY);
		m_size.y = Clamp(m_size.y, M_EPSILON, INFINITY);
		m_size.z = Clamp(m_size.z, M_EPSILON, INFINITY);

		Shape_Update();
	}

	void Collider::SetCenter(const Vector3& center)
	{
		if (m_center == center)
			return;

		m_center = center;
		RigidBody_SetCenterOfMass(m_center);
	}

	void Collider::SetShapeType(ColliderShape type)
	{
		if (m_shapeType == type)
			return;

		m_shapeType = type;
		Shape_Update();
	}

	void Collider::SetOptimize(bool optimize)
	{
		if (m_optimize == optimize)
			return;

		m_optimize = optimize;
		Shape_Update();
	}

	void Collider::Shape_Update()
	{
		Shape_Release();
		Vector3 worldScale = GetTransform()->GetScale();

		switch (m_shapeType)
		{
		case ColliderShape_Box:
			m_shape = new btBoxShape(ToBtVector3(m_size * 0.5f));
			m_shape->setLocalScaling(ToBtVector3(worldScale));
			break;

		case ColliderShape_Sphere:
			m_shape = new btSphereShape(m_size.x * 0.5f);
			m_shape->setLocalScaling(ToBtVector3(worldScale));
			break;

		case ColliderShape_StaticPlane:
			m_shape = new btStaticPlaneShape(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
			break;

		case ColliderShape_Cylinder:
			m_shape = new btCylinderShape(btVector3(m_size.x * 0.5f, m_size.y * 0.5f, m_size.x * 0.5f));
			m_shape->setLocalScaling(ToBtVector3(worldScale));
			break;

		case ColliderShape_Capsule:
			m_shape = new btCapsuleShape(m_size.x * 0.5f, Max(m_size.y - m_size.x, 0.0f));
			m_shape->setLocalScaling(ToBtVector3(worldScale));
			break;

		case ColliderShape_Cone:
			m_shape = new btConeShape(m_size.x * 0.5f, m_size.y);
			m_shape->setLocalScaling(ToBtVector3(worldScale));
			break;

		case ColliderShape_Mesh:
			// Get Renderable
			Renderable* renderable = GetEntity_PtrRaw()->GetComponent<Renderable>().get();
			if (!renderable)
			{
				LOG_WARNING("Collider::Shape_Update: Can't construct mesh shape, there is no Renderable component attached.");
				return;
			}

			// Validate vertex count
			if (renderable->Geometry_VertexCount() >= m_vertexLimit)
			{
				LOG_WARNING("Collider::Shape_Update: No user defined collider with more than " + to_string(m_vertexLimit) + " vertices is allowed.");
				return;
			}

			// Get geometry
			vector<unsigned int> indices;
			vector<RHI_Vertex_PosUvNorTan> vertices;
			renderable->Geometry_Get(&indices, &vertices);

			if (vertices.empty())
			{
				LOG_WARNING("Collider::UpdateShape: No vertices.");
				return;
			}

			// Construct hull approximation
			m_shape = new btConvexHullShape(
				(btScalar*)&vertices[0],					// points
				renderable->Geometry_VertexCount(),			// point count
				(unsigned int)sizeof(RHI_Vertex_PosUvNorTan));	// stride

			// Scaling has to be done before (potential) optimization
			m_shape->setLocalScaling(ToBtVector3(worldScale));

			// Optimize if requested
			if (m_optimize)
			{
				auto hull = (btConvexHullShape*)m_shape;
				hull->optimizeConvexHull();
				hull->initializePolyhedralFeatures();
			}
			break;
		}

		m_shape->setUserPointer(this);

		RigidBody_SetShape(m_shape);
		RigidBody_SetCenterOfMass(m_center);
	}

	void Collider::Shape_Release()
	{
		RigidBody_SetShape(nullptr);
		SafeDelete(m_shape);
	}

	void Collider::RigidBody_SetShape(btCollisionShape* shape)
	{
		if (const auto& rigidBody = m_entity->GetComponent<RigidBody>())
		{
			rigidBody->SetShape(shape);
		}
	}

	void Collider::RigidBody_SetCenterOfMass(const Vector3& center)
	{
		if (const auto& rigidBody = m_entity->GetComponent<RigidBody>())
		{
			rigidBody->SetCenterOfMass(center);
		}
	}
}