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

#pragma once

//= INCLUDES ==================
#include "Component.h"
#include <vector>
#include "../../Math/Vector3.h"
//=============================

namespace spartan
{
    class Entity;
    class Physics;
    class Car;
    class Car2;
    namespace math { class Quaternion; }

    enum class PhysicsBodyType
    {
        RigidBody,
        Vehicle,
        Vehicle2
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
        Mesh,
        Max
    };

    struct PhysicsBodyMeshData
    {
        std::vector<uint32_t> indices;
        std::vector<RHI_Vertex_PosTexNorTan> vertices;
    };

    class PhysicsBody : public Component
    {
    public:
        PhysicsBody(Entity* entity);
        ~PhysicsBody();

        // component
        void OnInitialize() override;
        void OnRemove() override;
        void OnStart() override;
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;

        // mass
        float GetMass() const { return m_mass; }
        void SetMass(float mass);

        // friction
        float GetFriction() const { return m_friction; }
        void SetFriction(float friction);

        // angular friction
        float GetFrictionRolling() const { return m_friction_rolling; }
        void SetFrictionRolling(float friction_rolling);

        // restitution
        float GetRestitution() const { return m_restitution; }
        void SetRestitution(float restitution);

        // gravity
        void SetUseGravity(bool gravity);
        bool GetUseGravity() const { return m_use_gravity; };
        math::Vector3 GetGravity() const { return m_gravity; }
        void SetGravity(const math::Vector3& gravity);

        // kinematic
        void SetIsKinematic(bool kinematic);
        bool GetIsKinematic() const { return m_is_kinematic; }

        // forces
        void SetLinearVelocity(const math::Vector3& velocity, const bool activate = true) const;
        math::Vector3 GetLinearVelocity() const;
        void SetAngularVelocity(const math::Vector3& velocity, const bool activate = true) const;
        void ApplyForce(const math::Vector3& force, PhysicsForce mode) const;
        void ApplyForceAtPosition(const math::Vector3& force, const math::Vector3& position, PhysicsForce mode) const;
        void ApplyTorque(const math::Vector3& torque, PhysicsForce mode) const;

        // position lock
        void SetPositionLock(bool lock);
        void SetPositionLock(const math::Vector3& lock);
        math::Vector3 GetPositionLock() const { return m_position_lock; }

        // rotation lock
        void SetRotationLock(bool lock);
        void SetRotationLock(const math::Vector3& lock);
        math::Vector3 GetRotationLock() const { return m_rotation_lock; }

        // center of mass
        void SetCenterOfMass(const math::Vector3& center_of_mass);
        const math::Vector3& GetCenterOfMass() const { return m_center_of_mass; }

        // position
        math::Vector3 GetPosition() const;
        void SetPosition(const math::Vector3& position, const bool activate = true) const;

        // rotation
        math::Quaternion GetRotation() const;
        void SetRotation(const math::Quaternion& rotation, const bool activate = true) const;

        // bounding box
        const math::Vector3& GetBoundingBox() const { return m_size; }
        void SetBoundingBox(const math::Vector3& boundingBox);

        // shape type
        PhysicsShape GetShapeType() const { return m_shape_type; }
        void SetShapeType(PhysicsShape type, const bool replicate_hierarchy = false);

        // body type
        PhysicsBodyType GetBodyType() const { return m_body_type; }
        void SetBodyType(const PhysicsBodyType type);

        // ray tracing
        bool RayTraceIsGrounded() const;
        math::Vector3 RayTraceIsNearStairStep(const math::Vector3& forward) const;

        // dimensional properties
        float GetCapsuleVolume();
        float GetCapsuleRadius();

        // misc
        void ClearForces() const;
        void Activate() const;
        void Deactivate() const;
        void* GetBtRigidBody() const  { return m_rigid_body; }
        std::shared_ptr<Car> GetCar() { return m_car; }

        constexpr static inline float mass_auto = FLT_MAX;

    private:
        void AddBodyToWorld();
        void RemoveBodyFromWorld();
        void UpdateShape();

        float m_mass                   = 0.0f;
        float m_friction               = 1.0f;
        float m_friction_rolling       = 0.002f;
        float m_restitution            = 0.2f;
        bool m_use_gravity             = true;
        bool m_is_kinematic            = false;
        math::Vector3 m_gravity        = math::Vector3::Zero;
        math::Vector3 m_position_lock  = math::Vector3::Zero;
        math::Vector3 m_rotation_lock  = math::Vector3::Zero;
        math::Vector3 m_center_of_mass = math::Vector3::Zero;
        math::Vector3 m_size           = math::Vector3::One;
        PhysicsShape m_shape_type      = PhysicsShape::Max;
        PhysicsBodyType m_body_type    = PhysicsBodyType::RigidBody;
        uint32_t terrain_width         = 0;
        uint32_t terrain_length        = 0;
        void* m_shape                  = nullptr;
        void* m_rigid_body             = nullptr;
        bool m_replicate_hierarchy     = false;
        std::vector<PhysicsBodyMeshData> m_mesh_data;

        // vehicle
        std::shared_ptr<Car> m_car;
        std::shared_ptr<Car2> m_car2;
    };
}
