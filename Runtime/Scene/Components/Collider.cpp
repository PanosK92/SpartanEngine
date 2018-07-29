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

//= INCLUDES ==================================================
#include "Collider.h"
#include "Transform.h"
#include "RigidBody.h"
#include "Renderable.h"
#include "../Actor.h"
#include "../../IO/FileStream.h"
#include "../../Physics/BulletPhysicsHelper.h"
#include "../../Rendering/Mesh.h"
#include "../../RHI/IRHI_Vertex.h"
#include "../../Logging/Log.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
//=============================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Collider::Collider(Context* context, Actor* actor, Transform* transform) : IComponent(context, actor, transform)
	{
		m_shapeType = ColliderShape_Box;
		m_center	= Vector3::Zero;
		m_size		= Vector3::One;
	}

	Collider::~Collider()
	{

	}

	//= ICOMPONENT ==================================================================
	void Collider::OnInitialize()
	{
		// If there is a mesh, use it's bounding box
		if (auto renderable = Getactor_PtrRaw()->GetRenderable_PtrRaw())
		{
			m_center = GetTransform()->GetPosition();
			m_size = renderable->Geometry_BB().GetSize();
		}

		UpdateShape();
	}

	void Collider::OnRemove()
	{
		ReleaseShape();
	}

	void Collider::OnUpdate()
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

		UpdateShape();
	}
	//===============================================================================

	//= BOUNDING BOX =========================================
	void Collider::SetBoundingBox(const Vector3& boundingBox)
	{
		if (m_size == boundingBox)
			return;

		m_size = boundingBox;
		m_size.x = Clamp(m_size.x, M_EPSILON, INFINITY);
		m_size.y = Clamp(m_size.y, M_EPSILON, INFINITY);
		m_size.z = Clamp(m_size.z, M_EPSILON, INFINITY);

		UpdateShape();
	}

	void Collider::SetCenter(const Vector3& center)
	{
		if (m_center == center)
			return;

		m_center = center;
		UpdateShape();
	}

	void Collider::SetShapeType(ColliderShape type)
	{
		if (m_shapeType == type)
			return;

		m_shapeType = type;
		UpdateShape();
	}

	void Collider::SetOptimize(bool optimize)
	{
		if (m_optimize == optimize)
			return;

		m_optimize = optimize;
		UpdateShape();
	}
	//========================================================

	//= COLLISION SHAPE =======================================================
	void Collider::UpdateShape()
	{
		// Release previous shape
		ReleaseShape();

		Vector3 newWorldScale = GetTransform()->GetScale();

		switch (m_shapeType)
		{
		case ColliderShape_Box:
			m_collisionShape = make_shared<btBoxShape>(ToBtVector3(m_size * 0.5f));
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case ColliderShape_Sphere:
			m_collisionShape = make_shared<btSphereShape>(m_size.x * 0.5f);
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case ColliderShape_StaticPlane:
			m_collisionShape = make_shared<btStaticPlaneShape>(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
			break;

		case ColliderShape_Cylinder:
			m_collisionShape = make_shared<btCylinderShape>(btVector3(m_size.x * 0.5f, m_size.y * 0.5f, m_size.x * 0.5f));
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case ColliderShape_Capsule:
			m_collisionShape = make_shared<btCapsuleShape>(m_size.x * 0.5f, Max(m_size.y - m_size.x, 0.0f));
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case ColliderShape_Cone:
			m_collisionShape = make_shared<btConeShape>(m_size.x * 0.5f, m_size.y);
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case ColliderShape_Mesh:
			// Get Renderable
			Renderable* renderable = Getactor_PtrRaw()->GetComponent<Renderable>().lock().get();
			if (!renderable)
			{
				LOG_WARNING("Collider::UpdateShape: Can't construct mesh shape, there is no Renderable component attached.");
				return;
			}

			// Validate vertex count
			if (renderable->Geometry_VertexCount() >= m_vertexLimit)
			{
				LOG_WARNING("Collider::UpdateShape: No user defined collider with more than " + to_string(m_vertexLimit) + " vertices is allowed.");
				break;
			}

			// Get geometry
			vector<unsigned int> indices;
			vector<RHI_Vertex_PosUVTBN> vertices;
			renderable->Geometry_Get(&indices, &vertices);

			if (vertices.empty())
			{
				LOG_WARNING("Collider::UpdateShape: No vertices.");
				return;
			}

			// Construct hull approximation
			m_collisionShape = make_shared<btConvexHullShape>(
				(btScalar*)&vertices[0],					// points
				renderable->Geometry_VertexCount(),			// point count
				(unsigned int)sizeof(RHI_Vertex_PosUVTBN));	// stride

			// Scaling has to be done before (potential) optimization
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));

			// Optimize if requested
			if (m_optimize)
			{
				auto hull = (btConvexHullShape*)m_collisionShape.get();
				hull->optimizeConvexHull();
				hull->initializePolyhedralFeatures();
			}
			break;
		}

		SetRigidBodyCollisionShape(m_collisionShape);
	}
	//=========================================================================

	//= PRIVATE =======================================================================
	void Collider::ReleaseShape()
	{
		SetRigidBodyCollisionShape(shared_ptr<btCollisionShape>());
		m_collisionShape.reset();
	}

	void Collider::SetRigidBodyCollisionShape(shared_ptr<btCollisionShape> shape)
	{
		RigidBody* rigidBody = Getactor_PtrRaw()->GetComponent<RigidBody>().lock().get();
		if (rigidBody)
		{
			rigidBody->SetCollisionShape(shape);
		}
	}
	//=================================================================================
}