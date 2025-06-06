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

//= INCLUDES =================================
#include "pch.h"
#include "PhysicsBody.h"
#include "Renderable.h"
#include "Terrain.h"
#include "../Entity.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../IO/FileStream.h"
#include "../../Physics/Physics.h"
#include "../../Geometry/GeometryProcessing.h"
SP_WARNINGS_OFF
#ifdef DEBUG
    #define _DEBUG 1
    #undef NDEBUG
#else
    #define NDEBUG 1
    #undef _DEBUG
#endif
#define PX_PHYSX_STATIC_LIB
#include <physx/PxPhysicsAPI.h>
SP_WARNINGS_ON
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
using namespace physx;
//============================

namespace spartan
{
    PhysicsBody::PhysicsBody(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mass, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction_rolling, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_restitution, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_kinematic, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_position_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotation_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_center_of_mass, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_scale, Vector3);
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
        if (m_body)
        {
            PxScene* scene = static_cast<PxScene*>(Physics::GetScene());

            // detach shape if it exists
            if (m_shape)
            {
                PxShape* shape = static_cast<PxShape*>(m_shape);
                static_cast<PxRigidActor*>(m_body)->detachShape(*shape);
                shape->release();
                m_shape = nullptr;
            }

            // remove and release body
            scene->removeActor(*static_cast<PxRigidActor*>(m_body));
            static_cast<PxRigidActor*>(m_body)->release();
            m_body = nullptr;
        }
    }

    void PhysicsBody::OnTick()
    {
        if (!m_body)
            return;
    
        // engine -> physx
        if (!Engine::IsFlagSet(EngineMode::Playing))
        {
            if (GetPosition() != GetEntity()->GetPosition())
            {
                SetPosition(GetEntity()->GetPosition());
                SetLinearVelocity(Vector3::Zero);
                SetAngularVelocity(Vector3::Zero);
            }
    
            if (GetRotation() != GetEntity()->GetRotation())
            {
                SetRotation(GetEntity()->GetRotation());
                SetLinearVelocity(Vector3::Zero);
                SetAngularVelocity(Vector3::Zero);
            }
        }
        else // physx -> engine
        {
            PxTransform pose = static_cast<PxRigidActor*>(m_body)->getGlobalPose();
            GetEntity()->SetPosition(Vector3(pose.p.x, pose.p.y, pose.p.z));
            GetEntity()->SetRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
        }
    }

    void PhysicsBody::Serialize(FileStream* stream)
    {
        stream->Write(m_mass);
        stream->Write(m_friction);
        stream->Write(m_friction_rolling);
        stream->Write(m_restitution);
        stream->Write(m_is_kinematic);
        stream->Write(m_position_lock);
        stream->Write(m_rotation_lock);
        stream->Write(uint32_t(m_shape_type));
        stream->Write(m_scale);
        stream->Write(m_center_of_mass);
    }

    void PhysicsBody::Deserialize(FileStream* stream)
    {
        stream->Read(&m_mass);
        stream->Read(&m_friction);
        stream->Read(&m_friction_rolling);
        stream->Read(&m_restitution);
        stream->Read(&m_is_kinematic);
        stream->Read(&m_position_lock);
        stream->Read(&m_rotation_lock);
        m_shape_type = PhysicsShape(stream->ReadAs<uint32_t>());
        stream->Read(&m_scale);
        stream->Read(&m_center_of_mass);

        Create();
    }

    void PhysicsBody::SetMass(float mass)
    {
        m_mass = max(mass, 0.0f);

        if (m_body)
        { 
            static_cast<PxRigidDynamic*>(m_body)->setMass(m_mass);
        }
    }

    void PhysicsBody::SetFriction(float friction)
    {
        if (!m_body || m_friction == friction)
            return;
    
        m_friction = friction;
    
        // Update the material's static friction
        PxShape* shape = static_cast<PxShape*>(m_shape);
        if (shape)
        {
            PxMaterial* material = nullptr;
            shape->getMaterials(&material, 1);
            if (material)
            {
                material->setStaticFriction(m_friction);
            }
            else
            {
                SP_LOG_WARNING("SetFriction: No material found for shape.");
            }
        }
        else
        {
            SP_LOG_WARNING("SetFriction: No shape attached to body.");
        }
    }

    void PhysicsBody::SetFrictionRolling(float frictionRolling)
    {
        if (!m_body || m_friction_rolling == frictionRolling)
            return;
    
        m_friction_rolling = frictionRolling;
    
        // Update the material's dynamic friction (used as a proxy for rolling friction)
        PxShape* shape = static_cast<PxShape*>(m_shape);
        if (shape)
        {
            PxMaterial* material = nullptr;
            shape->getMaterials(&material, 1);
            if (material)
            {
                material->setDynamicFriction(m_friction_rolling);
            }
            else
            {
                SP_LOG_WARNING("SetFrictionRolling: No material found for shape.");
            }
        }
        else
        {
            SP_LOG_WARNING("SetFrictionRolling: No shape attached to body.");
        }
    }

    void PhysicsBody::SetRestitution(float restitution)
    {
        if (!m_body || m_restitution == restitution)
            return;
    
        m_restitution = restitution;
    
        // Update the material's restitution
        PxShape* shape = static_cast<PxShape*>(m_shape);
        if (shape)
        {
            PxMaterial* material = nullptr;
            shape->getMaterials(&material, 1);
            if (material)
            {
                material->setRestitution(m_restitution);
            }
            else
            {
                SP_LOG_WARNING("SetRestitution: No material found for shape.");
            }
        }
        else
        {
            SP_LOG_WARNING("SetRestitution: No shape attached to body.");
        }
    }

    void PhysicsBody::SetIsKinematic(bool kinematic)
    {
        if (kinematic == m_is_kinematic)
            return;
    
        m_is_kinematic = kinematic;
    
        // Update kinematic flag for dynamic bodies
        PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
        if (rigid_dynamic)
        {
            rigid_dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, m_is_kinematic);
        }
        else
        {
            SP_LOG_WARNING("SetIsKinematic: Body is not dynamic, kinematic flag ignored.");
        }
    }

    void PhysicsBody::SetLinearVelocity(const Vector3& velocity) const
    {
        if (!m_body)
            return;

        PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
        if (rigid_dynamic)
        {
            rigid_dynamic->setLinearVelocity(PxVec3(velocity.x, velocity.y, velocity.z));
            rigid_dynamic->wakeUp();
        }
    }

    Vector3 PhysicsBody::GetLinearVelocity() const
    {
        if (!m_body)
            return Vector3::Zero;

        PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
        if (rigid_dynamic)
        {
            PxVec3 velocity = rigid_dynamic->getLinearVelocity();
            return Vector3(velocity.x, velocity.y, velocity.z);
        }

        return Vector3::Zero;
    }
    
	void PhysicsBody::SetAngularVelocity(const Vector3& velocity) const
    {
        if (!m_body)
            return;

        PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
        if (rigid_dynamic)
        {
            rigid_dynamic->setAngularVelocity(PxVec3(velocity.x, velocity.y, velocity.z));
            rigid_dynamic->wakeUp();
        }
    }

    void PhysicsBody::ApplyForce(const Vector3& force, PhysicsForce mode) const
    {
        if (!m_body)
            return;

        PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
        if (rigid_dynamic)
        {
            PxForceMode::Enum px_mode = (mode == PhysicsForce::Constant) ? PxForceMode::eFORCE : PxForceMode::eIMPULSE;
            rigid_dynamic->addForce(PxVec3(force.x, force.y, force.z), px_mode);
            rigid_dynamic->wakeUp();
        }
    }

    void PhysicsBody::SetPositionLock(bool lock)
    {
        SetPositionLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetPositionLock(const Vector3& lock)
    {
        if (!m_body || m_position_lock == lock)
            return;
    
        m_position_lock = lock;
    
        PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
        if (rigid_dynamic)
        {
            PxRigidDynamicLockFlags flags = rigid_dynamic->getRigidDynamicLockFlags();
            
            flags = PxRigidDynamicLockFlags(0);
            if (m_position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
            if (m_position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
            if (m_position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
            if (m_rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
            if (m_rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
            if (m_rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    
            rigid_dynamic->setRigidDynamicLockFlags(flags);
        }
        else
        {
            SP_LOG_WARNING("SetPositionLock: Body is not dynamic, position lock ignored.");
        }
    }

    void PhysicsBody::SetRotationLock(bool lock)
    {
        SetRotationLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetRotationLock(const Vector3& lock)
    {
        if (!m_body)
        {
            SP_LOG_WARNING("SetRotationLock: This call needs to happen after SetShapeType()");
            return;
        }
    
        if (m_rotation_lock == lock)
            return;
    
        m_rotation_lock = lock;
    
        PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
        if (rigid_dynamic)
        {
            PxRigidDynamicLockFlags flags = rigid_dynamic->getRigidDynamicLockFlags();
            
            flags = PxRigidDynamicLockFlags(0);
            if (m_position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
            if (m_position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
            if (m_position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
            if (m_rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
            if (m_rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
            if (m_rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
    
            rigid_dynamic->setRigidDynamicLockFlags(flags);
        }
        else
        {
            SP_LOG_WARNING("SetRotationLock: Body is not dynamic, rotation lock ignored.");
        }
    }

    void PhysicsBody::SetCenterOfMass(const Vector3& center_of_mass)
    {
        m_center_of_mass = center_of_mass;
        SetPosition(GetPosition());
    }

    Vector3 PhysicsBody::GetPosition() const
    {
        if (m_body)
        {
            PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body);
            PxTransform pose = rigid_actor->getGlobalPose();
            return Vector3(pose.p.x, pose.p.y, pose.p.z);
        }
        
        return Vector3::Zero;
    }

    void PhysicsBody::SetPosition(const Vector3& position) const
    {
        if (!m_body)
            return;

        PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body);
        PxTransform pose = rigid_actor->getGlobalPose();
        pose.p = PxVec3(position.x, position.y, position.z);
        rigid_actor->setGlobalPose(pose);
    }

    Quaternion PhysicsBody::GetRotation() const
    {
        if (m_body)
        {
            PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body);
            PxTransform pose = rigid_actor->getGlobalPose();
            return Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w);
        }
        
        return Quaternion::Identity;
    }

    void PhysicsBody::SetRotation(const Quaternion& rotation) const
    {
        if (!m_body)
            return;

        PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body);
        PxTransform pose = rigid_actor->getGlobalPose();
        pose.q = PxQuat(rotation.x, rotation.y, rotation.z, rotation.w);
        rigid_actor->setGlobalPose(pose);
    }

    void PhysicsBody::ClearForces() const
    {
        if (!m_body)
            return;
    }

    void PhysicsBody::SetScale(const Vector3& scale)
    {
        if (m_scale == scale)
            return;

        m_scale   = scale;
        m_scale.x = clamp(m_scale.x, numeric_limits<float>::min(), numeric_limits<float>::infinity());
        m_scale.y = clamp(m_scale.y, numeric_limits<float>::min(), numeric_limits<float>::infinity());
        m_scale.z = clamp(m_scale.z, numeric_limits<float>::min(), numeric_limits<float>::infinity());
    }

    void PhysicsBody::SetShapeType(PhysicsShape type, const bool replicate_hierarchy)
    {
        if (m_shape_type == type)
            return;

        m_shape_type = type;

        Create();
    }

    bool PhysicsBody::RayTraceIsGrounded() const
    {
        if (!m_body)
            return false;
    
        PxScene* scene           = static_cast<PxScene*>(Physics::GetScene());
        PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body);
    
        // get the body's AABB to find the lowest point
        PxShape* shape = nullptr;
        rigid_actor->getShapes(&shape, 1);
        if (!shape)
            return false;
    
        PxBounds3 bounds = PxShapeExt::getWorldBounds(*shape, *rigid_actor, 1.0f);
        float min_y = bounds.minimum.y;
    
        // get the body's position and adjust ray start
        PxTransform pose = rigid_actor->getGlobalPose();
        PxVec3 ray_start(pose.p.x, min_y + 0.1f, pose.p.z); // Offset 0.1 units above lowest point
    
        // define raycast parameters
        PxVec3 direction(0, -1, 0); // downward
        PxReal max_distance = 0.2f; // raycast 0.2 units down
        PxRaycastBuffer hit;
    
        // filter to exclude self-collision
        struct RaycastFilterCallback : public PxQueryFilterCallback
        {
            PxRigidActor* self_actor;
            RaycastFilterCallback(PxRigidActor* actor) : self_actor(actor) {}
            PxQueryHitType::Enum preFilter(const PxFilterData& filterData, const PxShape* shape, const PxRigidActor* actor, PxHitFlags& queryFlags) override
            {
                return (actor == self_actor) ? PxQueryHitType::eNONE : PxQueryHitType::eBLOCK;
            }
            PxQueryHitType::Enum postFilter(const PxFilterData& filterData, const PxQueryHit& hit, const PxShape* shape, const PxRigidActor* actor) override
            {
                return PxQueryHitType::eBLOCK;
            }
        };
        RaycastFilterCallback filter_callback(rigid_actor);
    
        // perform raycast
        PxQueryFilterData filter_data;
        filter_data.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC | PxQueryFlag::eANY_HIT;
    
        return scene->raycast(ray_start, direction, max_distance, hit, PxHitFlag::eDEFAULT, filter_data, &filter_callback);
    }
    
    float PhysicsBody::GetCapsuleVolume()
    {
        // total volume is the sum of the cylinder and two hemispheres
        float radius = GetCapsuleRadius();    // radius is max of x and z scale divided by 2
        float half_height = m_scale.y * 0.5f; // half the height of the cylindrical part

        // cylinder volume: π * r² * h
        float cylinder_volume = math::pi * radius * radius * (m_scale.y - 2 * radius);

        // sphere volume (two hemispheres = one full sphere): (4/3) * π * r³
        float sphere_volume = (4.0f / 3.0f) * math::pi * radius * radius * radius;

        // total volume
        return cylinder_volume + sphere_volume;
    }

    float PhysicsBody::GetCapsuleRadius()
    {
        return max(m_scale.x, m_scale.z) * 0.5f;
    }
    
    void PhysicsBody::Create()
    {
        // get physx pointers
        PxPhysics* physics        = static_cast<PxPhysics*>(Physics::GetPhysics());
        PxScene* scene            = static_cast<PxScene*>(Physics::GetScene());
        PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body);
    
        // determine if a new body is needed (no body or mass-based type change)
        bool is_static      = m_mass == 0.0f;
        bool needs_new_body = !m_body || (is_static && !rigid_actor->is<PxRigidStatic>()) || (!is_static && !rigid_actor->is<PxRigidDynamic>());

        if (needs_new_body)
        {
            // clean up existing body if it exists
            if (m_body)
            {
                // detach and release shape
                if (m_shape)
                {
                    PxShape* shape = static_cast<PxShape*>(m_shape);
                    static_cast<PxRigidActor*>(m_body)->detachShape(*shape);
                    shape->release();
                    m_shape = nullptr;
                }
                // remove and release body
                scene->removeActor(*static_cast<PxRigidActor*>(m_body));
                static_cast<PxRigidActor*>(m_body)->release();
                m_body = nullptr;
            }
    
            // create transform from entity
            PxTransform pose(
                PxVec3(GetEntity()->GetPosition().x, GetEntity()->GetPosition().y, GetEntity()->GetPosition().z),
                PxQuat(GetEntity()->GetRotation().x, GetEntity()->GetRotation().y, GetEntity()->GetRotation().z, GetEntity()->GetRotation().w)
            );
    
            // create body based on mass
            if (is_static)
            {
                m_body = physics->createRigidStatic(pose);
            }
            else
            {
                m_body = physics->createRigidDynamic(pose);
                PxRigidDynamic* rigid_dynamic = static_cast<PxRigidDynamic*>(m_body);
                rigid_dynamic->setMass(m_mass);
                if (m_center_of_mass != Vector3::Zero)
                {
                    PxVec3 p = PxVec3(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                    PxRigidBodyExt::setMassAndUpdateInertia(*rigid_dynamic, m_mass, &p);
                }
                rigid_dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, m_is_kinematic);
                PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
                if (m_position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
                if (m_position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
                if (m_position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
                if (m_rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
                if (m_rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
                if (m_rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
                rigid_dynamic->setRigidDynamicLockFlags(flags);
            }
    
            // add to scene
            scene->addActor(*static_cast<PxRigidActor*>(m_body));
            rigid_actor = static_cast<PxRigidActor*>(m_body);
        }
    
        // update existing dynamic body properties
        if (!needs_new_body && rigid_actor->is<PxRigidDynamic>())
        {
            PxRigidDynamic* rigid_dynamic = static_cast<PxRigidDynamic*>(m_body);
            rigid_dynamic->setMass(m_mass);
            if (m_center_of_mass != Vector3::Zero)
            {
                PxVec3 p = PxVec3(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                PxRigidBodyExt::setMassAndUpdateInertia(*rigid_dynamic, m_mass, &p);
            }
            rigid_dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, m_is_kinematic);
            PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
            if (m_position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
            if (m_position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
            if (m_position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
            if (m_rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
            if (m_rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
            if (m_rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
            rigid_dynamic->setRigidDynamicLockFlags(flags);
        }
    
        // remove existing shape
        if (m_shape)
        {
            PxShape* shape = static_cast<PxShape*>(m_shape);
            rigid_actor->detachShape(*shape);
            shape->release();
            m_shape = nullptr;
        }
    
        // create material
        PxMaterial* material = physics->createMaterial(m_friction, m_friction_rolling, m_restitution);
    
        // create new shape based on shape type
        switch (m_shape_type)
        {
            case PhysicsShape::Box:
            {
                PxBoxGeometry geometry(m_scale.x * 0.5f, m_scale.y * 0.5f, m_scale.z * 0.5f);
                m_shape = physics->createShape(geometry, *material);
                break;
            }
            case PhysicsShape::Sphere:
            {
                float radius = max(max(m_scale.x, m_scale.y), m_scale.z) * 0.5f;
                PxSphereGeometry geometry(radius);
                m_shape = physics->createShape(geometry, *material);
                break;
            }
            case PhysicsShape::StaticPlane:
            {
                PxPlaneGeometry geometry;
                m_shape = physics->createShape(geometry, *material);
                static_cast<PxShape*>(m_shape)->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxIdentity)));
                break;
            }
            case PhysicsShape::Capsule:
            {
                float radius      = max(m_scale.x, m_scale.z) * 0.5f;
                float half_height = m_scale.y * 0.5f;
                PxCapsuleGeometry geometry(radius, half_height);
                m_shape = physics->createShape(geometry, *material);
                static_cast<PxShape*>(m_shape)->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
                break;
            }
            case PhysicsShape::Terrain:
            {
                Terrain* terrain = GetEntity()->GetComponent<Terrain>();
                if (!terrain)
                {
                    SP_LOG_ERROR("No Terrain component found for terrain shape");
                    break;
                }
            
                // get heightmap data
                float* height_map = terrain->GetHeightData();
                uint32_t width    = terrain->GetWidth();
                uint32_t depth    = terrain->GetHeight();
            
                // compute terrain scale based on physical dimensions (from generate_positions)
                uint32_t density     = terrain->GetDensity();
                uint32_t scale       = terrain->GetScale();
                uint32_t base_width  = (width - 1) / density + 1;
                uint32_t base_height = (depth - 1) / density + 1;
                float extent_x = static_cast<float>(base_width - 1) * scale;
                float extent_z = static_cast<float>(base_height - 1) * scale;
                Vector3 terrain_scale(
                    extent_x, // x scale (width in world units)
                    1.0f,     // y scale (heights are in world units)
                    extent_z  // z scale (depth in world units)
                );
            
                // compute height range
                float height_range = terrain->GetMaxY() - terrain->GetMinY();
                if (height_range == 0.0f)
                {
                    SP_LOG_WARNING("Terrain height range is zero, using 1.0 to avoid division by zero");
                    height_range = 1.0f;
                }
            
                // Create height field samples
                std::vector<PxHeightFieldSample> samples(width * depth);
                for (uint32_t z = 0; z < depth; ++z)
                {
                    for (uint32_t x = 0; x < width; ++x)
                    {
                        uint32_t index = z * width + x;
                        PxHeightFieldSample& sample = samples[index];
            
                        // Map height directly to PxI16 (heights are in world units)
                        float height            = height_map[index];
                        float normalized_height = (height - terrain->GetMinY()) / height_range;
                        sample.height           = static_cast<PxI16>(normalized_height * 65535.0f - 32768.0f); // Map to [-32768, 32767]
            
                        // Set material indices (default to 0)
                        sample.materialIndex0 = 0;
                        sample.materialIndex1 = 0;
                    }
                }
            
                // Create height field description
                PxHeightFieldDesc height_field_desc;
                height_field_desc.nbRows         = width; // z-direction
                height_field_desc.nbColumns      = depth; // x-direction
                height_field_desc.samples.data   = samples.data();
                height_field_desc.samples.stride = sizeof(PxHeightFieldSample);
            
                // Cooking parameters
                PxTolerancesScale px_scale;
                px_scale.length = 1.0f; // 1 unit = 1 meter
                px_scale.speed = Physics::GetGravity().y; // gravity in meters per second
                PxCookingParams params(px_scale);
                params.meshPreprocessParams = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES);
                params.meshWeldTolerance = 0.05f; // merge vertices within 5cm
                params.buildGPUData = false;
            
                // Create height field
                PxInsertionCallback* insertion_callback = PxGetStandaloneInsertionCallback();
                PxHeightField* height_field = PxCreateHeightField(height_field_desc, *insertion_callback);
                if (!height_field)
                {
                    SP_LOG_ERROR("Failed to create height field");
                    break;
                }
            
                // Create height field geometry
                PxHeightFieldGeometry geometry(
                    height_field,
                    PxMeshGeometryFlags(),
                    height_range / 65535.0f, // height scale to map PxI16 back to world units
                    terrain_scale.x / (width > 1 ? width - 1 : 1), // row scale (x-direction)
                    terrain_scale.z / (depth > 1 ? depth - 1 : 1)  // column scale (z-direction)
                );
            
                // Create shape
                m_shape = physics->createShape(geometry, *material);
            
                // Release height field (shape takes ownership)
                height_field->release();
            
                // Adjust local pose to center the terrain
                if (m_shape)
                {
                    PxVec3 offset(
                        -terrain_scale.x * 0.5f, // center x
                        0.0f,                   // height is handled by geometry
                        -terrain_scale.z * 0.5f // center z
                    );
                    static_cast<PxShape*>(m_shape)->setLocalPose(PxTransform(offset));
                }
            
                break;
            }
            case PhysicsShape::Mesh:
            {
                Renderable* renderable = GetEntity()->GetComponent<Renderable>();
                if (!renderable)
                {
                    SP_LOG_ERROR("No Renderable component found for mesh shape");
                    break;
                }
            
                // get geometry from Renderable
                vector<uint32_t> indices;
                vector<RHI_Vertex_PosTexNorTan> vertices;
                renderable->GetGeometry(&indices, &vertices);
            
                if (vertices.empty() || indices.empty())
                {
                    SP_LOG_ERROR("Empty vertex or index data for mesh shape");
                    break;
                }

                // imported meshes lack duplicate vertices, but engine-generated meshes like cubes may have them to ensure unique
                // normals for flat shading, avoiding normal interpolation. We remove duplicates here to meet PhysX requirements
                geometry_processing::remove_duplicate_vertices(vertices, indices);

                // convert vertices to physx format
                vector<PxVec3> px_vertices;
                px_vertices.reserve(vertices.size());
                for (const auto& vertex : vertices)
                {
                    PxVec3 scaled_vertex(
                        vertex.pos[0] * m_scale.x,
                        vertex.pos[1] * m_scale.y,
                        vertex.pos[2] * m_scale.z
                    );
                    px_vertices.emplace_back(scaled_vertex);
                }
            
                // cooking parameters
                PxTolerancesScale scale;
                scale.length = 1.0f;                    // 1 unit = 1 meter
                scale.speed  = Physics::GetGravity().y; // gravity is in meters per second
                PxCookingParams params(scale);
                params.meshPreprocessParams           = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES);
                params.meshWeldTolerance              = 0.05f;   // merge vertices within 5cm  
                params.meshAreaMinLimit               = 0.0001f; // remove very small triangles
                params.meshEdgeLengthMaxLimit         = 500.0f;  // warn about large edges
                params.convexMeshCookingType          = PxConvexMeshCookingType::eQUICKHULL;
                params.buildGPUData                   = false;
                params.suppressTriangleMeshRemapTable = false;
                params.buildTriangleAdjacencies       = false;
            
                // create physx mesh
                PxShape* shape = nullptr;
                PxInsertionCallback* insertion_callback = PxGetStandaloneInsertionCallback();
                if (m_mass == 0.0f) // static: triangle mesh
                {
                    // ensure indices are in groups of 3 for triangles
                    if (indices.size() % 3 != 0)
                    {
                        SP_LOG_ERROR("Index count must be a multiple of 3 for triangle mesh");
                        break;
                    }
            
                    // create triangle mesh description
                    PxTriangleMeshDesc mesh_desc = {};
                    mesh_desc.points.count       = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride      = sizeof(PxVec3);
                    mesh_desc.points.data        = px_vertices.data();
                    mesh_desc.triangles.count    = static_cast<PxU32>(indices.size() / 3);
                    mesh_desc.triangles.stride   = 3 * sizeof(PxU32);
                    mesh_desc.triangles.data     = indices.data();
            
                    // validate triangle mesh
                    if (!PxValidateTriangleMesh(params, mesh_desc))
                    {
                        SP_LOG_ERROR("Triangle mesh validation failed");
                        break;
                    }
            
                    // create triangle mesh
                    PxTriangleMeshCookingResult::Enum condition;
                    PxTriangleMesh* triangle_mesh = PxCreateTriangleMesh(params, mesh_desc, *insertion_callback, &condition);
                    if (!triangle_mesh || condition != PxTriangleMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create triangle mesh: %d", condition);
                        if (triangle_mesh)
                            triangle_mesh->release();
                        break;
                    }
            
                    // create triangle mesh geometry
                    PxTriangleMeshGeometry geometry(triangle_mesh);
                    shape = physics->createShape(geometry, *material);
                    triangle_mesh->release(); // shape takes ownership
                }
                else // dynamic: convex mesh
                {
                    // create convex mesh description
                    PxConvexMeshDesc mesh_desc;
                    mesh_desc.points.count = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride = sizeof(PxVec3);
                    mesh_desc.points.data = px_vertices.data();
                    mesh_desc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
            
                    // validate convex mesh
                    if (!PxValidateConvexMesh(params, mesh_desc))
                    {
                        SP_LOG_ERROR("Convex mesh validation failed");
                        break;
                    }
            
                    // create convex mesh
                    PxConvexMeshCookingResult::Enum condition;
                    PxConvexMesh* convex_mesh = PxCreateConvexMesh(params, mesh_desc, *insertion_callback, &condition);
                    if (!convex_mesh || condition != PxConvexMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create convex mesh: %d", condition);
                        if (convex_mesh)
                            convex_mesh->release();
                        break;
                    }
            
                    // create convex mesh geometry
                    PxConvexMeshGeometry geometry(convex_mesh);
                    shape = physics->createShape(geometry, *material);
                    convex_mesh->release(); // shape takes ownership
                }
            
                if (shape)
                {
                    m_shape = shape;
                }
                else
                {
                    SP_LOG_ERROR("Failed to create mesh shape");
                }
                break;
            }
        }
    
        // attach shape to body
        if (m_shape)
        {
            PxShape* shape = static_cast<PxShape*>(m_shape);
            shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
            rigid_actor->attachShape(*shape);
            shape->release();
            m_shape = nullptr;
        }
        else
        {
            SP_LOG_ERROR("failed to create shape for type %d", m_shape_type);
        }
    
        // release material
        material->release();
    }
}
