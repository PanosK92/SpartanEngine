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

//= INCLUDES =================================================
#include "Collider.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include "Transform.h"
#include "MeshFilter.h"
#include "RigidBody.h"
#include "../Core/GameObject.h"
#include "../IO/StreamIO.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "../Graphics/Mesh.h"
//===========================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Collider::Collider()
	{
		m_shapeType = Box;
		m_extents = Vector3::One;
		m_center = Vector3::Zero;
		m_mesh = weak_ptr<Mesh>();
	}

	Collider::~Collider()
	{

	}

	//= ICOMPONENT ========================================================================
	void Collider::Reset()
	{
		// Get the mesh
		if (!g_gameObject.expired())
		{
			MeshFilter* meshFilter = g_gameObject._Get()->GetComponent<MeshFilter>();
			m_mesh = meshFilter->GetMesh();
		}

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
		if (m_lastKnownScale != g_transform->GetScale())
		{	
			m_lastKnownScale = g_transform->GetScale();
			m_shape->setLocalScaling(ToBtVector3(m_lastKnownScale));
		}
	}

	void Collider::Serialize()
	{
		StreamIO::WriteInt(int(m_shapeType));
		StreamIO::WriteVector3(m_extents);
		StreamIO::WriteVector3(m_center);
	}

	void Collider::Deserialize()
	{
		m_shapeType = ColliderShape(StreamIO::ReadInt());
		m_extents = StreamIO::ReadVector3();
		m_center = StreamIO::ReadVector3();

		UpdateShape();
	}

	//= BOUNDING BOX =============================================
	void Collider::SetBoundingBox(const Vector3& boundingBox)
	{
		m_extents = boundingBox;

		m_extents.x = Clamp(m_extents.x, M_EPSILON, INFINITY);
		m_extents.y = Clamp(m_extents.y, M_EPSILON, INFINITY);
		m_extents.z = Clamp(m_extents.z, M_EPSILON, INFINITY);
	}
	//=============================================================

	//= COLLISION SHAPE =======================================================
	void Collider::UpdateShape()
	{
		// Release previous shape
		ReleaseShape();
	
		Vector3 newWorldScale = g_transform->GetScale();

		switch (m_shapeType)
		{
		case Box:
			m_shape = make_shared<btBoxShape>(ToBtVector3(m_extents * 0.5f));
			m_shape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case Sphere:
			m_shape = make_shared<btSphereShape>(m_extents.x);
			m_shape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case Static_Plane:
			m_shape = make_shared<btStaticPlaneShape>(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
			break;

		case Cylinder:
			m_shape = make_shared<btCylinderShape>(btVector3(m_extents.x, m_extents.y, m_extents.x));
			m_shape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case Capsule:
			m_shape = make_shared<btCapsuleShape>(m_extents.x, Max(m_extents.y - m_extents.x, 0.0f));
			m_shape->setLocalScaling(ToBtVector3(newWorldScale));
			break;

		case Cone:
			m_shape = make_shared<btConeShape>(m_extents.x, m_extents.y);
			m_shape->setLocalScaling(ToBtVector3(newWorldScale));
			break;
		}

		SetRigidBodyCollisionShape(m_shape);
	}
	//=========================================================================

	//= PRIVATE ===========================================================================================
	void Collider::ReleaseShape()
	{
		SetRigidBodyCollisionShape(shared_ptr<btCollisionShape>());
		m_shape.reset();
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
	//======================================================================================================
}