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

//= INCLUDES ==================================================
#include "pch.h"
#include "PhysicsBody.h"
#include "Transform.h"
#include "Constraint.h"
#include "Renderable.h"
#include "../RHI/RHI_Vertex.h"
#include "../Entity.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
#include "../../IO/FileStream.h"
SP_WARNINGS_OFF
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "BulletCollision/CollisionShapes/btCollisionShape.h"
#include "BulletCollision/CollisionShapes/btBoxShape.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#include "BulletCollision/CollisionShapes/btStaticPlaneShape.h"
#include "BulletCollision/CollisionShapes/btCylinderShape.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#include "BulletCollision/CollisionShapes/btConeShape.h"
#include "BulletCollision/CollisionShapes/btConvexHullShape.h"
SP_WARNINGS_ON
//=============================================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    namespace
    {
        static const float DEFAULT_MASS              = 0.0f;
        static const float DEFAULT_FRICTION          = 0.5f;
        static const float DEFAULT_FRICTION_ROLLING  = 0.0f;
        static const float DEFAULT_RESTITUTION       = 0.0f;
        static const float DEFAULT_DEACTIVATION_TIME = 2000;
    }

    class MotionState : public btMotionState
    {
    public:
        MotionState(PhysicsBody* rigidBody) { m_rigidBody = rigidBody; }

        // Update from engine, ENGINE -> BULLET
        void getWorldTransform(btTransform& worldTrans) const override
        {
            const Vector3 lastPos    = m_rigidBody->GetTransform()->GetPosition();
            const Quaternion lastRot = m_rigidBody->GetTransform()->GetRotation();

            worldTrans.setOrigin(ToBtVector3(lastPos + lastRot * m_rigidBody->GetCenterOfMass()));
            worldTrans.setRotation(ToBtQuaternion(lastRot));
        }

        // Update from bullet, BULLET -> ENGINE
        void setWorldTransform(const btTransform& worldTrans) override
        {
            const Quaternion newWorldRot = ToQuaternion(worldTrans.getRotation());
            const Vector3 newWorldPos    = ToVector3(worldTrans.getOrigin()) - newWorldRot * m_rigidBody->GetCenterOfMass();

            m_rigidBody->GetTransform()->SetPosition(newWorldPos);
            m_rigidBody->GetTransform()->SetRotation(newWorldRot);
        }
    private:
        PhysicsBody* m_rigidBody;
    };

    PhysicsBody::PhysicsBody(weak_ptr<Entity> entity) : Component(entity)
    {
        m_in_world         = false;
        m_mass             = DEFAULT_MASS;
        m_restitution      = DEFAULT_RESTITUTION;
        m_friction         = DEFAULT_FRICTION;
        m_friction_rolling = DEFAULT_FRICTION_ROLLING;
        m_use_gravity      = true;
        m_gravity          = Physics::GetGravity();
        m_is_kinematic     = false;
        m_position_lock    = Vector3::Zero;
        m_rotation_lock    = Vector3::Zero;
        m_rigid_body       = nullptr;
        m_shape_type       = ColliderShape::Box;
        m_shape_center     = Vector3::Zero;
        m_size             = Vector3::One;
        m_shape            = nullptr;

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
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shape_center, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_vertexLimit, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shape_optimize, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_shape_type, SetShapeType, ColliderShape);
    }

    PhysicsBody::~PhysicsBody()
    {
        DeleteBody();

        delete m_shape;
        m_shape = nullptr;
    }

    void PhysicsBody::OnInitialize()
    {
        Component::OnInitialize();

        // shape
        {
            // If there is a mesh, use it's bounding box
            if (Renderable* renderable = GetEntityPtr()->GetComponent<Renderable>().get())
            {
                m_shape_center = Vector3::Zero;
                m_size         = renderable->GetAabb().GetSize();
            }

            UpdateShape();
        }

        AddBodyToWorld();
    }

    void PhysicsBody::OnRemove()
    {
        DeleteBody();

        delete m_shape;
        m_shape = nullptr;
    }

    void PhysicsBody::OnStart()
    {
        Activate();
    }

    void PhysicsBody::OnTick()
    {
        // When the rigid body is inactive or we are in editor mode, allow the user to move/rotate it
        if (!IsActivated() || !Engine::IsFlagSet(EngineMode::Game))
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
        stream->Write(m_shape_center);
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
        m_shape_type = ColliderShape(stream->ReadAs<uint32_t>());
        stream->Read(&m_size);
        stream->Read(&m_shape_center);

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
        m_rigid_body->setFriction(friction);
    }

    void PhysicsBody::SetFrictionRolling(float frictionRolling)
    {
        if (!m_rigid_body || m_friction_rolling == frictionRolling)
            return;

        m_friction_rolling = frictionRolling;
        m_rigid_body->setRollingFriction(frictionRolling);
    }

    void PhysicsBody::SetRestitution(float restitution)
    {
        if (!m_rigid_body || m_restitution == restitution)
            return;

        m_restitution = restitution;
        m_rigid_body->setRestitution(restitution);
    }

    void PhysicsBody::SetUseGravity(bool gravity)
    {
        if (gravity == m_use_gravity)
            return;

        m_use_gravity = gravity;
        AddBodyToWorld();
    }

    void PhysicsBody::SetGravity(const Vector3& acceleration)
    {
        if (m_gravity == acceleration)
            return;

        m_gravity = acceleration;
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

        m_rigid_body->setLinearVelocity(ToBtVector3(velocity));
        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    void PhysicsBody::SetAngularVelocity(const Vector3& velocity, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        m_rigid_body->setAngularVelocity(ToBtVector3(velocity));
        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    void PhysicsBody::ApplyForce(const Vector3& force, ForceMode mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == Force)
        {
            m_rigid_body->applyCentralForce(ToBtVector3(force));
        }
        else if (mode == Impulse)
        {
            m_rigid_body->applyCentralImpulse(ToBtVector3(force));
        }
    }

    void PhysicsBody::ApplyForceAtPosition(const Vector3& force, const Vector3& position, ForceMode mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == Force)
        {
            m_rigid_body->applyForce(ToBtVector3(force), ToBtVector3(position));
        }
        else if (mode == Impulse)
        {
            m_rigid_body->applyImpulse(ToBtVector3(force), ToBtVector3(position));
        }
    }

    void PhysicsBody::ApplyTorque(const Vector3& torque, ForceMode mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == Force)
        {
            m_rigid_body->applyTorque(ToBtVector3(torque));
        }
        else if (mode == Impulse)
        {
            m_rigid_body->applyTorqueImpulse(ToBtVector3(torque));
        }
    }

    void PhysicsBody::SetPositionLock(bool lock)
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

    void PhysicsBody::SetPositionLock(const Vector3& lock)
    {
        if (!m_rigid_body || m_position_lock == lock)
            return;

        m_position_lock = lock;
        m_rigid_body->setLinearFactor(ToBtVector3(Vector3::One - lock));
    }

    void PhysicsBody::SetRotationLock(bool lock)
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

    void PhysicsBody::SetRotationLock(const Vector3& lock)
    {
        if (!m_rigid_body || m_rotation_lock == lock)
            return;

        m_rotation_lock = lock;
        m_rigid_body->setAngularFactor(ToBtVector3(Vector3::One - lock));
    }

    void PhysicsBody::SetCenterOfMass(const Vector3& centerOfMass)
    {
        m_center_of_mass = centerOfMass;
        SetPosition(GetPosition());
    }

    Vector3 PhysicsBody::GetPosition() const
    {
        if (m_rigid_body)
        {
            const btTransform& transform = m_rigid_body->getWorldTransform();
            return ToVector3(transform.getOrigin()) - ToQuaternion(transform.getRotation()) * m_center_of_mass;
        }
    
        return Vector3::Zero;
    }

    void PhysicsBody::SetPosition(const Vector3& position, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        // Set position to world transform
        btTransform& transform_world = m_rigid_body->getWorldTransform();
        transform_world.setOrigin(ToBtVector3(position + ToQuaternion(transform_world.getRotation()) * m_center_of_mass));

        // Set position to interpolated world transform
        btTransform transform_world_interpolated = m_rigid_body->getInterpolationWorldTransform();
        transform_world_interpolated.setOrigin(transform_world.getOrigin());
        m_rigid_body->setInterpolationWorldTransform(transform_world_interpolated);

        if (activate)
        {
            Activate();
        }
    }

    Quaternion PhysicsBody::GetRotation() const
    {
        return m_rigid_body ? ToQuaternion(m_rigid_body->getWorldTransform().getRotation()) : Quaternion::Identity;
    }

    void PhysicsBody::SetRotation(const Quaternion& rotation, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        // Set rotation to world transform
        const Vector3 oldPosition = GetPosition();
        btTransform& transform_world = m_rigid_body->getWorldTransform();
        transform_world.setRotation(ToBtQuaternion(rotation));
        if (m_center_of_mass != Vector3::Zero)
        {
            transform_world.setOrigin(ToBtVector3(oldPosition + rotation * m_center_of_mass));
        }

        // Set rotation to interpolated world transform
        btTransform interpTrans = m_rigid_body->getInterpolationWorldTransform();
        interpTrans.setRotation(transform_world.getRotation());
        if (m_center_of_mass != Vector3::Zero)
        {
            interpTrans.setOrigin(transform_world.getOrigin());
        }
        m_rigid_body->setInterpolationWorldTransform(interpTrans);

        m_rigid_body->updateInertiaTensor();

        if (activate)
        {
            Activate();
        }
    }

    void PhysicsBody::ClearForces() const
    {
        if (!m_rigid_body)
            return;

        m_rigid_body->clearForces();
    }

    void PhysicsBody::Activate() const
    {
        if (!m_rigid_body)
            return;

        if (m_mass > 0.0f)
        {
            m_rigid_body->activate(true);
        }
    }

    void PhysicsBody::Deactivate() const
    {
        if (!m_rigid_body)
            return;

        m_rigid_body->setActivationState(WANTS_DEACTIVATION);
    }

    bool PhysicsBody::IsActive() const
    {
        return m_rigid_body->isActive();
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
        if (m_mass < 0.0f)
        {
            m_mass = 0.0f;
        }

        // transfer inertia to new collision shape
        btVector3 local_intertia = btVector3(0, 0, 0);
        if (m_shape && m_rigid_body)
        {
            local_intertia = m_rigid_body ? m_rigid_body->getLocalInertia() : local_intertia;
            m_shape->calculateLocalInertia(m_mass, local_intertia);
        }
        
        DeleteBody();

        // construct a new rigid body
        {
            SP_ASSERT(m_shape != nullptr);

            // construction info
            btRigidBody::btRigidBodyConstructionInfo construction_info(0.0f, nullptr, nullptr);
            construction_info.m_mass             = m_mass;
            construction_info.m_friction         = m_friction;
            construction_info.m_rollingFriction  = m_friction_rolling;
            construction_info.m_restitution      = m_restitution;
            construction_info.m_collisionShape   = m_shape;
            construction_info.m_localInertia     = local_intertia;
            construction_info.m_motionState      = new MotionState(this); // we delete this manually later

            m_rigid_body = new btRigidBody(construction_info);
            m_rigid_body->setUserPointer(this);
        }

        // reapply constraint positions for new center of mass shift
        for (const auto& constraint : m_constraints)
        {
            constraint->ApplyFrames();
        }
        
        Flags_UpdateKinematic();
        Flags_UpdateGravity();

        // transform
        SetPosition(GetTransform()->GetPosition());
        SetRotation(GetTransform()->GetRotation());

        // constraints
        SetPositionLock(m_position_lock);
        SetRotationLock(m_rotation_lock);

        // position and rotation locks
        SetPositionLock(m_position_lock);
        SetRotationLock(m_rotation_lock);

        // add to world
        Physics::AddBody(m_rigid_body);

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

    void PhysicsBody::DeleteBody()
    {
        if (!m_rigid_body)
            return;

        // release any constraints that refer to it
        for (const auto& constraint : m_constraints)
        {
            constraint->ReleaseConstraint();
        }

        // remove it from the world and delete it
        if (m_rigid_body)
        {
            if (m_in_world)
            {
                Physics::RemoveBody(m_rigid_body);
                m_in_world = false;
            }

            delete m_rigid_body->getMotionState();
            delete m_rigid_body;
            m_rigid_body = nullptr;
        }
    }

    void PhysicsBody::Flags_UpdateKinematic() const
    {
        int flags = m_rigid_body->getCollisionFlags();

        if (m_is_kinematic)
        {
            flags |= btCollisionObject::CF_KINEMATIC_OBJECT;
        }
        else
        {
            flags &= ~btCollisionObject::CF_KINEMATIC_OBJECT;
        }

        m_rigid_body->setCollisionFlags(flags);
        m_rigid_body->forceActivationState(m_is_kinematic ? DISABLE_DEACTIVATION : ISLAND_SLEEPING);
        m_rigid_body->setDeactivationTime(DEFAULT_DEACTIVATION_TIME);
    }

    void PhysicsBody::Flags_UpdateGravity() const
    {
        int flags = m_rigid_body->getFlags();

        if (m_use_gravity)
        {
            flags &= ~BT_DISABLE_WORLD_GRAVITY;
        }
        else
        {
            flags |= BT_DISABLE_WORLD_GRAVITY;
        }

        m_rigid_body->setFlags(flags);

        if (m_use_gravity)
        {
            m_rigid_body->setGravity(ToBtVector3(m_gravity));
        }
        else
        {
            m_rigid_body->setGravity(btVector3(0.0f, 0.0f, 0.0f));
        }
    }

    bool PhysicsBody::IsActivated() const
    {
        return m_rigid_body->isActive();
    }

    void PhysicsBody::SetBoundingBox(const Vector3& boundingBox)
    {
        if (m_size == boundingBox)
            return;

        m_size = boundingBox;
        m_size.x = Helper::Clamp(m_size.x, Helper::EPSILON, INFINITY);
        m_size.y = Helper::Clamp(m_size.y, Helper::EPSILON, INFINITY);
        m_size.z = Helper::Clamp(m_size.z, Helper::EPSILON, INFINITY);

        UpdateShape();
    }

    void PhysicsBody::SetShapeType(ColliderShape type)
    {
        if (m_shape_type == type)
            return;

        m_shape_type = type;
        UpdateShape();
    }

    void PhysicsBody::SetOptimize(bool optimize)
    {
        if (m_shape_optimize == optimize)
            return;

        m_shape_optimize = optimize;
        UpdateShape();
    }

    void PhysicsBody::UpdateShape()
    {
        delete m_shape;
        m_shape = nullptr;

        switch (m_shape_type)
        {
        case ColliderShape::Box:
            m_shape = new btBoxShape(ToBtVector3(m_size * 0.5f));
            break;

        case ColliderShape::Sphere:
            m_shape = new btSphereShape(m_size.x * 0.5f);
            break;

        case ColliderShape::StaticPlane:
            m_shape = new btStaticPlaneShape(btVector3(0.0f, 1.0f, 0.0f), 0.0f);
            break;

        case ColliderShape::Cylinder:
            m_shape = new btCylinderShape(btVector3(m_size.x * 0.5f, m_size.y * 0.5f, m_size.x * 0.5f));
            break;

        case ColliderShape::Capsule:
            m_shape = new btCapsuleShape(m_size.x * 0.5f, Helper::Max(m_size.y - m_size.x, 0.0f));
            break;

        case ColliderShape::Cone:
            m_shape = new btConeShape(m_size.x * 0.5f, m_size.y);
            break;

        case ColliderShape::Mesh:
            // Get Renderable
            shared_ptr<Renderable> renderable = GetEntityPtr()->GetComponent<Renderable>();
            if (!renderable)
            {
                SP_LOG_WARNING("Can't construct mesh shape, there is no Renderable component attached.");
                return;
            }

            // Validate vertex count
            if (renderable->GetVertexCount() >= m_vertexLimit)
            {
                SP_LOG_WARNING("No user defined collider with more than %d vertices is allowed.", m_vertexLimit);
                return;
            }

            // Get geometry
            vector<uint32_t> indices;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            renderable->GetGeometry(&indices, &vertices);

            if (vertices.empty())
            {
                SP_LOG_WARNING("No vertices.");
                return;
            }

            // Construct hull approximation
            m_shape = new btConvexHullShape(
                (btScalar*)&vertices[0],                                 // points
                renderable->GetVertexCount(),                            // point count
                static_cast<uint32_t>(sizeof(RHI_Vertex_PosTexNorTan))); // stride

            // Optimize if requested
            if (m_shape_optimize)
            {
                auto hull = static_cast<btConvexHullShape*>(m_shape);
                hull->optimizeConvexHull();
                hull->initializePolyhedralFeatures();
            }
            break;
        }

        m_shape->setUserPointer(this);

        AddBodyToWorld();
    }
}
