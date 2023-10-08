/*
Copyright(c) 2016-2023 Panos Karabelas

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

#pragma once

//= INCLUDES ==================
#include "Component.h"
#include <vector>
#include "../../Math/Vector3.h"
//=============================

namespace Spartan
{
    class Entity;
    class Constraint;
    class Physics;
    namespace Math { class Quaternion; }

    enum class PhysicsBodyType
    {
        RigidBody,
        Vehicle,
    };

    enum class PhysicsForce
    {
        Constant,
        Impulse
    };

    enum class PhysicsShape
    {
        Box,
        Sphere,
        StaticPlane,
        Cylinder,
        Capsule,
        Cone,
        Terrain,
        MeshConvexHull,
        Mesh
    };

    class SP_CLASS PhysicsBody : public Component
    {
    public:
        PhysicsBody(std::weak_ptr<Entity> entity);
        ~PhysicsBody();

        //= ICOMPONENT ===============================
        void OnInitialize() override;
        void OnRemove() override;
        void OnStart() override;
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        // Mass
        float GetMass() const { return m_mass; }
        void SetMass(float mass);

        // Friction
        float GetFriction() const { return m_friction; }
        void SetFriction(float friction);

        // Angular friction
        float GetFrictionRolling() const { return m_friction_rolling; }
        void SetFrictionRolling(float friction_rolling);

        // Restitution
        float GetRestitution() const { return m_restitution; }
        void SetRestitution(float restitution);

        // Gravity
        void SetUseGravity(bool gravity);
        bool GetUseGravity() const { return m_use_gravity; };
        Math::Vector3 GetGravity() const { return m_gravity; }
        void SetGravity(const Math::Vector3& acceleration);

        // Kinematic
        void SetIsKinematic(bool kinematic);
        bool GetIsKinematic() const { return m_is_kinematic; }

        // Forces
        void SetLinearVelocity(const Math::Vector3& velocity, const bool activate = true) const;
        Math::Vector3 GetLinearVelocity() const;
        void SetAngularVelocity(const Math::Vector3& velocity, const bool activate = true) const;
        void ApplyForce(const Math::Vector3& force, PhysicsForce mode) const;
        void ApplyForceAtPosition(const Math::Vector3& force, const Math::Vector3& position, PhysicsForce mode) const;
        void ApplyTorque(const Math::Vector3& torque, PhysicsForce mode) const;

        // Position lock
        void SetPositionLock(bool lock);
        void SetPositionLock(const Math::Vector3& lock);
        Math::Vector3 GetPositionLock() const { return m_position_lock; }

        // Rotation lock
        void SetRotationLock(bool lock);
        void SetRotationLock(const Math::Vector3& lock);
        Math::Vector3 GetRotationLock() const { return m_rotation_lock; }

        // Center of mass
        void SetCenterOfMass(const Math::Vector3& center_of_mass);
        const Math::Vector3& GetCenterOfMass() const { return m_center_of_mass; }

        // Position
        Math::Vector3 GetPosition() const;
        void SetPosition(const Math::Vector3& position, const bool activate = true) const;

        // Rotation
        Math::Quaternion GetRotation() const;
        void SetRotation(const Math::Quaternion& rotation, const bool activate = true) const;

        // Constraint
        void AddConstraint(Constraint* constraint);
        void RemoveConstraint(Constraint* constraint);

        // Bounding box
        const Math::Vector3& GetBoundingBox() const { return m_size; }
        void SetBoundingBox(const Math::Vector3& boundingBox);

        // Shape type
        PhysicsShape GetShapeType() const { return m_shape_type; }
        void SetShapeType(PhysicsShape type);

        // Body type
        PhysicsBodyType GetBodyType() const { return m_body_type; }
        void SetBodyType(const PhysicsBodyType type);

        // Torque
        float GetTorqueMaxNewtons() const              { return m_torque_max_newtons; }
        void SetTorqueMaxNewtons(float torque_newtons) { m_torque_max_newtons = torque_newtons; }

        // Misc
        bool IsGrounded() const;
        void ClearForces() const;
        void Activate() const;
        void Deactivate() const;
        bool IsInWorld()       const { return m_in_world; }
        void* GetBtRigidBody() const { return m_rigid_body; }

    private:
        void AddBodyToWorld();
        void RemoveBodyFromWorld();
        void UpdateShape();

        float m_mass                   = 0.0f;
        float m_friction               = 0.0f;
        float m_friction_rolling       = 0.0f;
        float m_restitution            = 0.0f;
        bool m_use_gravity             = false;
        bool m_is_kinematic            = false;
        Math::Vector3 m_gravity        = Math::Vector3::Zero;
        Math::Vector3 m_position_lock  = Math::Vector3::Zero;
        Math::Vector3 m_rotation_lock  = Math::Vector3::Zero;
        Math::Vector3 m_center_of_mass = Math::Vector3::Zero;
        Math::Vector3 m_size           = Math::Vector3::One;
        PhysicsShape m_shape_type      = PhysicsShape::Box;
        PhysicsBodyType m_body_type    = PhysicsBodyType::RigidBody;
        uint32_t terrain_width         = 0;
        uint32_t terrain_length        = 0;
        bool m_in_world                = false;
        void* m_shape                  = nullptr;
        void* m_rigid_body             = nullptr;
        void* m_vehicle                = nullptr;
        float m_torque_newtons         = 0.0f;
        float m_torque_max_newtons     = 0.0f;
        float m_steering_angle_radians = 0.0f;
        std::vector<Constraint*> m_constraints;
    };
}
