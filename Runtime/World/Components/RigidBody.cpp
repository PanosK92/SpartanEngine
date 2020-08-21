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
#include "RigidBody.h"
#include "Transform.h"
#include "Collider.h"
#include "Constraint.h"
#include "../Entity.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
#include "../../IO/FileStream.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    static const float DEFAULT_MASS                 = 0.0f;
    static const float DEFAULT_FRICTION             = 0.5f;
    static const float DEFAULT_FRICTION_ROLLING     = 0.0f;
    static const float DEFAULT_RESTITUTION          = 0.0f;    
    static const float DEFAULT_DEACTIVATION_TIME    = 2000;

    class MotionState : public btMotionState
    {
    public:
        MotionState(RigidBody* rigidBody) { m_rigidBody = rigidBody; }

        // Update from engine, ENGINE -> BULLET
        void getWorldTransform(btTransform& worldTrans) const override
        {
            const Vector3 lastPos        = m_rigidBody->GetTransform()->GetPosition();
            const Quaternion lastRot    = m_rigidBody->GetTransform()->GetRotation();

            worldTrans.setOrigin(ToBtVector3(lastPos + lastRot * m_rigidBody->GetCenterOfMass()));
            worldTrans.setRotation(ToBtQuaternion(lastRot));
        }

        // Update from bullet, BULLET -> ENGINE
        void setWorldTransform(const btTransform& worldTrans) override
        {
            const Quaternion newWorldRot    = ToQuaternion(worldTrans.getRotation());
            const Vector3 newWorldPos        = ToVector3(worldTrans.getOrigin()) - newWorldRot * m_rigidBody->GetCenterOfMass();

            m_rigidBody->GetTransform()->SetPosition(newWorldPos);
            m_rigidBody->GetTransform()->SetRotation(newWorldRot);
        }
    private:
        RigidBody* m_rigidBody;
    };

    RigidBody::RigidBody(Context* context, Entity* entity, uint32_t id /*= 0*/) : IComponent(context, entity, id)
    {
        m_physics = GetContext()->GetSubsystem<Physics>();

        m_in_world            = false;
        m_mass                = DEFAULT_MASS;
        m_restitution        = DEFAULT_RESTITUTION;
        m_friction            = DEFAULT_FRICTION;
        m_friction_rolling    = DEFAULT_FRICTION_ROLLING;
        m_use_gravity        = true;
        m_gravity           = m_physics->GetGravity();
        m_is_kinematic        = false;
        m_position_lock        = Vector3::Zero;
        m_rotation_lock        = Vector3::Zero;
        m_collision_shape    = nullptr;
        m_rigidBody            = nullptr;

        REGISTER_ATTRIBUTE_VALUE_VALUE(m_mass, float);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction, float);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction_rolling, float);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_restitution, float);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_use_gravity, bool);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_kinematic, bool);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_gravity, Vector3);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_position_lock, Vector3);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotation_lock, Vector3);
        REGISTER_ATTRIBUTE_VALUE_VALUE(m_center_of_mass, Vector3);    
    }

    RigidBody::~RigidBody()
    {
        Body_Release();
    }

    void RigidBody::OnInitialize()
    {
        Body_AcquireShape();
        Body_AddToWorld();
    }

    void RigidBody::OnRemove()
    {
        Body_Release();
    }

    void RigidBody::OnStart()
    {
        Activate();
    }

    void RigidBody::OnTick(float delta_time)
    {
        // When the rigid body is inactive or we are in editor mode, allow the user to move/rotate it
        if (!IsActivated() || !m_context->m_engine->EngineMode_IsSet(Engine_Game))
        {
            if (GetPosition() != GetTransform()->GetPosition())
            {
                SetPosition(GetTransform()->GetPosition(), false);
                SetLinearVelocity(Vector3::Zero, false);
                SetAngularVelocity(Vector3::Zero, false);
            }

            if (GetRotation() != GetTransform()->GetRotation())
            {
                SetRotation(GetTransform()->GetRotation(), false);
                SetLinearVelocity(Vector3::Zero, false);
                SetAngularVelocity(Vector3::Zero, false);
            }
        }
    }

    void RigidBody::Serialize(FileStream* stream)
    {
        stream->Write(m_mass);
        stream->Write(m_friction);
        stream->Write(m_friction_rolling);
        stream->Write(m_restitution);
        stream->Write(m_use_gravity);
        stream->Write(m_gravity);
        stream->Write(m_is_kinematic);
        stream->Write(m_position_lock);
        stream->Write(m_rotation_lock);
        stream->Write(m_in_world);
    }

    void RigidBody::Deserialize(FileStream* stream)
    {
        stream->Read(&m_mass);
        stream->Read(&m_friction);
        stream->Read(&m_friction_rolling);
        stream->Read(&m_restitution);
        stream->Read(&m_use_gravity);
        stream->Read(&m_gravity);
        stream->Read(&m_is_kinematic);
        stream->Read(&m_position_lock);
        stream->Read(&m_rotation_lock);
        stream->Read(&m_in_world);

        Body_AcquireShape();
        Body_AddToWorld();
    }

    void RigidBody::SetMass(float mass)
    {
        mass = Helper::Max(mass, 0.0f);
        if (mass != m_mass)
        {
            m_mass = mass;
            Body_AddToWorld();
        }
    }

    void RigidBody::SetFriction(float friction)
    {
        if (!m_rigidBody || m_friction == friction)
            return;

        m_friction = friction;
        m_rigidBody->setFriction(friction);
    }

    void RigidBody::SetFrictionRolling(float frictionRolling)
    {
        if (!m_rigidBody || m_friction_rolling == frictionRolling)
            return;

        m_friction_rolling = frictionRolling;
        m_rigidBody->setRollingFriction(frictionRolling);
    }

    void RigidBody::SetRestitution(float restitution)
    {
        if (!m_rigidBody || m_restitution == restitution)
            return;

        m_restitution = restitution;
        m_rigidBody->setRestitution(restitution);
    }

    void RigidBody::SetUseGravity(bool gravity)
    {
        if (gravity == m_use_gravity)
            return;

        m_use_gravity = gravity;
        Body_AddToWorld();
    }

    void RigidBody::SetGravity(const Vector3& acceleration)
    {
        if (m_gravity == acceleration)
            return;

        m_gravity = acceleration;
        Body_AddToWorld();
    }

    void RigidBody::SetIsKinematic(bool kinematic)
    {
        if (kinematic == m_is_kinematic)
            return;

        m_is_kinematic = kinematic;
        Body_AddToWorld();
    }

    void RigidBody::SetLinearVelocity(const Vector3& velocity, const bool activate /*= true*/) const
    {
        if (!m_rigidBody)
            return;

        m_rigidBody->setLinearVelocity(ToBtVector3(velocity));
        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    void RigidBody::SetAngularVelocity(const Vector3& velocity, const bool activate /*= true*/) const
    {
        if (!m_rigidBody)
            return;

        m_rigidBody->setAngularVelocity(ToBtVector3(velocity));
        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    void RigidBody::ApplyForce(const Vector3& force, ForceMode mode) const
    {
        if (!m_rigidBody)
            return;

        Activate();

        if (mode == Force)
        {
            m_rigidBody->applyCentralForce(ToBtVector3(force));
        }
        else if (mode == Impulse)
        {
            m_rigidBody->applyCentralImpulse(ToBtVector3(force));
        }
    }

    void RigidBody::ApplyForceAtPosition(const Vector3& force, const Vector3& position, ForceMode mode) const
    {
        if (!m_rigidBody)
            return;

        Activate();

        if (mode == Force)
        {
            m_rigidBody->applyForce(ToBtVector3(force), ToBtVector3(position));
        }
        else if (mode == Impulse)
        {
            m_rigidBody->applyImpulse(ToBtVector3(force), ToBtVector3(position));
        }
    }

    void RigidBody::ApplyTorque(const Vector3& torque, ForceMode mode) const
    {
        if (!m_rigidBody)
            return;

        Activate();

        if (mode == Force)
        {
            m_rigidBody->applyTorque(ToBtVector3(torque));
        }
        else if (mode == Impulse)
        {
            m_rigidBody->applyTorqueImpulse(ToBtVector3(torque));
        }
    }

    void RigidBody::SetPositionLock(bool lock)
    {
        if (lock)
        {
            SetPositionLock(Vector3::One);
        }
        else
        {
            SetPositionLock(Vector3::Zero);
        }
    }

    void RigidBody::SetPositionLock(const Vector3& lock)
    {
        if (!m_rigidBody || m_position_lock == lock)
            return;

        m_position_lock = lock;
        m_rigidBody->setLinearFactor(ToBtVector3(Vector3::One - lock));
    }

    void RigidBody::SetRotationLock(bool lock)
    {
        if (lock)
        {
            SetRotationLock(Vector3::One);
        }
        else
        {
            SetRotationLock(Vector3::Zero);
        }
    }

    void RigidBody::SetRotationLock(const Vector3& lock)
    {
        if (!m_rigidBody || m_rotation_lock == lock)
            return;

        m_rotation_lock = lock;
        m_rigidBody->setAngularFactor(ToBtVector3(Vector3::One - lock));
    }

    void RigidBody::SetCenterOfMass(const Vector3& centerOfMass)
    {
        m_center_of_mass = centerOfMass;
        SetPosition(GetPosition());
    }

    Vector3 RigidBody::GetPosition() const
    {
        if (m_rigidBody)
        {
            const btTransform& transform = m_rigidBody->getWorldTransform();
            return ToVector3(transform.getOrigin()) - ToQuaternion(transform.getRotation()) * m_center_of_mass;
        }
    
        return Vector3::Zero;
    }

    void RigidBody::SetPosition(const Vector3& position, const bool activate /*= true*/) const
    {
        if (!m_rigidBody)
            return;

        // Set position to world transform
        btTransform& transform_world = m_rigidBody->getWorldTransform();
        transform_world.setOrigin(ToBtVector3(position + ToQuaternion(transform_world.getRotation()) * m_center_of_mass));

        // Set position to interpolated world transform
        btTransform transform_world_interpolated = m_rigidBody->getInterpolationWorldTransform();
        transform_world_interpolated.setOrigin(transform_world.getOrigin());
        m_rigidBody->setInterpolationWorldTransform(transform_world_interpolated);

        if (activate)
        {
            Activate();
        }
    }

    Quaternion RigidBody::GetRotation() const
    {
        return m_rigidBody ? ToQuaternion(m_rigidBody->getWorldTransform().getRotation()) : Quaternion::Identity;
    }

    void RigidBody::SetRotation(const Quaternion& rotation, const bool activate /*= true*/) const
    {
        if (!m_rigidBody)
            return;

        // Set rotation to world transform
        const Vector3 oldPosition = GetPosition();
        btTransform& transform_world = m_rigidBody->getWorldTransform();
        transform_world.setRotation(ToBtQuaternion(rotation));
        if (m_center_of_mass != Vector3::Zero)
        {
            transform_world.setOrigin(ToBtVector3(oldPosition + rotation * m_center_of_mass));
        }

        // Set rotation to interpolated world transform
        btTransform interpTrans = m_rigidBody->getInterpolationWorldTransform();
        interpTrans.setRotation(transform_world.getRotation());
        if (m_center_of_mass != Vector3::Zero)
        {
            interpTrans.setOrigin(transform_world.getOrigin());
        }
        m_rigidBody->setInterpolationWorldTransform(interpTrans);

        m_rigidBody->updateInertiaTensor();

        if (activate)
        {
            Activate();
        }
    }

    void RigidBody::ClearForces() const
    {
        if (!m_rigidBody)
            return;

        m_rigidBody->clearForces();
    }

    void RigidBody::Activate() const
    {
        if (!m_rigidBody)
            return;

        if (m_mass > 0.0f)
        {
            m_rigidBody->activate(true);
        }
    }

    void RigidBody::Deactivate() const
    {
        if (!m_rigidBody)
            return;

        m_rigidBody->setActivationState(WANTS_DEACTIVATION);
    }

    void RigidBody::AddConstraint(Constraint* constraint)
    {
        m_constraints.emplace_back(constraint);
    }

    void RigidBody::RemoveConstraint(Constraint* constraint)
    {
        for (auto it = m_constraints.begin(); it != m_constraints.end(); )
        {
            const auto itConstraint = *it;
            if (constraint->GetId() == itConstraint->GetId())
            {
                it = m_constraints.erase(it);
            }
            else
            {
                ++it;
            }
        }

        Activate();
    }

    void RigidBody::SetShape(btCollisionShape* shape)
    {
        m_collision_shape = shape;

        if (m_collision_shape)
        {
            Body_AddToWorld();
        }
        else
        {
            Body_RemoveFromWorld();
        }
    }

    void RigidBody::Body_AddToWorld()
    {
        if (m_mass < 0.0f)
        {
            m_mass = 0.0f;
        }

        // Transfer inertia to new collision shape
        btVector3 local_intertia = btVector3(0, 0, 0);
        if (m_collision_shape && m_rigidBody)
        {
            local_intertia = m_rigidBody ? m_rigidBody->getLocalInertia() : local_intertia;
            m_collision_shape->calculateLocalInertia(m_mass, local_intertia);
        }
        
        Body_Release();

        // CONSTRUCTION
        {
            // Create a motion state (memory will be freed by the RigidBody)
            const auto motion_state = new MotionState(this);
            
            // Info
            btRigidBody::btRigidBodyConstructionInfo constructionInfo(m_mass, motion_state, m_collision_shape, local_intertia);
            constructionInfo.m_mass                = m_mass;
            constructionInfo.m_friction            = m_friction;
            constructionInfo.m_rollingFriction    = m_friction_rolling;
            constructionInfo.m_restitution        = m_restitution;
            constructionInfo.m_collisionShape    = m_collision_shape;
            constructionInfo.m_localInertia        = local_intertia;
            constructionInfo.m_motionState        = motion_state;

            m_rigidBody = new btRigidBody(constructionInfo);
            m_rigidBody->setUserPointer(this);
        }

        // Reapply constraint positions for new center of mass shift
        for (const auto& constraint : m_constraints)
        {
            constraint->ApplyFrames();
        }
        
        Flags_UpdateKinematic();
        Flags_UpdateGravity();

        // Transform
        SetPosition(GetTransform()->GetPosition());
        SetRotation(GetTransform()->GetRotation());

        // Constraints
        SetPositionLock(m_position_lock);
        SetRotationLock(m_rotation_lock);

        // Position and rotation locks
        SetPositionLock(m_position_lock);
        SetRotationLock(m_rotation_lock);

        // Add to world
        m_physics->AddBody(m_rigidBody);

        if (m_mass > 0.0f)
        {
            Activate();
        }
        else
        {
            SetLinearVelocity(Vector3::Zero);
            SetAngularVelocity(Vector3::Zero);
        }

        m_in_world = true;
    }

    void RigidBody::Body_Release()
    {
        if (!m_rigidBody)
            return;

        // Release any constraints that refer to it
        for (const auto& constraint : m_constraints)
        {
            constraint->ReleaseConstraint();
        }

        // Remove it from the world
        Body_RemoveFromWorld();

        // Reset it
        m_rigidBody = nullptr;
    }

    void RigidBody::Body_RemoveFromWorld()
    {
        if (!m_rigidBody)
            return;

        if (m_in_world)
        {
            m_physics->RemoveBody(m_rigidBody);
            m_in_world = false;
        }
    }

    void RigidBody::Body_AcquireShape()
    {
        if (const auto& collider = m_entity->GetComponent<Collider>())
        {
            m_collision_shape    = collider->GetShape();
            m_center_of_mass    = collider->GetCenter();
        }
    }

    void RigidBody::Flags_UpdateKinematic() const
    {
        int flags = m_rigidBody->getCollisionFlags();

        if (m_is_kinematic)
        {
            flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
        }
        else
        {
            flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
        }

        m_rigidBody->setCollisionFlags(flags);
        m_rigidBody->forceActivationState(m_is_kinematic ? DISABLE_DEACTIVATION : ISLAND_SLEEPING);
        m_rigidBody->setDeactivationTime(DEFAULT_DEACTIVATION_TIME);
    }

    void RigidBody::Flags_UpdateGravity() const
    {
        int flags = m_rigidBody->getFlags();

        if (m_use_gravity)
        {
            flags &= ~BT_DISABLE_WORLD_GRAVITY;
        }
        else
        {
            flags |= BT_DISABLE_WORLD_GRAVITY;
        }

        m_rigidBody->setFlags(flags);

        if (m_use_gravity)
        {
            m_rigidBody->setGravity(ToBtVector3(m_gravity));
        }
        else
        {
            m_rigidBody->setGravity(btVector3(0.0f, 0.0f, 0.0f));
        }
    }

    bool RigidBody::IsActivated() const
    {
        return m_rigidBody->isActive();
    }
}
