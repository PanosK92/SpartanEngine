/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "Constraint.h"
#include "Renderable.h"
#include "Terrain.h"
#include "../Entity.h"
#include "../RHI/RHI_Vertex.h"
#include "../RHI/RHI_Texture.h"
#include "../../IO/FileStream.h"
#include "../Physics/Car.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
SP_WARNINGS_OFF
#include <BulletDynamics/Dynamics/btRigidBody.h>
#include <BulletCollision/CollisionShapes/btSphereShape.h>
#include <BulletCollision/CollisionShapes/btStaticPlaneShape.h>
#include <BulletCollision/CollisionShapes/btCylinderShape.h>
#include <BulletCollision/CollisionShapes/btCapsuleShape.h>
#include <BulletCollision/CollisionShapes/btConeShape.h>
#include <BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h>
#include <BulletCollision/CollisionShapes/btTriangleMesh.h>
#include <BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h>
#include <BulletCollision/CollisionShapes/btConvexHullShape.h>
#include "../Rendering/Renderer.h"
SP_WARNINGS_ON
//====================================================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        const float k_default_deactivation_time = 2000;
        const float k_default_mass              = 1.0f;
        const float k_default_restitution       = 0.2f;
        const float k_default_friction          = 1.0f;
        const float k_default_friction_rolling  = 0.0f;
    }

    #define shape static_cast<btCollisionShape*>(m_shape)
    #define rigid_body static_cast<btRigidBody*>(m_rigid_body)
    #define vehicle static_cast<btRaycastVehicle*>(m_vehicle)

    class MotionState : public btMotionState
    {
    public:
        MotionState(PhysicsBody* rigidBody) { m_rigidBody = rigidBody; }

        // engine -> bullet
        void getWorldTransform(btTransform& worldTrans) const override
        {
            const Vector3 last_position    = m_rigidBody->GetEntity()->GetPosition();
            const Quaternion last_rotation = m_rigidBody->GetEntity()->GetRotation();

            worldTrans.setOrigin(ToBtVector3(last_position + last_rotation * m_rigidBody->GetCenterOfMass()));
            worldTrans.setRotation(ToBtQuaternion(last_rotation));
        }

        // bullet -> engine
        void setWorldTransform(const btTransform& worldTrans) override
        {
            const Quaternion new_rotation = ToQuaternion(worldTrans.getRotation());
            const Vector3 new_position    = ToVector3(worldTrans.getOrigin()) - new_rotation * m_rigidBody->GetCenterOfMass();

            m_rigidBody->GetEntity()->SetPosition(new_position);
            m_rigidBody->GetEntity()->SetRotation(new_rotation);
        }
    private:
        PhysicsBody* m_rigidBody;
    };

    PhysicsBody::PhysicsBody(weak_ptr<Entity> entity) : Component(entity)
    {
        m_in_world         = false;
        m_mass             = k_default_mass;
        m_restitution      = k_default_restitution;
        m_friction         = k_default_friction;
        m_friction_rolling = k_default_friction_rolling;
        m_use_gravity      = true;
        m_gravity          = Physics::GetGravity();
        m_is_kinematic     = false;
        m_position_lock    = Vector3::Zero;
        m_rotation_lock    = Vector3::Zero;
        m_rigid_body       = nullptr;
        m_shape_type       = PhysicsShape::Box;
        m_center_of_mass   = Vector3::Zero;
        m_size             = Vector3::One;
        m_shape            = nullptr;
        m_car              = make_shared<Car>();

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

        if (GetEntity()->GetComponent<Renderable>())
        {
            m_shape_type = PhysicsShape::MeshConvexHull;
        }

        if (GetEntity()->GetComponent<Terrain>())
        {
            m_shape_type = PhysicsShape::Terrain;
        }
    }

    PhysicsBody::~PhysicsBody()
    {
        OnRemove();
    }

    void PhysicsBody::OnInitialize()
    {
        Component::OnInitialize();
        UpdateShape();
    }

    void PhysicsBody::OnRemove()
    {
        RemoveBodyFromWorld();

        delete static_cast<btCollisionShape*>(m_shape);
        m_shape = nullptr;
    }

    void PhysicsBody::OnStart()
    {
        Activate();
    }

    void PhysicsBody::OnTick()
    {
        // when the rigid body is inactive or we are in editor mode, allow the user to move/rotate it
        if (!Engine::IsFlagSet(EngineMode::Game))
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

        if (m_body_type == PhysicsBodyType::Vehicle)
        {
            m_car->Tick();
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
        stream->Write(m_in_world);
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
        stream->Read(&m_in_world);
        m_shape_type = PhysicsShape(stream->ReadAs<uint32_t>());
        stream->Read(&m_size);
        stream->Read(&m_center_of_mass);

        AddBodyToWorld();
        UpdateShape();
    }

    void PhysicsBody::SetMass(float mass)
    {
        mass = Helper::Max(mass, 0.0f);
        if (mass != m_mass)
        {
            m_mass = mass;
            AddBodyToWorld();
        }
    }

    void PhysicsBody::SetFriction(float friction)
    {
        if (!m_rigid_body || m_friction == friction)
            return;

        m_friction = friction;
        rigid_body->setFriction(friction);
    }

    void PhysicsBody::SetFrictionRolling(float frictionRolling)
    {
        if (!m_rigid_body || m_friction_rolling == frictionRolling)
            return;

        m_friction_rolling = frictionRolling;
        rigid_body->setRollingFriction(frictionRolling);
    }

    void PhysicsBody::SetRestitution(float restitution)
    {
        if (!m_rigid_body || m_restitution == restitution)
            return;

        m_restitution = restitution;
        rigid_body->setRestitution(restitution);
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

        rigid_body->setLinearVelocity(ToBtVector3(velocity));
        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    Vector3 PhysicsBody::GetLinearVelocity() const
    {
        if (!m_rigid_body)
            return Vector3::Zero;

        return ToVector3(rigid_body->getLinearVelocity());
    }
    
	void PhysicsBody::SetAngularVelocity(const Vector3& velocity, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        rigid_body->setAngularVelocity(ToBtVector3(velocity));
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
            rigid_body->applyCentralForce(ToBtVector3(force));
        }
        else if (mode == PhysicsForce::Impulse)
        {
            rigid_body->applyCentralImpulse(ToBtVector3(force));
        }
    }

    void PhysicsBody::ApplyForceAtPosition(const Vector3& force, const Vector3& position, PhysicsForce mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == PhysicsForce::Constant)
        {
            rigid_body->applyForce(ToBtVector3(force), ToBtVector3(position));
        }
        else if (mode == PhysicsForce::Impulse)
        {
            rigid_body->applyImpulse(ToBtVector3(force), ToBtVector3(position));
        }
    }

    void PhysicsBody::ApplyTorque(const Vector3& torque, PhysicsForce mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == PhysicsForce::Constant)
        {
            rigid_body->applyTorque(ToBtVector3(torque));
        }
        else if (mode == PhysicsForce::Impulse)
        {
            rigid_body->applyTorqueImpulse(ToBtVector3(torque));
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
        rigid_body->setLinearFactor(ToBtVector3(Vector3::One - lock));
    }

    void PhysicsBody::SetRotationLock(bool lock)
    {
        SetRotationLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetRotationLock(const Vector3& lock)
    {
        if (!m_rigid_body || m_rotation_lock == lock)
            return;

        m_rotation_lock = lock;
        rigid_body->setAngularFactor(ToBtVector3(Vector3::One - lock));

        // recalculate inertia since bullet doesn't seem to be doing it automatically
        btVector3 inertia;
        rigid_body->getCollisionShape()->calculateLocalInertia(m_mass, inertia);
        rigid_body->setMassProps(m_mass, inertia * ToBtVector3(Vector3::One - lock));
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
            const btTransform& transform = rigid_body->getWorldTransform();
            return ToVector3(transform.getOrigin()) - ToQuaternion(transform.getRotation()) * m_center_of_mass;
        }
    
        return Vector3::Zero;
    }

    void PhysicsBody::SetPosition(const Vector3& position, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        // set position to world transform
        btTransform& transform_world = rigid_body->getWorldTransform();
        transform_world.setOrigin(ToBtVector3(position + ToQuaternion(transform_world.getRotation()) * m_center_of_mass));

        // set position to interpolated world transform
        btTransform transform_world_interpolated = rigid_body->getInterpolationWorldTransform();
        transform_world_interpolated.setOrigin(transform_world.getOrigin());
        rigid_body->setInterpolationWorldTransform(transform_world_interpolated);

        if (activate)
        {
            Activate();
        }
    }

    Quaternion PhysicsBody::GetRotation() const
    {
        return m_rigid_body ? ToQuaternion(rigid_body->getWorldTransform().getRotation()) : Quaternion::Identity;
    }

    void PhysicsBody::SetRotation(const Quaternion& rotation, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        // set rotation to world transform
        const Vector3 oldPosition = GetPosition();
        btTransform& transform_world = rigid_body->getWorldTransform();
        transform_world.setRotation(ToBtQuaternion(rotation));
        if (m_center_of_mass != Vector3::Zero)
        {
            transform_world.setOrigin(ToBtVector3(oldPosition + rotation * m_center_of_mass));
        }

        // set rotation to interpolated world transform
        btTransform interpTrans = rigid_body->getInterpolationWorldTransform();
        interpTrans.setRotation(transform_world.getRotation());
        if (m_center_of_mass != Vector3::Zero)
        {
            interpTrans.setOrigin(transform_world.getOrigin());
        }
        rigid_body->setInterpolationWorldTransform(interpTrans);

        rigid_body->updateInertiaTensor();

        if (activate)
        {
            Activate();
        }
    }

    void PhysicsBody::ClearForces() const
    {
        if (!m_rigid_body)
            return;

        rigid_body->clearForces();
    }

    void PhysicsBody::Activate() const
    {
        if (!m_rigid_body)
            return;

        if (m_mass > 0.0f)
        {
            rigid_body->activate(true);
        }
    }

    void PhysicsBody::Deactivate() const
    {
        if (!m_rigid_body)
            return;

        rigid_body->setActivationState(WANTS_DEACTIVATION);
    }

    void PhysicsBody::AddConstraint(Constraint* constraint)
    {
        m_constraints.emplace_back(constraint);
    }

    void PhysicsBody::RemoveConstraint(Constraint* constraint)
    {
        for (auto it = m_constraints.begin(); it != m_constraints.end(); )
        {
            const auto itConstraint = *it;
            if (constraint->GetObjectId() == itConstraint->GetObjectId())
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

    void PhysicsBody::AddBodyToWorld()
    {
        SP_ASSERT(shape != nullptr);

        // a vehicle with a mass of zero or less will cause crash
        if (m_body_type == PhysicsBodyType::Vehicle && m_mass <= 0.0f)
        {
            m_mass = 0.01f;
        }

        // compute local inertia so that we can transfer it to the new body
        btVector3 local_intertia = btVector3(0, 0, 0);
        {
            bool is_static          = m_mass == 0.0f;                     // static objects don't have inertia
            bool is_supported_shape = m_shape_type != PhysicsShape::Mesh; // shapes like btBvhTriangleMeshShape don't support local inertia
            bool support_inertia    = !is_static && is_supported_shape;
            if (m_rigid_body && support_inertia && shape)
            {
                local_intertia = rigid_body->getLocalInertia();
                shape->calculateLocalInertia(m_mass, local_intertia);
            }
        }

        // remove and delete the old body
        RemoveBodyFromWorld();

        // create rigid body
        {
            btRigidBody::btRigidBodyConstructionInfo construction_info(0.0f, nullptr, nullptr);
            construction_info.m_mass            = m_mass;
            construction_info.m_friction        = m_friction;
            construction_info.m_rollingFriction = m_friction_rolling;
            construction_info.m_restitution     = m_restitution;
            construction_info.m_collisionShape  = static_cast<btCollisionShape*>(m_shape);
            construction_info.m_localInertia    = local_intertia;
            construction_info.m_motionState     = new MotionState(this); // we delete this manually later

            m_rigid_body = new btRigidBody(construction_info);
            rigid_body->setUserPointer(this);
        }

        // reapply constraint positions for new center of mass shift
        for (Constraint* constraint : m_constraints)
        {
            constraint->ApplyFrames();
        }

        if (m_body_type == PhysicsBodyType::Vehicle)
        {
            m_car->Create(rigid_body, m_entity_ptr);
        }

        // set flags
        {
            int flags = rigid_body->getCollisionFlags();

            // kinematic
            {
                if (m_is_kinematic)
                {
                    flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
                }
                else
                {
                    flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
                }

                rigid_body->setCollisionFlags(flags);
                rigid_body->forceActivationState((m_is_kinematic || m_body_type == PhysicsBodyType::Vehicle) ? DISABLE_DEACTIVATION : ISLAND_SLEEPING);
                rigid_body->setDeactivationTime(k_default_deactivation_time);
            }

            // gravity
            {
                if (m_use_gravity)
                {
                    flags &= ~BT_DISABLE_WORLD_GRAVITY;
                }
                else
                {
                    flags |= BT_DISABLE_WORLD_GRAVITY;
                }

                rigid_body->setFlags(flags);
                rigid_body->setGravity(m_use_gravity ? ToBtVector3(m_gravity) : btVector3(0.0f, 0.0f, 0.0f));
            }
        }

        // set transform
        {
            SetPosition(GetEntity()->GetPosition());
            SetRotation(GetEntity()->GetRotation());

            SetPositionLock(m_position_lock);
            SetRotationLock(m_rotation_lock);
        }

        // add to world and activate
        {
            Physics::AddBody(rigid_body);
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
    }

    void PhysicsBody::RemoveBodyFromWorld()
    {
        if (!m_rigid_body)
            return;

        // release any constraints that refer to it
        for (const auto& constraint : m_constraints)
        {
            constraint->ReleaseConstraint();
        }

        if (m_rigid_body)
        {
            if (m_in_world)
            {
                Physics::RemoveBody(reinterpret_cast<btRigidBody*&>(m_rigid_body));
                m_in_world = false;
            }

            delete rigid_body->getMotionState();
            delete rigid_body;
            m_rigid_body = nullptr;
        }
    }

    void PhysicsBody::SetBoundingBox(const Vector3& bounding_box)
    {
        if (m_size == bounding_box)
            return;

        m_size   = bounding_box;
        m_size.x = Helper::Clamp(m_size.x, Helper::SMALL_FLOAT, INFINITY);
        m_size.y = Helper::Clamp(m_size.y, Helper::SMALL_FLOAT, INFINITY);
        m_size.z = Helper::Clamp(m_size.z, Helper::SMALL_FLOAT, INFINITY);

        UpdateShape();
    }

    void PhysicsBody::SetShapeType(PhysicsShape type)
    {
        if (m_shape_type == type)
            return;

        if (type == PhysicsShape::Terrain)
        {
            if (!m_entity_ptr->GetComponent<Terrain>())
            {
                SP_LOG_WARNING("Can't set terrain shape as there is no terrain component");
                return;
            }
        }

        m_shape_type = type;
        UpdateShape();
    }

    void PhysicsBody::SetBodyType(const PhysicsBodyType type)
    {
        if (m_body_type == type)
            return;

        m_body_type = type;
        AddBodyToWorld();
    }
    
    bool PhysicsBody::RayTraceIsGrounded() const
    {
        // get the lowest point of the AABB
        btVector3 aabb_min, aabb_max;
        shape->getAabb(rigid_body->getWorldTransform(), aabb_min, aabb_max);
        float min_y = aabb_min.y();

        // get the lowest point of the body
        Vector3 ray_start = ToVector3(rigid_body->getWorldTransform().getOrigin());
        ray_start.y       = min_y + 0.1f; // offset of 0.1f to avoid starting inside/at the ground

        // return the first hit
        vector<btRigidBody*> hit_bodies = Physics::RayCast(ray_start, ray_start - Vector3(0.0f, 0.2f, 0.0f));
        for (btRigidBody* hit_body : hit_bodies)
        {
            // ensure we are not hitting ourselves
            if (hit_body != rigid_body)
                return true;
        }

        return false;
    }

    Vector3 PhysicsBody::RayTraceIsNearStairStep(const Vector3& forward) const
    {
        const float ray_length          = 5.0f;
        const float max_scalable_height = 0.5f;
        const float forward_distance    = 0.5f;

        // get the lowest point of the AABB
        btVector3 aabb_min, aabb_max;
        shape->getAabb(rigid_body->getWorldTransform(), aabb_min, aabb_max);
        float min_y = aabb_min.y();

        // get the starting position
        Vector3 ray_start = ToVector3(rigid_body->getWorldTransform().getOrigin());
        ray_start.y       = min_y + ray_length;                     // raise it
        ray_start         = ray_start + forward * forward_distance; // move it forward
        // at this point, ray_start is likely to be above the stair step

        // the end position is just going down
        Vector3 ray_end = ray_start + Vector3(0.0f, -ray_length * 2.0f, 0.0f);

        Renderer::DrawDirectionalArrow(ray_start, ray_end, 0.1f);

        Vector3 hit_position = Physics::RayCastFirstHitPosition(ray_start, ray_end);

        bool is_scalable = Helper::Abs(hit_position.y - min_y) <= max_scalable_height;
        bool is_above    = hit_position.y > min_y;

        return (is_scalable && is_above) ? hit_position : Vector3::Infinity;
    }

    float PhysicsBody::GetCapsuleVolume()
	{
        btCapsuleShape* capsule_shape = static_cast<btCapsuleShape*>(m_shape);

        // get the radius of the capsule
        float radius = capsule_shape->getRadius();

        // get the height of the cylindrical part of the capsule
        // for a btCapsuleShape, the height is the distance between the centers of the end caps.
        float cylinder_height = capsule_shape->getHalfHeight() * 2.0f;

        // compute the volume of the cylindrical part
        float cylinder_volume = Math::Helper::PI * radius * radius * cylinder_height;

        // compute the volume of the hemispherical ends
        float hemisphere_volume = (4.0f / 3.0f) * Math::Helper::PI * std::pow(radius, 3.0f);

        // total volume is the sum of the cylinder and two hemispheres
        return cylinder_volume + hemisphere_volume;
	}

    float PhysicsBody::GetCapsuleRadius()
    {
        btCapsuleShape* capsule_shape = static_cast<btCapsuleShape*>(m_shape);

        return capsule_shape->getRadius();
    }
    
    void PhysicsBody::UpdateShape()
    {
        if (shape)
        {
            delete shape;
            m_shape = nullptr;
        }

        // get common prerequisites for certain shapes
        vector<uint32_t> indices;
        vector<RHI_Vertex_PosTexNorTan> vertices;
        shared_ptr<Renderable> renderable = nullptr;
        if (m_shape_type == PhysicsShape::Mesh || m_shape_type == PhysicsShape::MeshConvexHull)
        {
            // get renderable
            renderable = GetEntity()->GetComponent<Renderable>();
            if (!renderable || !renderable->HasMesh())
            {
                SP_LOG_WARNING("For a mesh shape to be constructed, there needs to be a Renderable component with a mesh");
                return;
            }

            // get geometry
            renderable->GetGeometry(&indices, &vertices);
            if (vertices.empty())
            {
                SP_LOG_WARNING("A shape can't be constructed without vertices");
                return;
            }
        }

        Vector3 size = m_size * GetEntity()->GetScale();

        // construct new shape
        switch (m_shape_type)
        {
            case PhysicsShape::Box:
                m_shape = new btBoxShape(ToBtVector3(size * 0.5f));
                break;

            case PhysicsShape::Sphere:
                m_shape = new btSphereShape(size.x * 0.5f);
                break;

            case PhysicsShape::StaticPlane:
                m_shape = new btStaticPlaneShape(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
                break;

            case PhysicsShape::Cylinder:
                m_shape = new btCylinderShape(ToBtVector3(size * 0.5f));
                break;

            case PhysicsShape::Capsule:
            {
                float radius = Helper::Max(size.x, size.z) * 0.5f;
                float height = size.y;
                m_shape = new btCapsuleShape(radius, height);
                break;
            }

            case PhysicsShape::Cone:
                m_shape = new btConeShape(size.x * 0.5f, size.y);
                break;

            case PhysicsShape::Terrain:
            {
                Terrain* terrain = GetEntity()->GetComponent<Terrain>().get();
                if (!terrain)
                {
                    SP_LOG_WARNING("For a terrain shape to be constructed, there needs to be a Terrain component");
                    return;
                }

                btHeightfieldTerrainShape* shape_local = new btHeightfieldTerrainShape(
                    terrain->GetHeightMap()->GetWidth(),  // width
                    terrain->GetHeightMap()->GetHeight(), // length
                    terrain->GetHeightData(),             // data - row major
                    1.0f,                                 // height scale
                    terrain->GetMinY(),                   // min height
                    terrain->GetMaxY(),                   // max height
                    1,                                    // Up axis (0=x, 1=y, 2=z)
                    PHY_FLOAT,                            // Data type
                    false                                 // Flip quad edges or not
                );
                
                shape_local->setLocalScaling(ToBtVector3(size));
                m_shape = shape_local;

                // calculate the offset needed to re-center the terrain
                float offset_xz = -0.5f; // don't know why bullet needs this
                float offset_y  = (terrain->GetMaxY() + terrain->GetMinY()) / 2.0f;

                // set the center of mass to adjust for Bullet's re-centering
                SetCenterOfMass(Vector3(offset_xz, offset_y, offset_xz));

                break;
            }

            case PhysicsShape::Mesh:
            {
                btTriangleMesh* shape_local = new btTriangleMesh();
                for (uint32_t i = 0; i < static_cast<uint32_t>(indices.size()); i += 3)
                {
                    btVector3 vertex0(vertices[indices[i]].pos[0],     vertices[indices[i]].pos[1],     vertices[indices[i]].pos[2]);
                    btVector3 vertex1(vertices[indices[i + 1]].pos[0], vertices[indices[i + 1]].pos[1], vertices[indices[i + 1]].pos[2]);
                    btVector3 vertex2(vertices[indices[i + 2]].pos[0], vertices[indices[i + 2]].pos[1], vertices[indices[i + 2]].pos[2]);
                    shape_local->addTriangle(vertex0, vertex1, vertex2);
                }
                shape_local->setScaling(ToBtVector3(size));

                m_shape = new btBvhTriangleMeshShape(shape_local, true);
                break;
            }

            case PhysicsShape::MeshConvexHull:
            {
                btConvexHullShape* shape_approximated = new btConvexHullShape(
                    (btScalar*)&vertices[0],                                 // points
                    renderable->GetVertexCount(),                            // point count
                    static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan))); // stride

                shape_approximated->setLocalScaling(ToBtVector3(size));

                // turn it into a proper convex hull since btConvexHullShape is an approximation
                m_shape = static_cast<btConvexHullShape*>(shape_approximated);
                static_cast<btConvexHullShape*>(m_shape)->optimizeConvexHull();
                break;
            }
        }

        static_cast<btCollisionShape*>(m_shape)->setUserPointer(this);

        // re-add the body to the world so it's re-created with the new shape
        AddBodyToWorld();
    }
}
