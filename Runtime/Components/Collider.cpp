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

//= INCLUDES ==================================================
#include "Collider.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include "Transform.h"
#include "MeshFilter.h"
#include "RigidBody.h"
#include "../Core/GameObject.h"
#include "../IO/StreamIO.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "../Graphics/Mesh.h"
#include "../Logging/Log.h"
//=============================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Collider::Collider()
	{
		m_shapeType = CollishionShape_Box;
		m_extents = Vector3::One;
		m_center = Vector3::Zero;
	}

	Collider::~Collider()
	{

	}

	//= ICOMPONENT ==================================================================
	void Collider::Initialize()
	{
		m_lastKnownScale = g_transform->GetScale();
		UpdateShape();
	}

	void Collider::Start()
	{

	}

	void Collider::OnDisable()
	{

	}

	void Collider::Remove()
	{
		ReleaseShape();
	}

	void Collider::Update()
	{
		// Scale the collider if the transform scale has changed
		if (m_collisionShape && (m_lastKnownScale != g_transform->GetScale()))
		{	
			m_lastKnownScale = g_transform->GetScale();
			UpdateShape();
		}
	}

	void Collider::Serialize(StreamIO* stream)
	{
		stream->Write(int(m_shapeType));
		stream->Write(m_extents);
		stream->Write(m_center);
	}

	void Collider::Deserialize(StreamIO* stream)
	{
		m_shapeType = ColliderShape(stream->ReadInt());
		stream->Read(m_extents);
		stream->Read(m_center);

		UpdateShape();
	}
	//===============================================================================

	//= BOUNDING BOX =========================================
	void Collider::SetBoundingBox(const Vector3& boundingBox)
	{
		if (m_extents == boundingBox)
			return;

		m_extents = boundingBox;
		m_extents.x = Clamp(m_extents.x, M_EPSILON, INFINITY);
		m_extents.y = Clamp(m_extents.y, M_EPSILON, INFINITY);
		m_extents.z = Clamp(m_extents.z, M_EPSILON, INFINITY);

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
	
		Vector3 newWorldScale = g_transform->GetScale();

		switch (m_shapeType)
		{
		case CollishionShape_Box:
			m_collisionShape = make_shared<btBoxShape>(ToBtVector3(m_extents * 0.5f));
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case CollishionShape_Sphere:
			m_collisionShape = make_shared<btSphereShape>(m_extents.x);
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case CollishionShape_StaticPlane:
			m_collisionShape = make_shared<btStaticPlaneShape>(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
			break;

		case CollishionShape_Cylinder:
			m_collisionShape = make_shared<btCylinderShape>(btVector3(m_extents.x, m_extents.y, m_extents.x));
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case CollishionShape_Capsule:
			m_collisionShape = make_shared<btCapsuleShape>(m_extents.x, Max(m_extents.y - m_extents.x, 0.0f));
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case CollishionShape_Cone:
			m_collisionShape = make_shared<btConeShape>(m_extents.x, m_extents.y);
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case CollishionShape_Mesh:
			// Get mesh
			MeshFilter* meshFilter = g_gameObject._Get()->GetComponent<MeshFilter>();
			Mesh* mesh = nullptr;
			if (meshFilter)
			{
				if (meshFilter->GetMesh().expired())
					break;

				mesh = meshFilter->GetMesh()._Get();
			}

			// Validate vertex count
			if (mesh->GetVertexCount() >= m_vertexLimit)
			{
				LOG_WARNING("No user defined collider with more than " + to_string(m_vertexLimit) + " vertices is allowed.");
				break;
			}

			// Construct hull approximation
			m_collisionShape = make_shared<btConvexHullShape>(
				(btScalar*)&mesh->GetVertices()[0],	// points
				mesh->GetVertexCount(),				// point count
				sizeof(VertexPosTexTBN));			// stride

			// Scaling has to be done before (potential) optimization
			m_collisionShape->setLocalScaling(ToBtVector3(newWorldScale));

			// Optimize if requested
			if (m_optimize)
			{
				btConvexHullShape* hull = (btConvexHullShape*)m_collisionShape.get();
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

	void Collider::SetRigidBodyCollisionShape(shared_ptr<btCollisionShape> shape) const
	{
		if (g_gameObject.expired())
			return;

		RigidBody* rigidBody = g_gameObject._Get()->GetComponent<RigidBody>();
		if (rigidBody)
		{
			rigidBody->SetCollisionShape(shape);
		}
	}
	//=================================================================================
}