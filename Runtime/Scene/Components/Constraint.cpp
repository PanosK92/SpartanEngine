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

//= INCLUDES =======================================================
#include "Constraint.h"
#include "RigidBody.h"
#include "Transform.h"
#include "../GameObject.h"
#include "../Scene.h"
#include "../../IO/FileStream.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
#pragma warning(push, 0) // Hide warnings which belong to Bullet
#include <BulletDynamics/Dynamics/btDiscreteDynamicsWorld.h>
#include "BulletDynamics/ConstraintSolver/btHingeConstraint.h"
#include "BulletDynamics/ConstraintSolver/btSliderConstraint.h"
#include "BulletDynamics/ConstraintSolver/btConeTwistConstraint.h"
#include "BulletDynamics/ConstraintSolver/btPoint2PointConstraint.h"
#pragma warning(pop)
//==================================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Constraint::Constraint(Context* context, GameObject* gameObject, Transform* transform) : IComponent(context, gameObject, transform)
	{
		m_constraint				= nullptr;
		m_isDirty					= false;
		m_enabledEffective			= true;
		m_collisionWithLinkedBody	= false;
		m_errorReduction			= 0.0f;
		m_constraintForceMixing		= 0.0f;
		m_type						= ConstraintType_Point;
	}

	Constraint::~Constraint()
	{
		ReleaseConstraint();
	}

	void Constraint::OnInitialize()
	{

	}

	void Constraint::OnStart()
	{

	}

	void Constraint::OnStop()
	{

	}

	void Constraint::OnRemove()
	{
		ReleaseConstraint();
	}

	void Constraint::OnUpdate()
	{
		if (!m_isDirty)
			return;

		ConstructConstraint();

		m_isDirty = false;
	}

	void Constraint::Serialize(FileStream* stream)
	{
		stream->Write(!m_bodyOther.expired() ? m_bodyOther.lock()->GetGameObject_PtrRaw()->GetID() : (unsigned int)0);
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

	void Constraint::SetConstraintType(ConstraintType type)
	{
		if (m_type != type || !m_constraint)
		{
			m_type = type;
			ConstructConstraint();
		}
	}

	void Constraint::SetPosition(const Vector3& position)
	{
		if (m_position != position)
		{
			m_position = position;
			ApplyFrames();
		}
	}

	void Constraint::SetRotation(const Quaternion& rotation)
	{
		if (m_rotation != rotation)
		{
			m_rotation = rotation;
			ApplyFrames();
		}
	}

	void Constraint::SetPositionOther(const Math::Vector3& position)
	{
		if (position != m_positionOther)
		{
			m_positionOther = position;
			ApplyFrames();
		}
	}

	void Constraint::SetRotationOther(const Math::Quaternion& rotation)
	{
		if (rotation != m_rotationOther)
		{
			m_rotationOther = rotation;
			ApplyFrames();
		}
	}

	void Constraint::SetHighLimit(const Vector2& limit)
	{
		if (m_highLimit != limit)
		{
			m_highLimit = limit;
			ApplyLimits();
		}
	}

	void Constraint::SetLowLimit(const Vector2& limit)
	{
		if (m_lowLimit != limit)
		{
			m_lowLimit = limit;
			ApplyLimits();
		}
	}

	/*------------------------------------------------------------------------------
								[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void Constraint::ConstructConstraint()
	{
		ReleaseConstraint();

		m_bodyOwn = m_gameObject->GetComponent<RigidBody>();
		btRigidBody* btOwnBody		= !m_bodyOwn.expired() ? m_bodyOwn.lock()->GetBtRigidBody() : nullptr;
		btRigidBody* btOtherBody	= !m_bodyOther.expired() ? m_bodyOther.lock()->GetBtRigidBody() : nullptr;

		if (!btOwnBody)
		    return;

		if (!btOtherBody)
		{
		    btOtherBody = &btTypedConstraint::getFixedBody();
		}	
		
		RigidBody* otherBody =  m_bodyOther.lock().get();

		Vector3 ownBodyScaledPosition	= m_position * m_transform->GetScale() - m_bodyOwn.lock()->GetColliderCenter();
		Vector3 otherBodyScaledPosition = otherBody ? m_positionOther * otherBody->GetTransform()->GetScale() - m_bodyOther.lock()->GetColliderCenter() : m_positionOther;

		switch (m_type)
		{
			case ConstraintType_Point:
			    {
			        m_constraint = make_unique<btPoint2PointConstraint>(*btOwnBody, *btOtherBody, ToBtVector3(ownBodyScaledPosition), ToBtVector3(otherBodyScaledPosition));
			    }
			    break;

			case ConstraintType_Hinge:
			    {
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        m_constraint = make_unique<btHingeConstraint>(*btOwnBody, *btOtherBody, ownFrame, otherFrame);
			    }
			    break;

			case ConstraintType_Slider:
			    {
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        m_constraint = make_unique<btSliderConstraint>(*btOwnBody, *btOtherBody, ownFrame, otherFrame, false);
			    }
			    break;

			case ConstraintType_ConeTwist:
			    {
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        m_constraint = make_unique<btConeTwistConstraint>(*btOwnBody, *btOtherBody, ownFrame, otherFrame);
			    }
			    break;

			default:
			    break;
		}

		if (m_constraint)
		{
		    m_constraint->setUserConstraintPtr(this);
		    m_constraint->setEnabled(m_enabledEffective);
		    m_bodyOwn.lock()->AddConstraint(this);
		    if (!m_bodyOther.expired())
			{
		        m_bodyOther.lock()->AddConstraint(this);
			}

		    ApplyLimits();

		    GetContext()->GetSubsystem<Physics>()->GetWorld()->addConstraint(m_constraint.get(), !m_collisionWithLinkedBody);
		}
	}

	void Constraint::ApplyLimits()
	{
		if (!m_constraint)
			return;

		switch (m_constraint->getConstraintType())
		{
			case HINGE_CONSTRAINT_TYPE:
			    {
			        auto* hingeConstraint = static_cast<btHingeConstraint*>(m_constraint.get());
			        hingeConstraint->setLimit(m_lowLimit.x * DEG_TO_RAD, m_highLimit.x * DEG_TO_RAD);
			    }
			    break;

			case SLIDER_CONSTRAINT_TYPE:
			    {
			        auto* sliderConstraint = static_cast<btSliderConstraint*>(m_constraint.get());
			        sliderConstraint->setUpperLinLimit(m_highLimit.x);
			        sliderConstraint->setUpperAngLimit(m_highLimit.y * DEG_TO_RAD);
			        sliderConstraint->setLowerLinLimit(m_lowLimit.x);
			        sliderConstraint->setLowerAngLimit(m_lowLimit.y * DEG_TO_RAD);
			    }
			    break;

			case CONETWIST_CONSTRAINT_TYPE:
			    {
			        auto* coneTwistConstraint = static_cast<btConeTwistConstraint*>(m_constraint.get());
			        coneTwistConstraint->setLimit(m_highLimit.y * DEG_TO_RAD, m_highLimit.y * DEG_TO_RAD, m_highLimit.x * DEG_TO_RAD);
			    }
			    break;

			default:
			    break;
		}

		if (m_errorReduction != 0.0f)
		{
		    m_constraint->setParam(BT_CONSTRAINT_STOP_ERP, m_errorReduction);
		}

		if (m_constraintForceMixing != 0.0f)
		{
			m_constraint->setParam(BT_CONSTRAINT_STOP_CFM, m_constraintForceMixing);
		}
	}

	void Constraint::ApplyFrames()
	{
		if (!m_constraint || m_bodyOther.expired())
			return;

		RigidBody* bodyOther = m_bodyOther.lock().get();

		Vector3 ownBodyScaledPosition = m_position * m_transform->GetScale() - m_bodyOwn.lock()->GetColliderCenter();
		Vector3 otherBodyScaledPosition = !m_bodyOther.expired() ? m_positionOther * bodyOther->GetTransform()->GetScale() - bodyOther->GetColliderCenter() : m_positionOther;

		switch (m_constraint->getConstraintType())
		{
			case POINT2POINT_CONSTRAINT_TYPE:
			    {
			        auto* pointConstraint = static_cast<btPoint2PointConstraint*>(m_constraint.get());
			        pointConstraint->setPivotA(ToBtVector3(ownBodyScaledPosition));
			        pointConstraint->setPivotB(ToBtVector3(otherBodyScaledPosition));
			    }
			    break;

			case HINGE_CONSTRAINT_TYPE:
			    {
			        auto* hingeConstraint = static_cast<btHingeConstraint*>(m_constraint.get());
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        hingeConstraint->setFrames(ownFrame, otherFrame);
			    }
			    break;

			case SLIDER_CONSTRAINT_TYPE:
			    {
			        auto* sliderConstraint = static_cast<btSliderConstraint*>(m_constraint.get());
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        sliderConstraint->setFrames(ownFrame, otherFrame);
			    }
			    break;

			case CONETWIST_CONSTRAINT_TYPE:
			    {
			        auto* coneTwistConstraint = static_cast<btConeTwistConstraint*>(m_constraint.get());
			        btTransform ownFrame(ToBtQuaternion(m_rotation), ToBtVector3(ownBodyScaledPosition));
			        btTransform otherFrame(ToBtQuaternion(m_rotationOther), ToBtVector3(otherBodyScaledPosition));
			        coneTwistConstraint->setFrames(ownFrame, otherFrame);
			    }
			    break;

			default:
			    break;
		}
	}

	void Constraint::ReleaseConstraint()
	{
		if (m_constraint)
		{
			if (!m_bodyOwn.expired())
			{
				m_bodyOwn.lock()->RemoveConstraint(this);
			}

			if (!m_bodyOther.expired())
			{
				m_bodyOther.lock()->RemoveConstraint(this);
			}

			GetContext()->GetSubsystem<Physics>()->GetWorld()->removeConstraint(m_constraint.get());
			m_constraint.reset();
		}	
	}
}
