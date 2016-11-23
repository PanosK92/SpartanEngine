/*
Copyright(c) 2016 Panos Karabelas

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

Collider::Collider()
{
	m_shapeType = Box;
	m_shape = nullptr;
	m_boundingBox = Vector3::One;
	m_center = Vector3::Zero;
}

Collider::~Collider()
{

}

//= ICOMPONENT ========================================================================
void Collider::Initialize()
{
	m_lastKnownScale = g_transform->GetScale();
	UpdateBoundingBox();
	Build();
}

void Collider::Start()
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
		Build();
		m_lastKnownScale = g_transform->GetScale();
	}
}

void Collider::Serialize()
{
	Serializer::WriteInt(int(m_shapeType));
	Serializer::WriteVector3(m_boundingBox);
	Serializer::WriteVector3(m_center);
}

void Collider::Deserialize()
{
	m_shapeType = ColliderShape(Serializer::ReadInt());
	m_boundingBox = Serializer::ReadVector3();
	m_center = Serializer::ReadVector3();

	Build();
}

//= BOUNDING BOX =============================================
const Vector3& Collider::GetBoundingBox() const
{
	return m_boundingBox;
}

void Collider::SetBoundingBox(const Vector3& boundingBox)
{
	Vector3 clampedBoundingBox = boundingBox;

	clampedBoundingBox.x = Clamp(boundingBox.x, M_EPSILON, INFINITY);
	clampedBoundingBox.y = Clamp(boundingBox.y, M_EPSILON, INFINITY);
	clampedBoundingBox.z = Clamp(boundingBox.z, M_EPSILON, INFINITY);

	m_boundingBox = clampedBoundingBox;
}

//= CENTER ========================================================
const Vector3& Collider::GetCenter() const
{
	return m_center;
}

void Collider::SetCenter(const Vector3& center)
{
	m_center = center;
}

//= COLLISION SHAPE ================================================
ColliderShape Collider::GetShapeType() const
{
	return m_shapeType;
}

void Collider::SetShapeType(ColliderShape type)
{
	m_shapeType = type;
}

shared_ptr<btCollisionShape> Collider::GetBtCollisionShape() const
{
	return m_shape;
}

void Collider::Build()
{
	// delete old shape (if it exists)
	DeleteCollisionShape();

	// Create BOX shape
	if (m_shapeType == Box)
	{
		m_shape = make_shared<btBoxShape>(ToBtVector3(m_boundingBox));
	}

	// Create CAPSULE shape
	else if (m_shapeType == Capsule)
	{
		float height = max(m_boundingBox.x, m_boundingBox.z);
		height = max(height, m_boundingBox.y);

		float radius = min(m_boundingBox.x, m_boundingBox.z);
		radius = min(radius, m_boundingBox.y);

		m_shape = make_shared<btCapsuleShape>(radius, height);
	}

	// Create CYLINDER shape
	else if (m_shapeType == Cylinder)
	{
		m_shape = make_shared<btCylinderShape>(ToBtVector3(m_boundingBox));
	}

	// Create SPHERE shape
	else if (m_shapeType == Sphere)
	{
		float radius = max(m_boundingBox.x, m_boundingBox.y);
		radius = max(radius, m_boundingBox.z);

		m_shape = make_shared<btSphereShape>(radius);
	}

	SetRigidBodyCollisionShape(m_shape);
}

//= HELPER FUNCTIONS ======================================================
void Collider::UpdateBoundingBox()
{
	// Info
	// - MeshFilter returns pretransformed data
	// - Mesh returns raw untouched data

	MeshFilter* meshFilter = g_gameObject->GetComponent<MeshFilter>();
	if (meshFilter)
	{
		SetCenter(meshFilter->GetCenter());
		SetBoundingBox(meshFilter->GetBoundingBox());
	}
}

void Collider::DeleteCollisionShape()
{
	SetRigidBodyCollisionShape(nullptr);
}

void Collider::SetRigidBodyCollisionShape(shared_ptr<btCollisionShape> shape) const
{
	RigidBody* rigidBody = g_gameObject->GetComponent<RigidBody>();
	if (rigidBody)
		rigidBody->SetCollisionShape(shape);
}

weak_ptr<Mesh> Collider::GetMeshFromAttachedMeshFilter() const
{
	MeshFilter* meshFilter = g_gameObject->GetComponent<MeshFilter>();
	return meshFilter ? meshFilter->GetMesh() : weak_ptr<Mesh>();
}
//=========================================================================