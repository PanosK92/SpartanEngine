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
#include "Hinge.h"
#include "RigidBody.h"
#include "../IO/StreamIO.h"
#include "../Core/Scene.h"
#include "../Core/GameObject.h"
#include "../Physics/Physics.h"
#include "../Physics/BulletPhysicsHelper.h"
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
//============================================================

//= NAMESPACES ================
using namespace Directus::Math;
//=============================

namespace Directus
{
	Hinge::Hinge()
	{
		m_hinge = nullptr;
		m_isConnected = false;
		m_isDirty = false;
	}

	Hinge::~Hinge()
	{

	}

	/*------------------------------------------------------------------------------
										[INTERFACE]
	------------------------------------------------------------------------------*/
	void Hinge::Reset()
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

	void Hinge::OnDisable()
	{

	}

	void Hinge::Remove()
	{
		g_context->GetSubsystem<Physics>()->GetWorld()->removeConstraint(m_hinge);
	}

	void Hinge::Update()
	{
		ComponentCheck();

		if (!m_isDirty)
			return;

		ConstructHinge();

		m_isDirty = false;
	}

	void Hinge::Serialize(StreamIO* stream)
	{
		stream->Write(m_isConnected);
		if (m_isConnected)
		{
			if (!m_connectedGameObject.expired())
			{
				stream->Write(m_connectedGameObject._Get()->GetID());
			}
		}

		stream->Write(m_axisA);
		stream->Write(m_axisB);
		stream->Write(m_pivotA);
		stream->Write(m_pivotB);
	}

	void Hinge::Deserialize(StreamIO* stream)
	{
		stream->Read(m_isConnected);
		if (m_isConnected)
		{
			// load gameobject
			size_t gameObjectID = stream->ReadInt();
			m_connectedGameObject = g_context->GetSubsystem<Scene>()->GetGameObjectByID(gameObjectID);
		}

		stream->Read(m_axisA);
		stream->Read(m_axisB);
		stream->Read(m_pivotA);
		stream->Read(m_pivotB);

		m_isDirty = true;
	}

	void Hinge::SetConnectedGameObject(weakGameObj connectedRigidBody)
	{
		m_connectedGameObject = connectedRigidBody;
		m_isConnected = true;

		m_isDirty = true;
	}

	weakGameObj Hinge::GetConnectedGameObject()
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
		if (m_connectedGameObject.expired())
			return;

		if (m_hinge)
		{
			g_context->GetSubsystem<Physics>()->GetWorld()->removeConstraint(m_hinge);
			delete m_hinge;
			m_hinge = nullptr;
		}

		// get the rigidbodies
		auto rigidBodyA = g_gameObject._Get()->GetComponent<RigidBody>()->GetBtRigidBody();
		auto rigidBodyB = m_connectedGameObject._Get()->GetComponent<RigidBody>()->GetBtRigidBody();

		CalculateConnections();

		// convert data to bullet data
		btTransform localA, localB;
		localA.setIdentity();
		localB.setIdentity();
		localA.getBasis().setEulerZYX(0, PI_2, 0);
		localA.setOrigin(btVector3(0.0, 1.0, 3.05));
		localB.getBasis().setEulerZYX(0, PI_2, 0);
		localB.setOrigin(btVector3(0.0, -1.5, -0.05));

		// create the hinge
		m_hinge = new btHingeConstraint(*rigidBodyA._Get(), *rigidBodyB._Get(), localA, localB);
		m_hinge->enableAngularMotor(true, 2, 3);

		// add it to the world
		g_context->GetSubsystem<Physics>()->GetWorld()->addConstraint(m_hinge);
	}

	void Hinge::CalculateConnections()
	{
		m_axisB = m_axisA; // The axis in A should be equal to to the axis in B and point away from the car off to the side.
	}

	void Hinge::ComponentCheck()
	{
		if (g_gameObject.expired())
			return;

		g_gameObject._Get()->AddComponent<RigidBody>();
	}
}