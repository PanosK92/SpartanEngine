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
#include "Hinge.h"
#include "RigidBody.h"
#include "../IO/Serializer.h"
#include "../Core/GameObjectPool.h"
#include "../Core/GameObject.h"
#include "../Physics/PhysicsWorld.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
//============================================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

Hinge::Hinge()
{
	m_hinge = nullptr;
	m_connectedGameObject = nullptr;
	m_isConnected = false;

	m_isDirty = false;
}

Hinge::~Hinge()
{
	
}

/*------------------------------------------------------------------------------
									[INTERFACE]
------------------------------------------------------------------------------*/
void Hinge::Awake()
{
	// A is the chassis and B is the tyre.
	m_axisA = Vector3(0.f, 1.f, 0.f); // The axis in A should be equal to to the axis in B and point away from the car off to the side.
	m_axisB = Vector3(0.f, 0.f, 0.f); // The axis in A should be equal to to the axis in B and point away from the car off to the side.
	m_pivotA = Vector3(0.f, 1.f, 0.f); // the mount point for the tyre on the chassis
	m_pivotB = Vector3(0.f, 0.f, 0.f); // the centre of the tyre
}

void Hinge::Start()
{

}

void Hinge::Remove()
{
	g_context->GetSubsystem<PhysicsWorld>()->GetWorld()->removeConstraint(m_hinge);
}

void Hinge::Update()
{
	ComponentCheck();

	if (!m_isDirty)
		return;

	ConstructHinge();

	m_isDirty = false;
}

void Hinge::Serialize()
{
	Serializer::WriteBool(m_isConnected);
	if (m_isConnected)
	{
		// save gameobject
		Serializer::WriteSTR(m_connectedGameObject->GetID());
	}

	Serializer::WriteVector3(m_axisA);
	Serializer::WriteVector3(m_axisB);
	Serializer::WriteVector3(m_pivotA);
	Serializer::WriteVector3(m_pivotB);
}

void Hinge::Deserialize()
{
	m_isConnected = Serializer::ReadBool();
	if (m_isConnected)
	{
		// load gameobject
		std::string gameObjectID = Serializer::ReadSTR();
		m_connectedGameObject = GameObjectPool::GetInstance().GetGameObjectByID(gameObjectID);
	}

	m_axisA = Serializer::ReadVector3();
	m_axisB = Serializer::ReadVector3();
	m_pivotA = Serializer::ReadVector3();
	m_pivotB = Serializer::ReadVector3();

	m_isDirty = true;
}

void Hinge::SetConnectedGameObject(GameObject* connectedRigidBody)
{
	m_connectedGameObject = connectedRigidBody;
	m_isConnected = true;

	m_isDirty = true;
}

GameObject* Hinge::GetConnectedGameObject()
{
	return m_connectedGameObject;
}

void Hinge::SetAxis(Vector3 axis)
{
	m_axisA = axis;
	m_isDirty = true;
}

Vector3 Hinge::GetAxis()
{
	return m_axisA;
}

void Hinge::SetPivot(Vector3 pivot)
{
	m_pivotA = pivot;
	m_isDirty = true;
}

Vector3 Hinge::GetPivot()
{
	return m_pivotA;
}

void Hinge::SetPivotConnected(Vector3 pivot)
{
	m_pivotB = pivot;
	m_isDirty = true;
}

Vector3 Hinge::GetPivotConnected()
{
	return m_pivotB;
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void Hinge::ConstructHinge()
{
	if (!m_connectedGameObject)
		return;

	if (m_hinge)
	{
		g_context->GetSubsystem<PhysicsWorld>()->GetWorld()->removeConstraint(m_hinge);
		delete m_hinge;
		m_hinge = nullptr;
	}

	// get the rigidbodies
	auto rigidBodyA = g_gameObject->GetComponent<RigidBody>()->GetBtRigidBody();
	auto rigidBodyB = m_connectedGameObject->GetComponent<RigidBody>()->GetBtRigidBody();

	CalculateConnections();

	// convert data to bullet data
	btVector3 axisA = ToBtVector3(m_axisA);
	btVector3 axisB = ToBtVector3(m_axisB);
	btVector3 pivotA = ToBtVector3(m_pivotA);
	btVector3 pivotB = ToBtVector3(m_pivotB);

	// create the hinge
	m_hinge = new btHingeConstraint(*rigidBodyA.lock(), *rigidBodyB.lock(), axisA, axisB, pivotA, pivotB);
	m_hinge->enableAngularMotor(true, 2, 3);

	// add it to the world
	g_context->GetSubsystem<PhysicsWorld>()->GetWorld()->addConstraint(m_hinge);
}

void Hinge::CalculateConnections()
{
	m_axisB = m_axisA; // The axis in A should be equal to to the axis in B and point away from the car off to the side.
}

void Hinge::ComponentCheck()
{
	if (!g_gameObject->HasComponent<RigidBody>())
		g_gameObject->AddComponent<RigidBody>();
}
