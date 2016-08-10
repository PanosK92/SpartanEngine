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
#include "../Core/Globals.h"
#include "Transform.h"
//===========================================================

//= NAMESPACES ================
using namespace Directus::Math;
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
	SafeDelete(m_shape);
}

//= ICOMPONENT ========================================================================
void Collider::Initialize()
{
	Mesh* mesh = GetMeshFromAttachedMeshFilter();
	if (mesh)
	{
		m_boundingBox = mesh->GetBoundingBox() * g_transform->GetWorldTransform();
		m_center = mesh->GetCenter() * g_transform->GetWorldTransform();
	}

	ConstructCollisionShape();
}

void Collider::Start()
{

}

void Collider::Remove()
{
	SetRigidBodyCollisionShape(nullptr);
}

void Collider::Update()
{

}

void Collider::Serialize()
{
	Serializer::SaveInt(int(m_shapeType));
	Serializer::SaveVector3(m_boundingBox);
	Serializer::SaveVector3(m_center);
}

void Collider::Deserialize()
{
	m_shapeType = ColliderShape(Serializer::LoadInt());
	m_boundingBox = Serializer::LoadVector3();
	m_center = Serializer::LoadVector3();

	ConstructCollisionShape();
}

//= BOUNDING BOX =============================================
const Vector3& Collider::GetBoundingBox() const
{
	return m_boundingBox;
}

void Collider::SetBoundingBox(const Vector3& boxSize)
{
	if (boxSize.x == boxSize.y == boxSize.z == 0.0f)
		return;

	m_boundingBox = boxSize.Absolute();

	ConstructCollisionShape();
}

//= CENTER ========================================================
const Vector3& Collider::GetCenter() const
{
	return m_center;
}

void Collider::SetCenter(const Vector3& center)
{
	m_center = center;

	ConstructCollisionShape();
}

//= COLLISION SHAPE ================================================
ColliderShape Collider::GetShapeType() const
{
	return m_shapeType;
}

void Collider::SetShapeType(ColliderShape type)
{
	m_shapeType = type;

	ConstructCollisionShape();
}

btCollisionShape* Collider::GetBtCollisionShape() const
{
	return m_shape;
}

//= MISC ====================================================================
void Collider::ConstructCollisionShape()
{
	// delete old shape (if it exists)
	SetRigidBodyCollisionShape(nullptr);
	SafeDelete(m_shape);

	// Create BOX shape
	if (m_shapeType == Box)
	{
		m_shape = new btBoxShape(ToBtVector3(m_boundingBox));
	}

	// Create CAPSULE shape
	if (m_shapeType == Capsule)
	{
		float radius = max(m_boundingBox.x, m_boundingBox.z);
		float height = m_boundingBox.y;
		m_shape = new btCapsuleShape(radius, height);
	}

	// Create CYLINDER shape
	if (m_shapeType == Cylinder)
	{
		m_shape = new btCylinderShape(ToBtVector3(m_boundingBox));
	}

	// Create SPHERE shape
	if (m_shapeType == Sphere)
	{
		float radius = max(m_boundingBox.x, m_boundingBox.y);
		radius = max(radius, m_boundingBox.z);
		m_shape = new btSphereShape(radius);
	}

	SetRigidBodyCollisionShape(m_shape);
}

void Collider::SetRigidBodyCollisionShape(btCollisionShape* shape) const
{
	if (!g_gameObject)
		return;

	RigidBody* rigidBody = g_gameObject->GetComponent<RigidBody>();
	if (rigidBody)
		rigidBody->SetCollisionShape(shape);
}

Mesh* Collider::GetMeshFromAttachedMeshFilter() const
{
	MeshFilter* meshFilter = g_gameObject->GetComponent<MeshFilter>();
	return meshFilter ? meshFilter->GetMesh() : nullptr;
}