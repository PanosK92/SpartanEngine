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

//= INCLUDES ========================
#include "Collider.h"
#include "Mesh.h"
#include "RigidBody.h"
#include <algorithm>
#include "../Core/GameObject.h"
#include "../IO/Serializer.h"
#include "../Physics/PhysicsEngine.h"
//===================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

Collider::Collider()
{
	m_shapeType = Box;
	m_shape = nullptr;
	m_isDirty = false;
	m_boundingBox = Vector3(1.0, 1.0f, 1.0f);
	m_center = Vector3::Zero;
}

Collider::~Collider()
{
	delete m_shape;
	m_shape = nullptr;

	SetRigidBodyCollisionShape(nullptr);
}

/*------------------------------------------------------------------------------
							[INTERFACE]
------------------------------------------------------------------------------*/
void Collider::Initialize()
{
	// get bounding box and center
	if (g_gameObject->HasComponent<Mesh>())
	{
		Mesh* mesh = g_gameObject->GetComponent<Mesh>();
		m_boundingBox = mesh->GetExtent();
		m_center = mesh->GetCenter();
	}

	m_isDirty = true;
}

void Collider::Update()
{
	if (!m_isDirty)
		return;

	// This function constructs a btCollisionShape and assigns it to a RigidBody (if attached).
	// It's important that it's called here and not in Load() as there is a possibility
	// that the RigidBody has not been attached yet (still loading).
	ConstructCollisionShape();

	m_isDirty = false;
}

void Collider::Serialize()
{
	Serializer::SaveInt(int(m_shapeType));
	Serializer::SaveVector3(m_boundingBox);
	Serializer::SaveVector3(m_center);
}

void Collider::Deserialize()
{
	m_shapeType = ShapeType(Serializer::LoadInt());
	m_boundingBox = Serializer::LoadVector3();
	m_center = Serializer::LoadVector3();

	m_isDirty = true;
}

/*------------------------------------------------------------------------------
								[PROPERTIES]
------------------------------------------------------------------------------*/
Vector3 Collider::GetBoundingBox()
{
	return m_boundingBox;
}

void Collider::SetBoundingBox(Vector3 boxSize)
{
	if (boxSize == Vector3::Zero)
		return;

	m_boundingBox = boxSize.Absolute();
	ConstructCollisionShape();
}

Vector3 Collider::GetScale()
{
	if (!m_shape)
		return Vector3(0, 0, 0);

	btVector3 scale = m_shape->getLocalScaling();
	return Vector3(scale.x(), scale.y(), scale.z());
}

void Collider::SetScale(Vector3 scale)
{
	if (!m_shape)
		return;

	if (scale.x == 0 || scale.y == 0 || scale.z == 0)
		return;

	m_shape->setLocalScaling(Vector3ToBtVector3(scale.Absolute()));
}

Vector3 Collider::GetCenter()
{
	return m_center;
}

void Collider::SetCenter(Vector3 center)
{
	m_center = center;

	m_isDirty = true;
}

btCollisionShape* Collider::GetShape()
{
	return m_shape;
}

void Collider::SetShapeType(ShapeType type)
{
	m_shapeType = type;
	ConstructCollisionShape();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
void Collider::ConstructCollisionShape()
{
	// delete old shape (if it exists)
	if (m_shape)
	{
		delete m_shape;
		m_shape = nullptr;
	}

	// create new shape
	if (m_shapeType == Box)
	{
		m_shape = new btBoxShape(Vector3ToBtVector3(m_boundingBox));
	}
	else if (m_shapeType == Capsule)
	{
		float radius = max(m_boundingBox.x, m_boundingBox.z);
		float height = m_boundingBox.y;
		m_shape = new btCapsuleShape(radius, height);
	}
	else if (m_shapeType == Cylinder)
	{
		m_shape = new btCylinderShape(Vector3ToBtVector3(m_boundingBox));
	}
	else if (m_shapeType == Sphere)
	{
		float radius = max(m_boundingBox.x, m_boundingBox.y);
		radius = max(radius, m_boundingBox.z);
		m_shape = new btSphereShape(radius);
	}

	SetRigidBodyCollisionShape(m_shape);
}

void Collider::SetRigidBodyCollisionShape(btCollisionShape* shape)
{
	if (!g_gameObject)
		return;

	RigidBody* rigidBody = g_gameObject->GetComponent<RigidBody>();
	if (rigidBody)
		rigidBody->SetCollisionShape(shape);
}
