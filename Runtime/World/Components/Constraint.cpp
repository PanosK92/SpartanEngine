/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES =================================
#include "Spartan.h"
#include "Constraint.h"
#include "RigidBody.h"
#include "Transform.h"
#include "../Entity.h"
#include "../World.h"
#include "../../IO/FileStream.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Constraint::Constraint(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {
        m_constraint                = nullptr;
        m_enabledEffective            = true;
        m_collisionWithLinkedBody    = false;
        m_errorReduction            = 0.0f;
        m_constraintForceMixing        = 0.0f;
        m_constraintType            = ConstraintType_Point;
        m_physics                    = GetContext()->GetSubsystem<Physics>();

        REGISTER_ATTRIBUTE_VALUE_VALUE(m_errorReduction, float);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_constraintForceMixing, float);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_enabledEffective, bool);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_collisionWithLinkedBody, bool);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_position, Vector3);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotation, Quaternion);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_highLimit, Vector2);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_lowLimit, Vector2);
        REGISTER_ATTRIBUTE_VALUE_SET(m_constraintType, SetConstraintType, ConstraintType);
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

    void Constraint::OnTick(float delta_time)
    {
        if (m_deferredConstruction)
        {
            Construct();
        }
    }

    void Constraint::Serialize(FileStream* stream)
    {
        stream->Write(static_cast<uint32_t>(m_constraintType));
        stream->Write(m_position);
        stream->Write(m_rotation);
        stream->Write(m_highLimit);
        stream->Write(m_lowLimit);
        stream->Write(!m_bodyOther.expired() ? m_bodyOther.lock()->GetId() : static_cast<uint32_t>(0));
    }

    void Constraint::Deserialize(FileStream* stream)
    {
        uint32_t constraint_type = 0;
        stream->Read(&constraint_type);
        m_constraintType = static_cast<ConstraintType>(constraint_type);
        stream->Read(&m_position);
        stream->Read(&m_rotation);
        stream->Read(&m_highLimit);
        stream->Read(&m_lowLimit);

        const auto body_other_id = stream->ReadAs<uint32_t>();
        m_bodyOther = GetContext()->GetSubsystem<World>()->EntityGetById(body_other_id);

        Construct();
    }

    void Constraint::SetConstraintType(const ConstraintType type)
    {
        if (m_constraintType != type || !m_constraint)
        {
            m_constraintType = type;
            Construct();
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

    void Constraint::SetPositionOther(const Vector3& position)
    {
        if (position != m_positionOther)
        {
            m_positionOther = position;
            ApplyFrames();
        }
    }

    void Constraint::SetRotationOther(const Quaternion& rotation)
    {
        if (rotation != m_rotationOther)
        {
            m_rotationOther = rotation;
            ApplyFrames();
        }
    }

    void Constraint::SetBodyOther(const std::weak_ptr<Entity>& body_other)
    {
        if (body_other.expired())
            return;

        if (!body_other.expired() && body_other.lock()->GetId() == m_entity->GetId())
        {
            LOG_WARNING("You can't connect a body to itself.");
            return;
        }

        m_bodyOther = body_other;
        Construct();
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

    void Constraint::ReleaseConstraint()
    {
        if (m_constraint)
        {
            RigidBody* rigid_body_own    = m_entity->GetComponent<RigidBody>();
            RigidBody* rigid_body_other    = !m_bodyOther.expired() ? m_bodyOther.lock()->GetComponent<RigidBody>() : nullptr;

            // Make both bodies aware of the removal of this constraint
            if (rigid_body_own)    rigid_body_own->RemoveConstraint(this);
            if (rigid_body_other) rigid_body_other->RemoveConstraint(this);

            m_physics->RemoveConstraint(m_constraint);
        }
    }

    void Constraint::ApplyFrames() const
    {
        if (!m_constraint || m_bodyOther.expired())
            return;

        RigidBody* rigid_body_own   = m_entity->GetComponent<RigidBody>();
        RigidBody* rigid_body_other = !m_bodyOther.expired() ? m_bodyOther.lock()->GetComponent<RigidBody>() : nullptr;
        btRigidBody* bt_own_body    = rigid_body_own ? rigid_body_own->GetBtRigidBody() : nullptr;
        btRigidBody* bt_other_body  = rigid_body_other ? rigid_body_other->GetBtRigidBody() : nullptr;

        Vector3 own_body_scaled_position    = m_position * m_transform->GetScale() - rigid_body_own->GetCenterOfMass();
        Vector3 other_body_scaled_position  = !m_bodyOther.expired() ? m_positionOther * rigid_body_other->GetTransform()->GetScale() - rigid_body_other->GetCenterOfMass() : m_positionOther;

        switch (m_constraint->getConstraintType())
        {
        case POINT2POINT_CONSTRAINT_TYPE:
        {
            auto* point_constraint = dynamic_cast<btPoint2PointConstraint*>(m_constraint);
            point_constraint->setPivotA(ToBtVector3(own_body_scaled_position));
            point_constraint->setPivotB(ToBtVector3(other_body_scaled_position));
        }
        break;

        case HINGE_CONSTRAINT_TYPE:
        {
            auto* hinge_constraint = dynamic_cast<btHingeConstraint*>(m_constraint);
            btTransform own_frame(ToBtQuaternion(m_rotation), ToBtVector3(own_body_scaled_position));
            btTransform other_frame(ToBtQuaternion(m_rotationOther), ToBtVector3(other_body_scaled_position));
            hinge_constraint->setFrames(own_frame, other_frame);
        }
        break;

        case SLIDER_CONSTRAINT_TYPE:
        {
            auto* slider_constraint = dynamic_cast<btSliderConstraint*>(m_constraint);
            btTransform own_frame(ToBtQuaternion(m_rotation), ToBtVector3(own_body_scaled_position));
            btTransform other_frame(ToBtQuaternion(m_rotationOther), ToBtVector3(other_body_scaled_position));
            slider_constraint->setFrames(own_frame, other_frame);
        }
        break;

        case CONETWIST_CONSTRAINT_TYPE:
        {
            auto* cone_twist_constraint = dynamic_cast<btConeTwistConstraint*>(m_constraint);
            btTransform own_frame(ToBtQuaternion(m_rotation), ToBtVector3(own_body_scaled_position));
            btTransform other_frame(ToBtQuaternion(m_rotationOther), ToBtVector3(other_body_scaled_position));
            cone_twist_constraint->setFrames(own_frame, other_frame);
        }
        break;

        default:
            break;
        }
    }

    /*------------------------------------------------------------------------------
                                [HELPER FUNCTIONS]
    ------------------------------------------------------------------------------*/
    void Constraint::Construct()
    {
        ReleaseConstraint();

        // Make sure we have two bodies
        RigidBody* rigid_body_own    = m_entity->GetComponent<RigidBody>();
        RigidBody* rigid_body_other    = !m_bodyOther.expired() ? m_bodyOther.lock()->GetComponent<RigidBody>() : nullptr;
        if (!rigid_body_own || !rigid_body_other)
        {
            LOG_INFO("A RigidBody component is still initializing, deferring construction...");
            m_deferredConstruction = true;
            return;
        }
        else if (m_deferredConstruction)
        {
            LOG_INFO("Deferred construction has succeeded");
            m_deferredConstruction = false;
        }

        btRigidBody* bt_own_body    = rigid_body_own ? rigid_body_own->GetBtRigidBody() : nullptr;
        btRigidBody* bt_other_body    = rigid_body_other ? rigid_body_other->GetBtRigidBody() : nullptr;

        if (!bt_own_body)
            return;

        if (!bt_other_body)
        {
            bt_other_body = &btTypedConstraint::getFixedBody();
        }    
        
        Vector3 own_body_scaled_position    = m_position * m_transform->GetScale() - rigid_body_own->GetCenterOfMass();
        Vector3 other_body_scaled_position    = rigid_body_other ? m_positionOther * rigid_body_other->GetTransform()->GetScale() - rigid_body_other->GetCenterOfMass() : m_positionOther;

        switch (m_constraintType)
        {
            case ConstraintType_Point:
                {
                    m_constraint = new btPoint2PointConstraint(*bt_own_body, *bt_other_body, ToBtVector3(own_body_scaled_position), ToBtVector3(other_body_scaled_position));
                }
                break;

            case ConstraintType_Hinge:
                {
                    btTransform own_frame(ToBtQuaternion(m_rotation), ToBtVector3(own_body_scaled_position));
                    btTransform other_frame(ToBtQuaternion(m_rotationOther), ToBtVector3(other_body_scaled_position));
                    m_constraint = new btHingeConstraint(*bt_own_body, *bt_other_body, own_frame, other_frame);
                }
                break;

            case ConstraintType_Slider:
                {
                    btTransform own_frame(ToBtQuaternion(m_rotation), ToBtVector3(own_body_scaled_position));
                    btTransform other_frame(ToBtQuaternion(m_rotationOther), ToBtVector3(other_body_scaled_position));
                    m_constraint = new btSliderConstraint(*bt_own_body, *bt_other_body, own_frame, other_frame, false);
                }
                break;

            case ConstraintType_ConeTwist:
                {
                    btTransform own_frame(ToBtQuaternion(m_rotation), ToBtVector3(own_body_scaled_position));
                    btTransform other_frame(ToBtQuaternion(m_rotationOther), ToBtVector3(other_body_scaled_position));
                    m_constraint = new btConeTwistConstraint(*bt_own_body, *bt_other_body, own_frame, other_frame);
                }
                break;

            default:
                break;
        }

        if (m_constraint)
        {
            m_constraint->setUserConstraintPtr(this);
            m_constraint->setEnabled(m_enabledEffective);

            // Make both bodies aware of this constraint
            rigid_body_own->AddConstraint(this);
            if (rigid_body_other)
            {
                rigid_body_other->AddConstraint(this);
            }

            ApplyLimits();
            m_physics->AddConstraint(m_constraint, m_collisionWithLinkedBody);
        }
    }

    void Constraint::ApplyLimits() const
    {
        if (!m_constraint)
            return;

        switch (m_constraint->getConstraintType())
        {
            case HINGE_CONSTRAINT_TYPE:
                {
                    auto* hinge_constraint = dynamic_cast<btHingeConstraint*>(m_constraint);
                    hinge_constraint->setLimit(m_lowLimit.x * Helper::DEG_TO_RAD, m_highLimit.x * Helper::DEG_TO_RAD);
                }
                break;

            case SLIDER_CONSTRAINT_TYPE:
                {
                    auto* slider_constraint = dynamic_cast<btSliderConstraint*>(m_constraint);
                    slider_constraint->setUpperLinLimit(m_highLimit.x);
                    slider_constraint->setUpperAngLimit(m_highLimit.y * Helper::DEG_TO_RAD);
                    slider_constraint->setLowerLinLimit(m_lowLimit.x);
                    slider_constraint->setLowerAngLimit(m_lowLimit.y * Helper::DEG_TO_RAD);
                }
                break;

            case CONETWIST_CONSTRAINT_TYPE:
                {
                    auto* cone_twist_constraint = dynamic_cast<btConeTwistConstraint*>(m_constraint);
                    cone_twist_constraint->setLimit(m_highLimit.y * Helper::DEG_TO_RAD, m_highLimit.y * Helper::DEG_TO_RAD, m_highLimit.x * Helper::DEG_TO_RAD);
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
}
