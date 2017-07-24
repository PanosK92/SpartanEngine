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
#include "MeshFilter.h"
#include "RigidBody.h"
#include "../Core/GameObject.h"
#include "../IO/Serializer.h"
#include "../Physics/BulletPhysicsHelper.h"
#include <BulletCollision/CollisionShapes/btCollisionShape.h>
#include <BulletCollision/CollisionShapes/btBoxShape.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include "Transform.h"
//===========================================================

//= NAMESPACES ================
using namespace Directus::Math;
using namespace std;
//=============================

namespace Directus
{
	Collider::Collider()
	{
		Register();
		m_shapeType = Box;
		m_shape = nullptr;
		m_extents = Vector3::One;
		m_center = Vector3::Zero;
	}

	Collider::~Collider()
	{

	}

	//= ICOMPONENT ========================================================================
	void Collider::Reset()
	{
		m_lastKnownScale = g_transform->GetScale();
		UpdateBoundingBox();
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
		DeleteCollisionShape();
	}

	void Collider::Update()
	{
		// Ensure that the collider scales with the transform
		if (m_lastKnownScale != g_transform->GetScale())
		{
			UpdateBoundingBox();
			UpdateShape();
			m_lastKnownScale = g_transform->GetScale();
		}
	}

	void Collider::Serialize()
	{
		Serializer::WriteInt(int(m_shapeType));
		Serializer::WriteVector3(m_extents);
		Serializer::WriteVector3(m_center);
	}

	void Collider::Deserialize()
	{
		m_shapeType = ColliderShape(Serializer::ReadInt());
		m_extents = Serializer::ReadVector3();
		m_center = Serializer::ReadVector3();

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

	//= COLLISION SHAPE =======================================================
	void Collider::UpdateShape()
	{
		// delete old shape (if it exists)
		DeleteCollisionShape();

		// Create BOX shape
		if (m_shapeType == Box)
		{
			m_shape = make_shared<btBoxShape>(ToBtVector3(m_extents));
		}

		// Create CAPSULE shape
		else if (m_shapeType == Capsule)
		{
			float height = max(m_extents.x, m_extents.z);
			height = max(height, m_extents.y);

			float radius = min(m_extents.x, m_extents.z);
			radius = min(radius, m_extents.y);

			m_shape = make_shared<btCapsuleShape>(radius, height);
		}

		// Create CYLINDER shape
		else if (m_shapeType == Cylinder)
		{
			m_shape = make_shared<btCylinderShape>(ToBtVector3(m_extents));
		}

		// Create SPHERE shape
		else if (m_shapeType == Sphere)
		{
			float radius = max(m_extents.x, m_extents.y);
			radius = max(radius, m_extents.z);

			m_shape = make_shared<btSphereShape>(radius);
		}

		SetRigidBodyCollisionShape(m_shape);
	}
	//=========================================================================

	//= HELPER FUNCTIONS ======================================================
	void Collider::UpdateBoundingBox()
	{
		if (g_gameObject.expired())
			return;

		auto mesh = GetMeshFromAttachedMeshFilter();
		auto meshFilter = g_gameObject._Get()->GetComponent<MeshFilter>();
		if (!mesh.expired() && meshFilter)
		{
			BoundingBox box = meshFilter->GetBoundingBoxTransformed();
			SetCenter(box.GetCenter());
			SetBoundingBox(box.GetHalfSize());
		}
	}

	void Collider::DeleteCollisionShape()
	{
		SetRigidBodyCollisionShape(shared_ptr<btCollisionShape>());
		m_shape.reset();
	}

	void Collider::SetRigidBodyCollisionShape(shared_ptr<btCollisionShape> shape) const
	{
		if (g_gameObject.expired())
			return;

		RigidBody* rigidBody = g_gameObject.lock()->GetComponent<RigidBody>();
		if (rigidBody)
		{
			rigidBody->SetCollisionShape(shape);
		}
	}

	weak_ptr<Mesh> Collider::GetMeshFromAttachedMeshFilter() const
	{
		if (g_gameObject.expired())
			return weak_ptr<Mesh>();

		MeshFilter* meshFilter = g_gameObject._Get()->GetComponent<MeshFilter>();
		return meshFilter ? meshFilter->GetMesh() : weak_ptr<Mesh>();
	}
	//=========================================================================
}