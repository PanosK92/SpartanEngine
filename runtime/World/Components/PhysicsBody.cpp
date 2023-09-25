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

//= INCLUDES =========================================================
#include "pch.h"
#include "PhysicsBody.h"
#include "Transform.h"
#include "Constraint.h"
#include "Renderable.h"
#include "Terrain.h"
#include "../RHI/RHI_Vertex.h"
#include "../RHI/RHI_Texture.h"
#include "../Entity.h"
#include "../../Physics/Physics.h"
#include "../../Physics/BulletPhysicsHelper.h"
#include "../../IO/FileStream.h"
SP_WARNINGS_OFF
#include "BulletDynamics/Dynamics/btRigidBody.h"
#include "BulletCollision/CollisionShapes/btSphereShape.h"
#include "BulletCollision/CollisionShapes/btStaticPlaneShape.h"
#include "BulletCollision/CollisionShapes/btCylinderShape.h"
#include "BulletCollision/CollisionShapes/btCapsuleShape.h"
#include "BulletCollision/CollisionShapes/btConeShape.h"
#include "BulletCollision/CollisionShapes/btHeightfieldTerrainShape.h"
#include "BulletCollision/CollisionShapes/btTriangleMesh.h"
#include "BulletCollision/CollisionShapes/btBvhTriangleMeshShape.h"
#include "BulletCollision/CollisionShapes/btConvexHullShape.h"
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

        // update from engine, ENGINE -> BULLET
        void getWorldTransform(btTransform& worldTrans) const override
        {
            const Vector3 lastPos    = m_rigidBody->GetTransform()->GetPosition();
            const Quaternion lastRot = m_rigidBody->GetTransform()->GetRotation();

            worldTrans.setOrigin(ToBtVector3(lastPos + lastRot * m_rigidBody->GetCenterOfMass()));
            worldTrans.setRotation(ToBtQuaternion(lastRot));
        }

        // update from bullet, BULLET -> ENGINE
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
        m_shape_type       = PhysicsShape::Box;
        m_center_of_mass   = Vector3::Zero;
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
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_shape_type, SetShapeType, PhysicsShape);

        if (GetEntityPtr()->GetComponent<Renderable>())
        {
            m_shape_type = PhysicsShape::MeshConvexHull;
        }

        if (GetEntityPtr()->GetComponent<Terrain>())
        {
            m_shape_type = PhysicsShape::Terrain;
        }

        UpdateShape();
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
        if (!static_cast<btRigidBody*>(m_rigid_body)->isActive() || !Engine::IsFlagSet(EngineMode::Game))
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
        static_cast<btRigidBody*>(m_rigid_body)->setFriction(friction);
    }

    void PhysicsBody::SetFrictionRolling(float frictionRolling)
    {
        if (!m_rigid_body || m_friction_rolling == frictionRolling)
            return;

        m_friction_rolling = frictionRolling;
        static_cast<btRigidBody*>(m_rigid_body)->setRollingFriction(frictionRolling);
    }

    void PhysicsBody::SetRestitution(float restitution)
    {
        if (!m_rigid_body || m_restitution == restitution)
            return;

        m_restitution = restitution;
        static_cast<btRigidBody*>(m_rigid_body)->setRestitution(restitution);
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

        static_cast<btRigidBody*>(m_rigid_body)->setLinearVelocity(ToBtVector3(velocity));
        if (velocity != Vector3::Zero && activate)
        {
            Activate();
        }
    }

    void PhysicsBody::SetAngularVelocity(const Vector3& velocity, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        static_cast<btRigidBody*>(m_rigid_body)->setAngularVelocity(ToBtVector3(velocity));
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
            static_cast<btRigidBody*>(m_rigid_body)->applyCentralForce(ToBtVector3(force));
        }
        else if (mode == PhysicsForce::Impulse)
        {
            static_cast<btRigidBody*>(m_rigid_body)->applyCentralImpulse(ToBtVector3(force));
        }
    }

    void PhysicsBody::ApplyForceAtPosition(const Vector3& force, const Vector3& position, PhysicsForce mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == PhysicsForce::Constant)
        {
            static_cast<btRigidBody*>(m_rigid_body)->applyForce(ToBtVector3(force), ToBtVector3(position));
        }
        else if (mode == PhysicsForce::Impulse)
        {
            static_cast<btRigidBody*>(m_rigid_body)->applyImpulse(ToBtVector3(force), ToBtVector3(position));
        }
    }

    void PhysicsBody::ApplyTorque(const Vector3& torque, PhysicsForce mode) const
    {
        if (!m_rigid_body)
            return;

        Activate();

        if (mode == PhysicsForce::Constant)
        {
            static_cast<btRigidBody*>(m_rigid_body)->applyTorque(ToBtVector3(torque));
        }
        else if (mode == PhysicsForce::Impulse)
        {
            static_cast<btRigidBody*>(m_rigid_body)->applyTorqueImpulse(ToBtVector3(torque));
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
        static_cast<btRigidBody*>(m_rigid_body)->setLinearFactor(ToBtVector3(Vector3::One - lock));
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
        static_cast<btRigidBody*>(m_rigid_body)->setAngularFactor(ToBtVector3(Vector3::One - lock));
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
            const btTransform& transform = static_cast<btRigidBody*>(m_rigid_body)->getWorldTransform();
            return ToVector3(transform.getOrigin()) - ToQuaternion(transform.getRotation()) * m_center_of_mass;
        }
    
        return Vector3::Zero;
    }

    void PhysicsBody::SetPosition(const Vector3& position, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        // set position to world transform
        btTransform& transform_world = static_cast<btRigidBody*>(m_rigid_body)->getWorldTransform();
        transform_world.setOrigin(ToBtVector3(position + ToQuaternion(transform_world.getRotation()) * m_center_of_mass));

        // set position to interpolated world transform
        btTransform transform_world_interpolated = static_cast<btRigidBody*>(m_rigid_body)->getInterpolationWorldTransform();
        transform_world_interpolated.setOrigin(transform_world.getOrigin());
        static_cast<btRigidBody*>(m_rigid_body)->setInterpolationWorldTransform(transform_world_interpolated);

        if (activate)
        {
            Activate();
        }
    }

    Quaternion PhysicsBody::GetRotation() const
    {
        return m_rigid_body ? ToQuaternion(static_cast<btRigidBody*>(m_rigid_body)->getWorldTransform().getRotation()) : Quaternion::Identity;
    }

    void PhysicsBody::SetRotation(const Quaternion& rotation, const bool activate /*= true*/) const
    {
        if (!m_rigid_body)
            return;

        // set rotation to world transform
        const Vector3 oldPosition = GetPosition();
        btTransform& transform_world = static_cast<btRigidBody*>(m_rigid_body)->getWorldTransform();
        transform_world.setRotation(ToBtQuaternion(rotation));
        if (m_center_of_mass != Vector3::Zero)
        {
            transform_world.setOrigin(ToBtVector3(oldPosition + rotation * m_center_of_mass));
        }

        // set rotation to interpolated world transform
        btTransform interpTrans = static_cast<btRigidBody*>(m_rigid_body)->getInterpolationWorldTransform();
        interpTrans.setRotation(transform_world.getRotation());
        if (m_center_of_mass != Vector3::Zero)
        {
            interpTrans.setOrigin(transform_world.getOrigin());
        }
        static_cast<btRigidBody*>(m_rigid_body)->setInterpolationWorldTransform(interpTrans);

        static_cast<btRigidBody*>(m_rigid_body)->updateInertiaTensor();

        if (activate)
        {
            Activate();
        }
    }

    void PhysicsBody::ClearForces() const
    {
        if (!m_rigid_body)
            return;

        static_cast<btRigidBody*>(m_rigid_body)->clearForces();
    }

    void PhysicsBody::Activate() const
    {
        if (!m_rigid_body)
            return;

        if (m_mass > 0.0f)
        {
            static_cast<btRigidBody*>(m_rigid_body)->activate(true);
        }
    }

    void PhysicsBody::Deactivate() const
    {
        if (!m_rigid_body)
            return;

        static_cast<btRigidBody*>(m_rigid_body)->setActivationState(WANTS_DEACTIVATION);
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
        bool is_static           = m_mass == 0.0f; // static objects don't have inertia
        bool is_supported_shape  = m_shape_type != PhysicsShape::Mesh; // shapes like btBvhTriangleMeshShape don't support local inertia
        bool support_inertia     = !is_static && is_supported_shape;
        if (m_rigid_body && support_inertia && m_shape)
        {
            local_intertia = static_cast<btRigidBody*>(m_rigid_body)->getLocalInertia();
            static_cast<btCollisionShape*>(m_shape)->calculateLocalInertia(m_mass, local_intertia);
        }
        
        RemoveBodyFromWorld();

        // construct a new rigid body
        {
            SP_ASSERT(m_shape != nullptr);

            // construction info
            btRigidBody::btRigidBodyConstructionInfo construction_info(0.0f, nullptr, nullptr);
            construction_info.m_mass             = m_mass;
            construction_info.m_friction         = m_friction;
            construction_info.m_rollingFriction  = m_friction_rolling;
            construction_info.m_restitution      = m_restitution;
            construction_info.m_collisionShape   = static_cast<btCollisionShape*>(m_shape);
            construction_info.m_localInertia     = local_intertia;
            construction_info.m_motionState      = new MotionState(this); // we delete this manually later

            m_rigid_body = new btRigidBody(construction_info);
            static_cast<btRigidBody*>(m_rigid_body)->setUserPointer(this);
        }

        // reapply constraint positions for new center of mass shift
        for (Constraint* constraint : m_constraints)
        {
            constraint->ApplyFrames();
        }

        // flags
        {
            int flags = static_cast<btRigidBody*>(m_rigid_body)->getCollisionFlags();

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

                static_cast<btRigidBody*>(m_rigid_body)->setCollisionFlags(flags);
                static_cast<btRigidBody*>(m_rigid_body)->forceActivationState(m_is_kinematic ? DISABLE_DEACTIVATION : ISLAND_SLEEPING);
                static_cast<btRigidBody*>(m_rigid_body)->setDeactivationTime(DEFAULT_DEACTIVATION_TIME);
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

                static_cast<btRigidBody*>(m_rigid_body)->setFlags(flags);

                btVector3 gravity = m_use_gravity ? ToBtVector3(m_gravity) : btVector3(0.0f, 0.0f, 0.0f);
                static_cast<btRigidBody*>(m_rigid_body)->setGravity(gravity);
            }
        }

        // position
        SetPosition(GetTransform()->GetPosition());

        // rotation
        SetRotation(GetTransform()->GetRotation());

        // position and rotation locks
        SetPositionLock(m_position_lock);
        SetRotationLock(m_rotation_lock);

        // add to world
        Physics::AddBody(static_cast<btRigidBody*>(m_rigid_body));

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

    void PhysicsBody::RemoveBodyFromWorld()
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
                Physics::RemoveBody(reinterpret_cast<btRigidBody*&>(m_rigid_body));
                m_in_world = false;
            }

            delete static_cast<btRigidBody*>(m_rigid_body)->getMotionState();
            delete static_cast<btRigidBody*>(m_rigid_body);
            m_rigid_body = nullptr;
        }
    }

    void PhysicsBody::SetBoundingBox(const Vector3& bounding_box)
    {
        if (m_size == bounding_box)
            return;

        m_size   = bounding_box;
        m_size.x = Helper::Clamp(m_size.x, Helper::EPSILON, INFINITY);
        m_size.y = Helper::Clamp(m_size.y, Helper::EPSILON, INFINITY);
        m_size.z = Helper::Clamp(m_size.z, Helper::EPSILON, INFINITY);

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

    void PhysicsBody::UpdateShape()
    {
        // delete old shape
        if (m_shape)
        {
            delete static_cast<btCollisionShape*>(m_shape);
            m_shape = nullptr;
        }

        // get common prerequisites for certain shapes
        vector<uint32_t> indices;
        vector<RHI_Vertex_PosTexNorTan> vertices;
        shared_ptr<Renderable> renderable = nullptr;
        if (m_shape_type == PhysicsShape::Mesh || m_shape_type == PhysicsShape::MeshConvexHull)
        {
            // get renderable
            renderable = GetEntityPtr()->GetComponent<Renderable>();
            if (!renderable)
            {
                SP_LOG_WARNING("For a mesh shape to be constructed, there needs to be a Renderable component");
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

        Vector3 size = m_size * m_entity_ptr->GetTransform()->GetScale();

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
                m_shape = new btCapsuleShape(size.x * 0.5f, Helper::Max(size.y - size.x, 0.0f));
                break;

            case PhysicsShape::Cone:
                m_shape = new btConeShape(size.x * 0.5f, size.y);
                break;

            case PhysicsShape::Terrain:
            {
                Terrain* terrain = GetEntityPtr()->GetComponent<Terrain>().get();
                if (!terrain)
                {
                    SP_LOG_WARNING("For a terrain shape to be constructed, there needs to be a Terrain component");
                    return;
                }

                terrain_width  = terrain->GetHeightMap()->GetWidth();
                terrain_length = terrain->GetHeightMap()->GetHeight();

                btHeightfieldTerrainShape* shape = new btHeightfieldTerrainShape(
                    terrain_width,            // width
                    terrain_length,           // length
                    terrain->GetHeightData(), // data - row major
                    1.0f,                     // height scale
                    terrain->GetMinY(),       // min height
                    terrain->GetMaxY(),       // max height
                    1,                        // Up axis (0=x, 1=y, 2=z)
                    PHY_FLOAT,                // Data type
                    false                     // Flip quad edges or not
                );
                
                shape->setLocalScaling(ToBtVector3(size));
                m_shape = shape;

                break;
            }

            case PhysicsShape::Mesh:
            {
                btTriangleMesh* shape = new btTriangleMesh();
                for (uint32_t i = 0; i < static_cast<uint32_t>(indices.size()); i += 3)
                {
                    btVector3 vertex0(vertices[indices[i]].pos[0],     vertices[indices[i]].pos[1],     vertices[indices[i]].pos[2]);
                    btVector3 vertex1(vertices[indices[i + 1]].pos[0], vertices[indices[i + 1]].pos[1], vertices[indices[i + 1]].pos[2]);
                    btVector3 vertex2(vertices[indices[i + 2]].pos[0], vertices[indices[i + 2]].pos[1], vertices[indices[i + 2]].pos[2]);
                    shape->addTriangle(vertex0, vertex1, vertex2);
                }
                shape->setScaling(ToBtVector3(size));

                m_shape = new btBvhTriangleMeshShape(shape, true);
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
                static_cast<btConvexHullShape*>(m_shape)->initializePolyhedralFeatures();
                break;
            }
        }

        static_cast<btCollisionShape*>(m_shape)->setUserPointer(this);

        // re-add the body to the world so it's re-created with the new shape
        AddBodyToWorld();
    }
}
