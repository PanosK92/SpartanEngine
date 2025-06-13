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
#include "Camera.h"
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
    namespace
    {
        void* controller_manager = nullptr;
    }

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
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_body_type, SetBodyType, BodyType);
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
        if (m_controller)
        {
            // update entity position from controller
            if (Engine::IsFlagSet(EngineMode::Playing))
            {
                // apply gravity
                PxVec3 displacement(0.0f,  Physics::GetGravity().y * Timer::GetDeltaTimeSec(), 0.0f);
                PxControllerFilters filters;
                filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
                static_cast<PxCapsuleController*>(m_controller)->move(displacement, 0.001f, Timer::GetDeltaTimeSec(), filters);

                // update entity position from controller
                PxExtendedVec3 pos = static_cast<PxCapsuleController*>(m_controller)->getPosition();
                GetEntity()->SetPosition(Vector3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)));
            }
            else
            {
                // update controller position from entity
                Vector3 entity_pos = GetEntity()->GetPosition();
                static_cast<PxCapsuleController*>(m_controller)->setPosition(PxExtendedVec3(entity_pos.x, entity_pos.y, entity_pos.z));
            }
        }
        else if (PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body))
        {
            // existing rigid body logic
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                PxTransform pose = rigid_actor->getGlobalPose();
                Vector3 current_position(pose.p.x, pose.p.y, pose.p.z);
                Quaternion current_rotation(pose.q.x, pose.q.y, pose.q.z, pose.q.w);
                Vector3 entity_position = GetEntity()->GetPosition();
                Quaternion entity_rotation = GetEntity()->GetRotation();

                if (current_position != entity_position)
                {
                    pose.p = PxVec3(entity_position.x, entity_position.y, entity_position.z);
                    rigid_actor->setGlobalPose(pose);
                    SetLinearVelocity(Vector3::Zero);
                    SetAngularVelocity(Vector3::Zero);
                }

                if (current_rotation != entity_rotation)
                {
                    pose.q = PxQuat(entity_rotation.x, entity_rotation.y, entity_rotation.z, entity_rotation.w);
                    rigid_actor->setGlobalPose(pose);
                    SetLinearVelocity(Vector3::Zero);
                    SetAngularVelocity(Vector3::Zero);
                }
            }
            else
            {
                PxTransform pose = rigid_actor->getGlobalPose();
                GetEntity()->SetPosition(Vector3(pose.p.x, pose.p.y, pose.p.z));
                GetEntity()->SetRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
            }
        }

        // distance-based removal for static bodies
        if (m_mass == 0.0f && m_body_type != BodyType::Controller)
        {
            if (Camera* camera = World::GetCamera())
            {
                const Vector3 camera_pos        = camera->GetEntity()->GetPosition();
                const Vector3 body_pos          = GetEntity()->GetComponent<Renderable>()->GetBoundingBox().GetClosestPoint(camera_pos);
                const float distance_camera     = Vector3::Distance(camera_pos, body_pos);
                const float distance_deactivate = 30.0f;
                const float distance_activate   = 15.0f;

                if (m_body && distance_camera > distance_deactivate)
                {
                    OnRemove();
                }
                else if (!m_body && distance_camera <= distance_activate)
                {
                    Create();
                }
            }
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
        stream->Write(uint32_t(m_body_type));
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
        m_body_type = BodyType(stream->ReadAs<uint32_t>());
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
    
        // update the material's static friction
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
    
        // update the material's dynamic friction (used as a proxy for rolling friction)
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
    
        // update the material's restitution
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

    void PhysicsBody::SetLinearVelocity(const Vector3& velocity) const
    {
        if (!m_body || m_body_type == BodyType::Controller)
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
        if (!m_body || m_body_type == BodyType::Controller)
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
        if (m_body_type == BodyType::Controller)
        {
            SP_WARNINGS_ON("Don't call ApplyForce on a player controller, call Move() instead");
            return;
        }

        if (m_body)
        {
            PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
            if (rigid_dynamic)
            {
                PxForceMode::Enum px_mode = (mode == PhysicsForce::Constant) ? PxForceMode::eFORCE : PxForceMode::eIMPULSE;
                rigid_dynamic->addForce(PxVec3(force.x, force.y, force.z), px_mode);
                rigid_dynamic->wakeUp();
            }
        }
    }

    void PhysicsBody::SetPositionLock(bool lock)
    {
        SetPositionLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetPositionLock(const Vector3& lock)
    {
        if (!m_body || m_position_lock == lock || m_body_type == BodyType::Controller)
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
        if (!m_body || m_body_type == BodyType::Controller)
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
        if (m_body_type == BodyType::Controller)
            return;

        m_center_of_mass = center_of_mass;
        if (m_body)
        {
            PxRigidDynamic* rigid_dynamic = static_cast<PxRigidActor*>(m_body)->is<PxRigidDynamic>();
            if (rigid_dynamic && m_center_of_mass != Vector3::Zero)
            {
                PxVec3 p = PxVec3(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                PxRigidBodyExt::setMassAndUpdateInertia(*rigid_dynamic, m_mass, &p);
            }
        }
    }

    void PhysicsBody::SetBodyType(BodyType type)
    {
        if (m_body_type == type)
            return;

        m_body_type = type;
        Create();
    }

    bool PhysicsBody::RayTraceIsGrounded() const
    {
        PxScene* scene = static_cast<PxScene*>(Physics::GetScene());
        
        if (m_body_type == BodyType::Controller)
        {
            if (!m_controller)
                return false;
    
            PxControllerState state;
            static_cast<PxController*>(m_controller)->getState(state);
            return state.collisionFlags & PxControllerCollisionFlag::eCOLLISION_DOWN;
        }
        else
        {
            if (!m_body)
                return false;
    
            auto perform_raycast = [](PxScene* scene, const Vector3& ray_start, void* actor_to_exclude) -> bool
            {
                PxVec3 px_ray_start = PxVec3(ray_start.x, ray_start.y, ray_start.z);
                PxVec3 direction(0, -1, 0);
                PxReal max_distance = 1.8f; // average male height (Greece)
    
                const PxU32 max_hits = 10;
                PxRaycastHit hit_buffer[max_hits];
                PxRaycastBuffer hit(hit_buffer, max_hits);
                PxQueryFilterData filter_data;
                filter_data.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
    
                bool hit_found = scene->raycast(px_ray_start, direction, max_distance, hit, PxHitFlag::eDEFAULT, filter_data);
                bool is_grounded = false;
                if (hit_found)
                {
                    if (hit.hasBlock && hit.block.actor != actor_to_exclude && hit.block.distance > 0.001f)
                    {
                        is_grounded = true;
                    }
                    else if (!is_grounded && hit.nbTouches > 0)
                    {
                        for (PxU32 i = 0; i < hit.nbTouches; ++i)
                        {
                            const PxRaycastHit& current_hit = hit.getTouch(i);
                            if (current_hit.actor != actor_to_exclude && current_hit.distance > 0.001f)
                            {
                                is_grounded = true;
                                break;
                            }
                        }
                    }
                }
                return is_grounded;
            };
    
            PxRigidActor* rigid_actor = static_cast<PxRigidActor*>(m_body);
            return perform_raycast(scene, GetEntity()->GetPosition(), rigid_actor);
        }
    }

    float PhysicsBody::GetCapsuleVolume()
    {
        // total volume is the sum of the cylinder and two hemispheres
        float radius      = GetCapsuleRadius(); // radius is max of x and z scale divided by 2
        Vector3 scale     = GetEntity()->GetScale();
        float half_height = scale.y * 0.5f;   // half the height of the cylindrical part

        // cylinder volume: π * r² * h
        float cylinder_volume = math::pi * radius * radius * (scale.y - 2 * radius);

        // sphere volume (two hemispheres = one full sphere): (4/3) * π * r³
        float sphere_volume = (4.0f / 3.0f) * math::pi * radius * radius * radius;

        // total volume
        return cylinder_volume + sphere_volume;
    }

    float PhysicsBody::GetCapsuleRadius()
    {
        Vector3 scale = GetEntity()->GetScale();
        return max(scale.x, scale.z) * 0.5f;
    }

    void PhysicsBody::Move(const math::Vector3& offset)
    {
        if (m_body_type == BodyType::Controller && Engine::IsFlagSet(EngineMode::Playing))
        {
            if (!m_controller)
                return;

            PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);
            float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());
            PxControllerFilters filters;
            filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
            controller->move(PxVec3(offset.x, offset.y, offset.z), 0.001f, delta_time, filters);
        }
        else
        {
            GetEntity()->Translate(offset);
        }
    }
    
    void PhysicsBody::Create()
    {
        // get physx pointers
        PxPhysics* physics              = static_cast<PxPhysics*>(Physics::GetPhysics());
        PxScene* scene                  = static_cast<PxScene*>(Physics::GetScene());
        PxRigidActor* rigid_actor       = static_cast<PxRigidActor*>(m_body);
        PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);

        if (m_body_type == BodyType::Controller)
        {
            if (!controller_manager)
            {
                controller_manager = PxCreateControllerManager(*scene);
                if (!controller_manager)
                {
                    SP_LOG_ERROR("Failed to create controller manager");
                    return;
                }
            }

            PxCapsuleControllerDesc desc;
            desc.radius        = 0.5f;
            desc.height        = 1.8f;
            desc.climbingMode  = PxCapsuleClimbingMode::eEASY;
            desc.stepOffset    = 0.8f;
            desc.slopeLimit    = cosf(60.0f * math::deg_to_rad);
            desc.contactOffset = 0.15f;
            desc.position      = PxExtendedVec3(GetEntity()->GetPosition().x, GetEntity()->GetPosition().y, GetEntity()->GetPosition().z);
            desc.upDirection   = PxVec3(0, 1, 0);
            desc.material      = physics->createMaterial(m_friction, m_friction_rolling, m_restitution);

            m_controller = static_cast<PxControllerManager*>(controller_manager)->createController(desc);
            if (!m_controller)
            {
                SP_LOG_ERROR("Failed to create capsule controller");
                desc.material->release();
                return;
            }
            desc.material->release();
        }
        else
        { 
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
            
                // create body
                if (is_static)
                {
                    m_body = physics->createRigidStatic(pose);
                }
                else
                {
                    m_body = physics->createRigidDynamic(pose);
                    PxRigidDynamic* rigid_dynamic = static_cast<PxRigidDynamic*>(m_body);
                    rigid_dynamic->setMass(m_mass);
                    rigid_dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
                    if (m_center_of_mass != Vector3::Zero)
                    {
                        PxVec3 p = PxVec3(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                        PxRigidBodyExt::setMassAndUpdateInertia(*rigid_dynamic, m_mass, &p);
                    }
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
            switch (m_body_type)
            {
                case BodyType::Box:
                {
                    Vector3 scale = GetEntity()->GetScale();
                    PxBoxGeometry geometry(scale.x * 0.5f, scale.y * 0.5f, scale.z * 0.5f);
                    m_shape = physics->createShape(geometry, *material);
                    break;
                }
                case BodyType::Sphere:
                {
                    Vector3 scale = GetEntity()->GetScale();
                    float radius  = max(max(scale.x, scale.y), scale.z) * 0.5f;
                    PxSphereGeometry geometry(radius);
                    m_shape = physics->createShape(geometry, *material);
                    break;
                }
                case BodyType::Plane:
                {
                    PxPlaneGeometry geometry;
                    m_shape = physics->createShape(geometry, *material);
                    static_cast<PxShape*>(m_shape)->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
                    break;
                }
                case BodyType::Capsule:
                {
                    Vector3 scale     = GetEntity()->GetScale();
                    float radius      = max(scale.x, scale.z) * 0.5f;
                    float half_height = scale.y * 0.5f;
                    PxCapsuleGeometry geometry(radius, half_height);
                    m_shape = physics->createShape(geometry, *material);
                    static_cast<PxShape*>(m_shape)->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
                    break;
                }
                case BodyType::HeightField:
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
                
                    // create height field samples
                    std::vector<PxHeightFieldSample> samples(width * depth);
                    for (uint32_t z = 0; z < depth; ++z)
                    {
                        for (uint32_t x = 0; x < width; ++x)
                        {
                            uint32_t index = z * width + x; 
                            PxHeightFieldSample& sample = samples[index];
                    
                            // flip x and z if heightmap is stored as [x * depth + z]
                            uint32_t flipped_index  = x * depth + z;
                            float height            = height_map[flipped_index];
                            float normalized_height = (height - terrain->GetMinY()) / height_range;
                            sample.height           = static_cast<PxI16>(normalized_height * 65535.0f - 32768.0f);
                    
                            sample.materialIndex0 = 0;
                            sample.materialIndex1 = 0;
                        }
                    }
                
                    // create height field description
                    PxHeightFieldDesc height_field_desc;
                    height_field_desc.nbRows         = depth; // z-direction
                    height_field_desc.nbColumns      = width; // x-direction
                    height_field_desc.samples.data   = samples.data();
                    height_field_desc.samples.stride = sizeof(PxHeightFieldSample);
                
                    // cooking parameters
                    PxTolerancesScale px_scale;
                    px_scale.length = 1.0f; // 1 unit = 1 meter
                    px_scale.speed  = Physics::GetGravity().y; // gravity in meters per second
                    PxCookingParams params(px_scale);
                    params.meshPreprocessParams = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES);
                    params.meshWeldTolerance    = 0.2f; // merge vertices within 20cm
                    params.buildGPUData         = false;
                
                    // create height field
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
                        float height_midpoint = (terrain->GetMaxY() + terrain->GetMinY()) / 2.0f;
                        PxVec3 offset(
                            -terrain_scale.x * 0.5f, // Center x
                            height_midpoint,         // Shift up by half height range
                            -terrain_scale.z * 0.5f  // Center z
                        );
                        static_cast<PxShape*>(m_shape)->setLocalPose(PxTransform(offset));
                    }
                
                    break;
                }
                case BodyType::Mesh:
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
                    Vector3 _scale = GetEntity()->GetScale();
                    for (const auto& vertex : vertices)
                    {
                        PxVec3 scaled_vertex(
                            vertex.pos[0] * _scale.x,
                            vertex.pos[1] * _scale.y,
                            vertex.pos[2] * _scale.z
                        );
                        px_vertices.emplace_back(scaled_vertex);
                    }
                
                    // cooking parameters
                    PxTolerancesScale scale;
                    scale.length = 1.0f;                    // 1 unit = 1 meter
                    scale.speed  = Physics::GetGravity().y; // gravity is in meters per second
                    PxCookingParams params(scale);
                    params.meshPreprocessParams           = PxMeshPreprocessingFlags(PxMeshPreprocessingFlag::eWELD_VERTICES);
                    params.meshWeldTolerance              = 0.01f;  // merge vertices within 1cm
                    params.meshAreaMinLimit               = 0.01f;  // remove very small triangles
                    params.meshEdgeLengthMaxLimit         = 500.0f; // warn about large edges
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
                        mesh_desc.points.count  = static_cast<PxU32>(px_vertices.size());
                        mesh_desc.points.stride = sizeof(PxVec3);
                        mesh_desc.points.data   = px_vertices.data();
                        mesh_desc.flags         = PxConvexFlag::eCOMPUTE_CONVEX;
                
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
                        if (PxShape* old_shape = static_cast<PxShape*>(m_shape))
                        {
                            old_shape->release();
                        }

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
            }
            else
            {
                SP_LOG_ERROR("failed to create shape for type %d", m_body_type);
            }
            
            // release material
            material->release();
        }
    }
}
