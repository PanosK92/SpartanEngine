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
#include "Constraint.h"
#include "RigidBody.h"
#include "../IO/FileStream.h"
#include "../Scene/Scene.h"
#include "../Scene/GameObject.h"
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
	Constraint::Constraint()
	{
		m_constraint = nullptr;
		m_isDirty = false;
	}

	Constraint::~Constraint()
	{
		ReleaseConstraint();
	}

	void Constraint::Initialize()
	{

	}

	void Constraint::Start()
	{

	}

	void Constraint::OnDisable()
	{

	}

	void Constraint::Remove()
	{
		ReleaseConstraint();
	}

	void Constraint::Update()
	{
		if (!m_isDirty)
			return;

		ConstructConstraint();

		m_isDirty = false;
	}

	void Constraint::Serialize(FileStream* stream)
	{
		stream->Write(!m_bodyOther.expired() ? m_bodyOther.lock()->GetGameObject()->GetID() : (unsigned int)0);
	}

	void Constraint::Deserialize(FileStream* stream)
	{
		unsigned int bodyOtherID = stream->ReadUInt();
		auto otherGameObject = GetContext()->GetSubsystem<Scene>()->GetGameObjectByID(bodyOtherID);
		if (!otherGameObject.expired())
		{
			m_bodyOther = otherGameObject.lock()->GetComponent<RigidBody>();
		}

		m_isDirty = true;
	}

	/*------------------------------------------------------------------------------
								[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void Constraint::ConstructConstraint()
	{
		/*if (m_connectedRigidBody.expired())
			return;

		if (m_constraint)
		{
			g_context->GetSubsystem<Physics>()->GetWorld()->removeConstraint(m_constraint);
			delete m_constraint;
			m_constraint = nullptr;
		}

		// get the rigidbodies
		auto rigidBodyA = g_gameObject.lock()->GetComponent<RigidBody>()->GetBtRigidBody();
		auto rigidBodyB = m_connectedRigidBody.lock()->GetComponent<RigidBody>()->GetBtRigidBody();

		// convert data to bullet data
		btTransform localA, localB;
		localA.setIdentity();
		localB.setIdentity();
		localA.getBasis().setEulerZYX(0, PI_2, 0);
		localA.setOrigin(btVector3(0.0, 1.0, 3.05));
		localB.getBasis().setEulerZYX(0, PI_2, 0);
		localB.setOrigin(btVector3(0.0, -1.5, -0.05));

		// create the hinge
		m_constraint = new btHingeConstraint(*rigidBodyA, *rigidBodyB, localA, localB);
		m_constraint->enableAngularMotor(true, 2, 3);

		// add it to the world
		g_context->GetSubsystem<Physics>()->GetWorld()->addConstraint(m_constraint);
		*/
	}

	void Constraint::ReleaseConstraint()
	{
		if (!m_constraint)
			return;

		// Activate RigidBodies
		if (!m_bodyOwn.expired())
		{
			m_bodyOwn.lock()->Activate();
		}
		if (!m_bodyOther.expired())
		{
			m_bodyOther.lock()->Activate();
		}

		GetContext()->GetSubsystem<Physics>()->GetWorld()->removeConstraint(m_constraint.get());
		m_constraint.reset();
	}
}
