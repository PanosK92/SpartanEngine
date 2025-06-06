/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES =========================================================
#include "pch.h"
#include "PhysicsBody.h"
#include "Renderable.h"
#include "Terrain.h"
#include "../Entity.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../IO/FileStream.h"
#include "../../Physics/Physics.h"
#include "../../Rendering/Renderer.h"
#include "../../Core/ProgressTracker.h"
#include "../../Geometry/GeometryProcessing.h"
SP_WARNINGS_OFF
SP_WARNINGS_ON
//====================================================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    PhysicsBody::PhysicsBody(Entity* entity) : Component(entity)
    {
        m_gravity = Physics::GetGravity();

        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mass, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction_rolling, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_restitution, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_use_gravity, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_kinematic, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_gravity, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_position_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotation_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_center_of_mass, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_size, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_shape_type, SetShapeType, PhysicsShape);
    }

    PhysicsBody::~PhysicsBody()
    {
        OnRemove();
    }

    void PhysicsBody::OnInitialize()
    {
        Component::OnInitialize();
    }

    void PhysicsBody::OnRemove()
    {

    }

    void PhysicsBody::OnStart()
    {
        Activate();
    }

    void PhysicsBody::OnTick()
    {
        // when the rigid body is inactive or we are in editor mode, allow the user to move/rotate it
        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            if (GetPosition() != GetEntity()->GetPosition())
            {
                SetPosition(GetEntity()->GetPosition(), false);
                SetLinearVelocity(Vector3::Zero, false);
                SetAngularVelocity(Vector3::Zero, false);
            }

            if (GetRotation() != GetEntity()->GetRotation())
            {
                SetRotation(GetEntity()->GetRotation(), false);
                SetLinearVelocity(Vector3::Zero, false);
                SetAngularVelocity(Vector3::Zero, false);
            }
        }
    }

    void PhysicsBody::Serialize(FileStream* stream)
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
        stream->Write(uint32_t(m_shape_type));
        stream->Write(m_size);
        stream->Write(m_center_of_mass);
    }

    void PhysicsBody::Deserialize(FileStream* stream)
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
        m_shape_type = PhysicsShape(stream->ReadAs<uint32_t>());
        stream->Read(&m_size);
        stream->Read(&m_center_of_mass);

        AddBodyToWorld();
        UpdateShape();
    }

    void PhysicsBody::SetMass(float mass)
    {
        m_mass = max(mass, 0.0f);
    }

    void PhysicsBody::SetFriction(float friction)
    {
        if (!m_rigid_body || m_friction == friction)
            return;

        m_friction = friction;
    }

    void PhysicsBody::SetFrictionRolling(float frictionRolling)
    {
        if (!m_rigid_body || m_friction_rolling == frictionRolling)
            return;

        m_friction_rolling = frictionRolling;
    }

    void PhysicsBody::SetRestitution(float restitution)
    {
        if (!m_rigid_body || m_restitution == restitution)
            return;

        m_restitution = restitution;
    }

    void PhysicsBody::SetUseGravity(bool gravity)
    {
        if (gravity == m_use_gravity)
            return;

        m_use_gravity = gravity;
        AddBodyToWorld();
    }

    void PhysicsBody::SetGravity(const Vector3& gravity)
    {
        if (m_gravity == gravity)
            return;

        m_gravity = gravity;
        AddBodyToWorld();
    }

    void PhysicsBody::SetIsKinematic(bool kinematic)
    {
        if (kinematic == m_is_kinematic)
            return;

        m_is_kinematic = kinematic;
        AddBodyToWorld();
    }

    void PhysicsBody::SetLinearVelocity(const Vector3& velocity, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    Vector3 PhysicsBody::GetLinearVelocity() const
    {
        if (!m_rigid_body)
            return Vector3::Zero;

        return Vector3::Zero;
    }
    
	void PhysicsBody::SetAngularVelocity(const Vector3& velocity, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    void PhysicsBody::ApplyForce(const Vector3& force, PhysicsForce mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == PhysicsForce::Constant)
        {
            
        }
        else if (mode == PhysicsForce::Impulse)
        {
           
        }
    }

    void PhysicsBody::ApplyForceAtPosition(const Vector3& force, const Vector3& position, PhysicsForce mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == PhysicsForce::Constant)
        {
            
        }
        else if (mode == PhysicsForce::Impulse)
        {
           
        }
    }

    void PhysicsBody::ApplyTorque(const Vector3& torque, PhysicsForce mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == PhysicsForce::Constant)
        {
            
        }
        else if (mode == PhysicsForce::Impulse)
        {
           
        }
    }

    void PhysicsBody::SetPositionLock(bool lock)
    {
        SetPositionLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetPositionLock(const Vector3& lock)
    {
        if (!m_rigid_body || m_position_lock == lock)
            return;

        m_position_lock = lock;
    }

    void PhysicsBody::SetRotationLock(bool lock)
    {
        SetRotationLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetRotationLock(const Vector3& lock)
    {
        if (!m_rigid_body)
        {
            SP_LOG_WARNING("This call needs to happen after SetShapeType()");
        }

        if (m_rotation_lock == lock)
            return;

        m_rotation_lock = lock;
    }

    void PhysicsBody::SetCenterOfMass(const Vector3& center_of_mass)
    {
        m_center_of_mass = center_of_mass;
        SetPosition(GetPosition());
    }

    Vector3 PhysicsBody::GetPosition() const
    {
        if (m_rigid_body)
        {

        }
    
        return Vector3::Zero;
    }

    void PhysicsBody::SetPosition(const Vector3& position, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        if (activate)
        {
            Activate();
        }
    }

    Quaternion PhysicsBody::GetRotation() const
    {
        return Quaternion::Identity;
    }

    void PhysicsBody::SetRotation(const Quaternion& rotation, const bool activate /*= true*/) const
    {
        // setting rotation when loading can sometimes cause a crash, so early exit
        if (!m_rigid_body || ProgressTracker::IsLoading())
            return;

        if (activate)
        {
            Activate();
        }
    }

    void PhysicsBody::ClearForces() const
    {
        if (!m_rigid_body)
            return;
    }

    void PhysicsBody::Activate() const
    {
        if (!m_rigid_body)
            return;

        if (m_mass > 0.0f)
        {

        }
    }

    void PhysicsBody::Deactivate() const
    {
        if (!m_rigid_body)
            return;

    }

    void PhysicsBody::AddBodyToWorld()
    {
       
    }

    void PhysicsBody::RemoveBodyFromWorld()
    {
        
    }

    void PhysicsBody::SetBoundingBox(const Vector3& bounding_box)
    {
        if (m_size == bounding_box)
            return;

        m_size   = bounding_box;
        m_size.x = clamp(m_size.x, std::numeric_limits<float>::min(), std::numeric_limits<float>::infinity());
        m_size.y = clamp(m_size.y, std::numeric_limits<float>::min(), std::numeric_limits<float>::infinity());
        m_size.z = clamp(m_size.z, std::numeric_limits<float>::min(), std::numeric_limits<float>::infinity());
    }

    void PhysicsBody::SetShapeType(PhysicsShape type, const bool replicate_hierarchy)
    {
        if (m_shape_type == type)
            return;

        m_shape_type          = type;
        m_replicate_hierarchy = replicate_hierarchy;

        UpdateShape();
    }

    void PhysicsBody::SetBodyType(const PhysicsBodyType type)
    {
        if (m_body_type == type)
            return;

        m_body_type = type;
    }
    
    bool PhysicsBody::RayTraceIsGrounded() const
    {
        return false;
    }

    float PhysicsBody::GetCapsuleVolume()
	{
        // total volume is the sum of the cylinder and two hemispheres
        return 0.0f;
	}

    float PhysicsBody::GetCapsuleRadius()
    {
        return 0.0f;
    }
    
    void PhysicsBody::UpdateShape()
    {

    }
}
