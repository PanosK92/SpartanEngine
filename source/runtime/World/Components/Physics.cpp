/*
Copyright(c) 2015-2026 Panos Karabelas

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
#include "Physics.h"
#include "Renderable.h"
#include "Camera.h"
#include "../Entity.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../Physics/PhysicsWorld.h"
#include "../../Physics/Car.h"
#include "../../Geometry/GeometryProcessing.h"
#include "../../Rendering/Renderer.h"
SP_WARNINGS_OFF
#ifdef DEBUG
    #define _DEBUG 1
    #undef NDEBUG
#else
    #define NDEBUG 1
    #undef _DEBUG
#endif
#define PX_PHYSX_STATIC_LIB
#include "../IO/pugixml.hpp"
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
        const float distance_deactivate = 80.0f;
        const float distance_activate   = 40.0f;

        // average european male: ~1.78m tall, eye level at ~1.65m
        // capsule total height = cylinder_height + 2 * radius
        // we want total height = 1.8m, with radius 0.25m
        // so cylinder_height = 1.8 - 0.5 = 1.3m
        const float controller_radius   = 0.25f;
        const float standing_height     = 1.3f;  // cylinder height (total = 1.3 + 0.5 = 1.8m)
        const float crouch_height       = 0.5f;  // cylinder height when crouching (total = 0.5 + 0.5 = 1.0m)

        // derivatives
        const float distance_deactivate_squared = distance_deactivate * distance_deactivate;
        const float distance_activate_squared   = distance_activate * distance_activate;

        PxControllerManager* controller_manager = nullptr;

        // helper to build lock flags from position and rotation lock vectors
        PxRigidDynamicLockFlags build_lock_flags(const Vector3& position_lock, const Vector3& rotation_lock)
        {
            PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
            if (position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
            if (position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
            if (position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
            if (rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
            if (rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
            if (rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
            return flags;
        }
    }

    Physics::Physics(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_static, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_kinematic, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mass, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_friction_rolling, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_restitution, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_position_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_rotation_lock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_center_of_mass, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_velocity, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_controller, void*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material, void*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mesh, void*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_actors, vector<void*>);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_body_type, SetBodyType, BodyType);
    }

    Physics::~Physics()
    {
        Remove();
    }

    void Physics::Initialize()
    {
        Component::Initialize();
    }

    void Physics::Shutdown()
    {
        // release the controller manager (created lazily when first controller is made)
        if (controller_manager)
        {
            controller_manager->release();
            controller_manager = nullptr;
        }
    }

    void Physics::Remove()
    {
        if (m_controller)
        {
            static_cast<PxController*>(m_controller)->release();
            m_controller = nullptr;
            
            // release the material that was created for this controller
            if (m_material)
            {
                static_cast<PxMaterial*>(m_material)->release();
                m_material = nullptr;
            }
        }

        for (auto* body : m_actors)
        {
            if (body)
            {
                PxRigidActor* actor = static_cast<PxRigidActor*>(body);
                PhysicsWorld::RemoveActor(actor);
                actor->release();
            }
        }
        m_actors.clear();
        m_actors_active.clear();

        if (PxMaterial* material = static_cast<PxMaterial*>(m_material))
        {
            material->release();
            m_material = nullptr;
        }
    }

    void Physics::Tick()
    {
        // map transform from physx to engine and vice versa
        if (m_body_type == BodyType::Controller)
        {
            if (Engine::IsFlagSet(EngineMode::Playing))
            {
                // compute gravitational acceleration
                float delta_time  = static_cast<float>(Timer::GetDeltaTimeSec());
                m_velocity.y     += PhysicsWorld::GetGravity().y * delta_time;
                PxVec3 displacement(0.0f, m_velocity.y * delta_time, 0.0f);

                // if there is a collision below, zero out the vertical velocity
                PxControllerFilters filters;
                filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
                PxControllerCollisionFlags collision_flags = static_cast<PxCapsuleController*>(m_controller)->move(displacement, 0.001f, delta_time, filters);
                if (collision_flags & PxControllerCollisionFlag::eCOLLISION_DOWN)
                {
                    m_velocity.y = 0.0f;
                }

                // set new position to entity
                PxExtendedVec3 pos_ext = static_cast<PxCapsuleController*>(m_controller)->getPosition();
                Vector3 pos_previous   = GetEntity()->GetPosition();
                Vector3 pos            = Vector3(static_cast<float>(pos_ext.x), static_cast<float>(pos_ext.y), static_cast<float>(pos_ext.z));
                GetEntity()->SetPosition(pos);

                // compute velocity for xz
                if (delta_time > 0.0f)
                {
                    m_velocity.x = (pos.x - pos_previous.x) / delta_time;
                    m_velocity.z = (pos.z - pos_previous.z) / delta_time;
                }
            }
            else
            {
                Vector3 entity_pos = GetEntity()->GetPosition();
                static_cast<PxCapsuleController*>(m_controller)->setPosition(PxExtendedVec3(entity_pos.x, entity_pos.y, entity_pos.z));
                m_velocity = Vector3::Zero;
            }
        }
        else if (m_body_type == BodyType::Vehicle)
        {
            if (Engine::IsFlagSet(EngineMode::Playing))
            {
                // sync wheel offsets from entity positions once at start of play
                if (!m_wheel_offsets_synced)
                {
                    SyncWheelOffsetsFromEntities();
                    m_wheel_offsets_synced = true;
                }
                
                // update vehicle physics (input is set externally via vehicle::set_throttle/brake/steering)
                float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());
                car::tick(delta_time);
                
                // sync physx -> entity
                if (!m_actors.empty() && m_actors[0])
                {
                    PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[0]);
                    PxTransform pose = actor->getGlobalPose();
                    GetEntity()->SetPosition(Vector3(pose.p.x, pose.p.y, pose.p.z));
                    GetEntity()->SetRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
                }

                // update wheel entity transforms (spin and steering)
                UpdateWheelTransforms();
            }
            else
            {
                // editor mode: sync entity -> physx, reset velocities
                m_wheel_offsets_synced = false; // reset so offsets re-sync on next play
                
                if (!m_actors.empty() && m_actors[0])
                {
                    Vector3 pos = GetEntity()->GetPosition();
                    Quaternion rot = GetEntity()->GetRotation();
                    PxTransform pose(
                        PxVec3(pos.x, pos.y, pos.z),
                        PxQuat(rot.x, rot.y, rot.z, rot.w)
                    );
                    
                    PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[0]);
                    actor->setGlobalPose(pose);
                    
                    if (PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>())
                    {
                        dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                        dynamic->setAngularVelocity(PxVec3(0, 0, 0));
                    }
                }
            }
        }
        else if (!m_is_static)
        {
            Renderable* renderable = GetEntity()->GetComponent<Renderable>();
            if (!renderable)
                return;

            for (uint32_t i = 0; i < m_actors.size(); i++)
            {
                if (!m_actors[i])
                    continue;
                    
                PxRigidActor* actor     = static_cast<PxRigidActor*>(m_actors[i]);
                PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>();
                if (Engine::IsFlagSet(EngineMode::Playing))
                {
                    if (m_is_kinematic && dynamic)
                    {
                        // sync entity -> physX (kinematic target)
                        math::Matrix transform;
                        if (renderable->HasInstancing() && i < renderable->GetInstanceCount())
                        {
                            transform = renderable->GetInstance(i, true);
                        }
                        else if (i == 0)
                        {
                            transform = GetEntity()->GetMatrix();
                        }
                        else
                        {
                            continue;
                        }
                        PxTransform target(
                            PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                            PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
                        );
                        dynamic->setKinematicTarget(target);
                    }
                    else
                    {
                        // sync physx -> entity (simulated dynamic)
                        PxTransform pose = actor->getGlobalPose();
                        math::Matrix transform = math::Matrix::CreateTranslation(Vector3(pose.p.x, pose.p.y, pose.p.z)) * math::Matrix::CreateRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
                        if (renderable->HasInstancing() && i < renderable->GetInstanceCount())
                        {
                            //renderable->SetInstance(static_cast<uint32_t>(i), transform); // implement if needed
                        }
                        else if (i == 0)
                        {
                            GetEntity()->SetPosition(Vector3(pose.p.x, pose.p.y, pose.p.z));
                            GetEntity()->SetRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
                        }
                    }
                }
                else
                {
                    // editor mode: sync entity -> physx, reset velocities only for non-kinematics
                    math::Matrix transform;
                    if (renderable->HasInstancing() && i < renderable->GetInstanceCount())
                    {
                        transform = renderable->GetInstance(i, true);
                    }
                    else if (i == 0)
                    {
                        transform = GetEntity()->GetMatrix();
                    }
                    else
                    {
                        continue;
                    }

                    PxTransform pose(
                        PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                        PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
                    );
                    actor->setGlobalPose(pose);

                    if (dynamic && !m_is_kinematic)
                    {
                        dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                        dynamic->setAngularVelocity(PxVec3(0, 0, 0));
                    }
                }
            }
        }

        // distance-based activation/deactivation for static actors
        // this optimization prevents the physics scene from being overwhelmed with distant static colliders
        if (m_body_type != BodyType::Controller && m_is_static)
        {
            Camera* camera = World::GetCamera();
            Renderable* renderable = GetEntity()->GetComponent<Renderable>();
            if (camera && renderable)
            {
                const Vector3 camera_pos = camera->GetEntity()->GetPosition();
                
                // ensure tracking vector matches actor count
                if (m_actors_active.size() != m_actors.size())
                {
                    m_actors_active.resize(m_actors.size(), true); // assume initially active
                }

                for (uint32_t i = 0; i < static_cast<uint32_t>(m_actors.size()); i++)
                {
                    PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[i]);
                    if (!actor)
                        continue;

                    // compute distance to actor
                    Vector3 closest_point = renderable->HasInstancing()
                        ? renderable->GetInstance(i, true).GetTranslation()
                        : renderable->GetBoundingBox().GetClosestPoint(camera_pos);
                    const float distance_squared = Vector3::DistanceSquared(camera_pos, closest_point);

                    // use hysteresis to prevent flickering at boundary
                    const bool is_active     = m_actors_active[i];
                    const bool should_remove = is_active && (distance_squared > distance_deactivate_squared);
                    const bool should_add    = !is_active && (distance_squared <= distance_activate_squared);

                    if (should_remove)
                    {
                        PhysicsWorld::RemoveActor(actor);
                        m_actors_active[i] = false;
                    }
                    else if (should_add)
                    {
                        PhysicsWorld::AddActor(actor);
                        m_actors_active[i] = true;
                    }
                }
            }
        }
    }

    void Physics::Save(pugi::xml_node& node)
    {
        node.append_attribute("mass")             = m_mass;
        node.append_attribute("friction")         = m_friction;
        node.append_attribute("friction_rolling") = m_friction_rolling;
        node.append_attribute("restitution")      = m_restitution;
        node.append_attribute("is_static")        = m_is_static;
        node.append_attribute("is_kinematic")     = m_is_kinematic;
        node.append_attribute("position_lock_x")  = m_position_lock.x;
        node.append_attribute("position_lock_y")  = m_position_lock.y;
        node.append_attribute("position_lock_z")  = m_position_lock.z;
        node.append_attribute("rotation_lock_x")  = m_rotation_lock.x;
        node.append_attribute("rotation_lock_y")  = m_rotation_lock.y;
        node.append_attribute("rotation_lock_z")  = m_rotation_lock.z;
        node.append_attribute("center_of_mass_x") = m_center_of_mass.x;
        node.append_attribute("center_of_mass_y") = m_center_of_mass.y;
        node.append_attribute("center_of_mass_z") = m_center_of_mass.z;
        node.append_attribute("body_type")        = static_cast<int>(m_body_type);
    }
    
    void Physics::Load(pugi::xml_node& node)
    {
        m_mass             = node.attribute("mass").as_float(0.001f);
        m_friction         = node.attribute("friction").as_float(1.0f);
        m_friction_rolling = node.attribute("friction_rolling").as_float(0.002f);
        m_restitution      = node.attribute("restitution").as_float(0.2f);
        m_is_static        = node.attribute("is_static").as_bool(true);
        m_is_kinematic     = node.attribute("is_kinematic").as_bool(false);
        m_position_lock.x  = node.attribute("position_lock_x").as_float(0.0f);
        m_position_lock.y  = node.attribute("position_lock_y").as_float(0.0f);
        m_position_lock.z  = node.attribute("position_lock_z").as_float(0.0f);
        m_rotation_lock.x  = node.attribute("rotation_lock_x").as_float(0.0f);
        m_rotation_lock.y  = node.attribute("rotation_lock_y").as_float(0.0f);
        m_rotation_lock.z  = node.attribute("rotation_lock_z").as_float(0.0f);
        m_center_of_mass.x = node.attribute("center_of_mass_x").as_float(0.0f);
        m_center_of_mass.y = node.attribute("center_of_mass_y").as_float(0.0f);
        m_center_of_mass.z = node.attribute("center_of_mass_z").as_float(0.0f);
        m_body_type        = static_cast<BodyType>(node.attribute("body_type").as_int(static_cast<int>(BodyType::Max)));
    
        Create();
    }

    void Physics::SetMass(float mass)
    {
        // approximate mass from volume
        if (mass == mass_from_volume)
        {
            constexpr float density = 1000.0f; // kg/m³ (default density, e.g., water)
            float volume            = 0.0f;
            Vector3 scale           = GetEntity()->GetScale();

            if (m_body_type == BodyType::Max)
            {
                SP_LOG_WARNING("This call will be ignored. You need to set the body type before setting mass from volume.");
                return;
            }

            switch (m_body_type)
            {
                case BodyType::Box:
                {
                    // volume = x * y * z
                    volume = scale.x * scale.y * scale.z;
                    break;
                }
                case BodyType::Sphere:
                {
                    // volume = (4/3) * π * r³, radius = max(x, y, z) / 2
                    float radius = max(max(scale.x, scale.y), scale.z) * 0.5f;
                    volume       = (4.0f / 3.0f) * math::pi * radius * radius * radius;
                    break;
                }
                case BodyType::Capsule:
                {
                    // volume             = cylinder (π * r² * h) + two hemispheres ((4/3) * π * r³)
                    float radius          = max(scale.x, scale.z) * 0.5f;
                    float cylinder_height = max(0.0f, scale.y - 2.0f * radius); // height of cylindrical part (clamp to avoid negative)
                    float cylinder_volume = math::pi * radius * radius * cylinder_height;
                    float sphere_volume   = (4.0f / 3.0f) * math::pi * radius * radius * radius;
                    volume                = cylinder_volume + sphere_volume;
                    break;
                }
                case BodyType::Mesh:
                {
                    // approximate using bounding box volume
                    Renderable* renderable = GetEntity()->GetComponent<Renderable>();
                    if (renderable)
                    {
                        BoundingBox bbox = renderable->GetBoundingBox();
                        Vector3 extents = bbox.GetExtents();
                        volume = extents.x * extents.y * extents.z * 8.0f; // extents are half-size
                    }
                    else
                    {
                        volume = 1.0f; // fallback volume (1 m³)
                    }
                    break;
                }
                case BodyType::Plane:
                {
                    // infinite plane, use default mass
                    mass   = 1.0f;
                    volume = 0.0f; // skip volume-based calculation
                    break;
                }
                case BodyType::Controller:
                {
                    // controller, use default mass (e.g., human-like)
                    mass   = 70.0f; // approximate human mass
                    volume = 0.0f;  // skip volume-based calculation
                    break;
                }
            }
    
            // calculate mass from volume if applicable
            if (volume > 0.0f)
            {
                mass = volume * density;
            }
        }
    
        // ensure safe physx mass range
        m_mass = min(max(mass, 0.001f), 10000.0f);
    
        // update mass for all dynamic bodies
        for (auto* body : m_actors)
        {
            if (body)
            { 
                if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
                {
                    dynamic->setMass(m_mass);
                    // update inertia if center of mass is set
                    if (m_center_of_mass != Vector3::Zero)
                    {
                        PxVec3 p(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                        PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &p);
                    }
                }
            }
        }
    }

    void Physics::SetFriction(float friction)
    {
        if (m_friction == friction)
            return;
    
        if (m_material)
        {
            m_friction = friction;
            static_cast<PxMaterial*>(m_material)->setStaticFriction(m_friction);
        }
    }

    void Physics::SetFrictionRolling(float friction_rolling)
    {
        if (m_friction_rolling == friction_rolling)
            return;

        if (m_material)
        {
            m_friction_rolling = friction_rolling;
            static_cast<PxMaterial*>(m_material)->setDynamicFriction(m_friction_rolling);
        }
    }

    void Physics::SetRestitution(float restitution)
    {
        if (m_restitution == restitution)
            return;

        if (m_material)
        {
            m_restitution = restitution;
            static_cast<PxMaterial*>(m_material)->setRestitution(m_restitution);
        }
    }

    void Physics::SetLinearVelocity(const Vector3& velocity) const
    {
        if (m_body_type == BodyType::Controller)
            return;

        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setLinearVelocity(PxVec3(velocity.x, velocity.y, velocity.z));
                dynamic->wakeUp();
            }
        }
    }

    Vector3 Physics::GetLinearVelocity() const
    {
        if (m_body_type == BodyType::Controller)
        {
            if (m_controller)
            {
                // for controllers, return the stored velocity used for movement
                return m_velocity;
            }
            return Vector3::Zero;
        }
        
        if (m_actors.empty() || !m_actors[0])
            return Vector3::Zero;
            
        if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(m_actors[0])->is<PxRigidDynamic>())
        {
            PxVec3 velocity = dynamic->getLinearVelocity();
            return Vector3(velocity.x, velocity.y, velocity.z);
        }
        
        return Vector3::Zero;
    }

    void Physics::SetAngularVelocity(const Vector3& velocity) const
    {
        if (m_body_type == BodyType::Controller)
            return;

        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setAngularVelocity(PxVec3(velocity.x, velocity.y, velocity.z));
                dynamic->wakeUp();
            }
        }
    }

    void Physics::ApplyForce(const Vector3& force, PhysicsForce mode) const
    {
        if (m_body_type == BodyType::Controller)
        {
            SP_LOG_WARNING("Don't call ApplyForce on a controller, call Move() instead");
            return;
        }

        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                PxForceMode::Enum px_mode = (mode == PhysicsForce::Constant) ? PxForceMode::eFORCE : PxForceMode::eIMPULSE;
                dynamic->addForce(PxVec3(force.x, force.y, force.z), px_mode);
                dynamic->wakeUp();
            }
        }
    }

    void Physics::SetPositionLock(bool lock)
    {
        SetPositionLock(lock ? Vector3::One : Vector3::Zero);
    }

    void Physics::SetPositionLock(const Vector3& lock)
    {
        if (m_body_type == BodyType::Controller)
            return;
    
        m_position_lock = lock;
        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
            }
        }
    }

    void Physics::SetRotationLock(bool lock)
    {
        SetRotationLock(lock ? Vector3::One : Vector3::Zero);
    }

    void Physics::SetRotationLock(const Vector3& lock)
    {
        if (m_body_type == BodyType::Controller)
            return;
    
        m_rotation_lock = lock;
        for (auto* body : m_actors)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
            }
        }
    }

    void Physics::SetCenterOfMass(const Vector3& center_of_mass)
    {
        if (m_body_type == BodyType::Controller)
            return;
    
        m_center_of_mass = center_of_mass;
        for (auto* body : m_actors)
        {
            if (!body)
                continue;
                
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                if (m_center_of_mass != Vector3::Zero)
                {
                    PxVec3 p(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                    PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &p);
                }
                else
                {
                    // update inertia with default center of mass (0,0,0)
                    PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, nullptr);
                }
            }
        }
    }

    void Physics::SetBodyType(BodyType type)
    {
        if (m_body_type == type)
            return;

        m_body_type = type;
        Create();
    }

    bool Physics::IsGrounded() const
    {
        return GetGroundEntity() != nullptr; // eCOLLISION_DOWN is not very reliable (it can flicker), so we use raycasting as a fallback
    }

    Entity* Physics::GetGroundEntity() const
    {
        // check if body is a controller
        if (m_body_type != BodyType::Controller)
        {
            SP_LOG_WARNING("this method is only applicable for controller bodies.");
            return nullptr;
        }
    
        if (!m_controller)
            return nullptr;
    
        // get controller's current position
        PxController* controller = static_cast<PxController*>(m_controller);
        PxExtendedVec3 pos_ext   = controller->getPosition();
        PxVec3 pos               = PxVec3(static_cast<float>(pos_ext.x), static_cast<float>(pos_ext.y), static_cast<float>(pos_ext.z));
    
        // ray start just below the controller
        const float ray_length = standing_height;
        PxVec3 ray_start       = pos;
        PxVec3 ray_dir         = PxVec3(0.0f, -1.0f, 0.0f);
    
        const PxU32 max_hits = 10;
        PxRaycastHit hit_buffer[max_hits];
        PxRaycastBuffer hit(hit_buffer, max_hits);
    
        PxQueryFilterData filter_data;
        filter_data.flags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
    
        PxScene* scene = static_cast<PxScene*>(PhysicsWorld::GetScene());
        if (!scene)
            return nullptr;
    
        // get the actor used by the controller to avoid returning itself
        PxRigidActor* actor_to_ignore = controller->getActor();
    
        if (scene->raycast(ray_start, ray_dir, ray_length, hit, PxHitFlag::eDEFAULT, filter_data))
        { 
            for (PxU32 i = 0; i < hit.nbTouches; ++i)
            {
                const PxRaycastHit& current_hit = hit.getTouch(i);
    
                if (!current_hit.actor || current_hit.actor == actor_to_ignore)
                    continue;
    
                if (current_hit.actor->userData)
                    return static_cast<Entity*>(current_hit.actor->userData);
            }
        }
    
        return nullptr;
    }

    float Physics::GetCapsuleVolume()
    {
        // total volume is the sum of the cylinder and two hemispheres
        const float radius = GetCapsuleRadius();
        const Vector3 scale = GetEntity()->GetScale();

        // cylinder volume: π * r² * h (clamp to avoid negative height)
        const float cylinder_height = max(0.0f, scale.y - 2.0f * radius);
        const float cylinder_volume = math::pi * radius * radius * cylinder_height;

        // sphere volume (two hemispheres = one full sphere): (4/3) * π * r³
        const float sphere_volume = (4.0f / 3.0f) * math::pi * radius * radius * radius;

        return cylinder_volume + sphere_volume;
    }

    float Physics::GetCapsuleRadius()
    {
        Vector3 scale = GetEntity()->GetScale();
        return max(scale.x, scale.z) * 0.5f;
    }

    Vector3 Physics::GetControllerTopLocal() const
    {
        if (m_body_type != BodyType::Controller || !m_controller)
        {
            SP_LOG_WARNING("Only applicable for controller bodies.");
            return Vector3::Zero;
        }
        
        PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);
        float height                    = controller->getHeight();
        float radius                    = controller->getRadius();
        
        // for an average european male (1.8m), eye level is at ~1.65m from the ground
        // that's about 0.15m below the top of the head
        // this returns eye level position relative to capsule center (where camera should be)
        const float eye_offset_from_top = 0.13f;
        return Vector3(0.0f, (height * 0.5f) + radius - eye_offset_from_top, 0.0f);
    }

    void Physics::SetStatic(bool is_static)
    {
        // return if state hasn't changed
        if (m_is_static == is_static)
            return;
    
        // update static state
        m_is_static    = is_static;
        m_is_kinematic = false; // statics can't be kinematic
     
        // recreate bodies to apply static/dynamic state
        Create();
    }

    void Physics::SetKinematic(bool is_kinematic)
    {
        // return if state hasn't changed
        if (m_is_kinematic == is_kinematic)
            return;
    
        // update kinematic state
        m_is_kinematic = is_kinematic;
        m_is_static    = false; // kinematics require dynamic (non-static) bodies
    
        Create(); // recreate body to apply changes
    }

    void Physics::Move(const math::Vector3& offset)
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

    void Physics::Crouch(const bool crouch)
    {
        if (m_body_type != BodyType::Controller || !m_controller || !Engine::IsFlagSet(EngineMode::Playing))
            return;

        // resize the capsule
        PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);
        const float current_height      = controller->getHeight();
        const float target_height       = crouch ? crouch_height : standing_height;
        const float delta_time          = static_cast<float>(Timer::GetDeltaTimeSec());
        const float speed               = 10.0f;
        const float lerped_height       = math::lerp(current_height, target_height, 1.0f - exp(-speed * delta_time));  
        controller->resize(lerped_height);

        // ensure bottom of the capsule is touching the ground
        PxExtendedVec3 pos = controller->getPosition();
        GetEntity()->SetPosition(Vector3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)));
    }

    void Physics::SetVehicleThrottle(float value)
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        car::set_throttle(value);
    }

    void Physics::SetVehicleBrake(float value)
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        car::set_brake(value);
    }

    void Physics::SetVehicleSteering(float value)
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        car::set_steering(value);
    }

    void Physics::SetVehicleHandbrake(float value)
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        car::set_handbrake(value);
    }

    void Physics::SetWheelEntity(WheelIndex wheel, Entity* entity)
    {
        if (m_body_type != BodyType::Vehicle)
        {
            SP_LOG_WARNING("SetWheelEntity only works with Vehicle body type");
            return;
        }

        int index = static_cast<int>(wheel);
        if (index >= 0 && index < static_cast<int>(WheelIndex::Count))
        {
            m_wheel_entities[index] = entity;
            
            // sync the physics wheel offset from the entity position
            if (entity)
            {
                Entity* vehicle_entity = GetEntity();
                if (vehicle_entity)
                {
                    // transform wheel world position to vehicle-local space
                    Vector3 vehicle_world_pos = vehicle_entity->GetPosition();
                    Quaternion vehicle_world_rot = vehicle_entity->GetRotation();
                    Quaternion vehicle_world_rot_inv = vehicle_world_rot.Conjugate();
                    
                    // try to get the actual mesh center from the renderable's bounding box
                    Vector3 wheel_world_pos = entity->GetPosition();
                    Renderable* renderable = entity->GetComponent<Renderable>();
                    if (renderable)
                    {
                        renderable->Tick();
                        BoundingBox aabb = renderable->GetBoundingBox();
                        wheel_world_pos = aabb.GetCenter();
                    }
                    
                    Vector3 local_pos = vehicle_world_rot_inv * (wheel_world_pos - vehicle_world_pos);
                    car::set_wheel_offset(index, local_pos.x, local_pos.z);
                }
            }
        }
    }

    Entity* Physics::GetWheelEntity(WheelIndex wheel) const
    {
        int index = static_cast<int>(wheel);
        if (index >= 0 && index < static_cast<int>(WheelIndex::Count))
        {
            return m_wheel_entities[index];
        }
        return nullptr;
    }

    void Physics::SetChassisEntity(Entity* entity)
    {
        if (m_body_type != BodyType::Vehicle)
        {
            SP_LOG_WARNING("SetChassisEntity only works with Vehicle body type");
            return;
        }

        m_chassis_entity = entity;
        if (entity)
        {
            // store base position so we can offset from it
            m_chassis_base_pos = entity->GetPositionLocal();
            SP_LOG_INFO("SetChassisEntity: chassis set to '%s', base_pos=(%.2f, %.2f, %.2f)", 
                entity->GetObjectName().c_str(), m_chassis_base_pos.x, m_chassis_base_pos.y, m_chassis_base_pos.z);
        }
        else
        {
            SP_LOG_WARNING("SetChassisEntity: entity is null!");
        }
    }

    void Physics::SetWheelRadius(float radius)
    {
        if (m_body_type != BodyType::Vehicle)
        {
            SP_LOG_WARNING("SetWheelRadius only works with Vehicle body type");
            return;
        }

        m_wheel_radius = radius;
        
        // update the wheel radius in vehicle config (for physics contact calculations)
        car::cfg.wheel_radius = radius;
        
        // recalculate and update body height based on actual wheel radius
        if (car::body)
        {
            // calculate correct body height using actual spring stiffness
            float front_mass_per_wheel = car::cfg.mass * 0.40f * 0.5f;
            float front_omega = 2.0f * math::pi * car::tuning::front_spring_freq;
            float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
            float front_load = front_mass_per_wheel * 9.81f;
            float expected_sag = std::clamp(front_load / front_stiffness, 0.0f, car::cfg.suspension_travel * 0.8f);
            const float correct_body_height = radius + car::cfg.suspension_height + expected_sag;
            
            // update body position with correct height
            PxTransform pose = car::body->getGlobalPose();
            pose.p.y = correct_body_height;
            car::body->setGlobalPose(pose);
            
            // recompute wheel constants with new radius
            car::compute_constants();
            
            SP_LOG_INFO("SetWheelRadius: adjusted body height to %.3f for radius %.3f", correct_body_height, radius);
        }
        
        SP_LOG_INFO("SetWheelRadius: wheel radius set to %.3f", radius);
    }

    void Physics::ComputeWheelRadiusFromEntity(Entity* wheel_entity)
    {
        if (!wheel_entity)
        {
            SP_LOG_WARNING("ComputeWheelRadiusFromEntity: wheel_entity is null");
            return;
        }

        // get the renderable component to access the bounding box
        Renderable* renderable = wheel_entity->GetComponent<Renderable>();
        if (!renderable)
        {
            SP_LOG_WARNING("ComputeWheelRadiusFromEntity: wheel entity has no Renderable component");
            return;
        }

        // force bounding box update to reflect current entity transform (including scale)
        // this is needed because the bounding box is lazily updated during Tick()
        renderable->Tick();

        // get the aabb - this is in world space (transformed by entity matrix including scale)
        BoundingBox aabb = renderable->GetBoundingBox();
        Vector3 extents = aabb.GetExtents(); // half-sizes, already scaled
        
        // the wheel radius is the largest extent (wheels are usually symmetric)
        // for a wheel mesh, this gives us the actual visual radius
        float radius = max(max(extents.x, extents.y), extents.z);
        
        // compute the offset from entity origin to mesh center
        // this handles meshes that don't have their origin at geometric center
        Vector3 aabb_center = aabb.GetCenter();
        Vector3 entity_pos = wheel_entity->GetPosition();
        m_wheel_mesh_center_offset_y = aabb_center.y - entity_pos.y;
        
        SetWheelRadius(radius);
        
        SP_LOG_INFO("ComputeWheelRadiusFromEntity: computed radius=%.3f, center_offset_y=%.3f from entity '%s' (extents: %.3f, %.3f, %.3f)", 
            radius, m_wheel_mesh_center_offset_y, wheel_entity->GetObjectName().c_str(), extents.x, extents.y, extents.z);
    }

    float Physics::GetSuspensionHeight() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::cfg.suspension_height;
    }

    float Physics::GetVehicleThrottle() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_throttle();
    }

    float Physics::GetVehicleBrake() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_brake();
    }

    float Physics::GetVehicleSteering() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_steering();
    }

    float Physics::GetVehicleHandbrake() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_handbrake();
    }

    bool Physics::IsWheelGrounded(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::is_wheel_grounded(static_cast<int>(wheel));
    }

    float Physics::GetWheelCompression(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_compression(static_cast<int>(wheel));
    }

    float Physics::GetWheelSuspensionForce(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_suspension_force(static_cast<int>(wheel));
    }

    float Physics::GetWheelSlipAngle(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_slip_angle(static_cast<int>(wheel));
    }

    float Physics::GetWheelSlipRatio(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_slip_ratio(static_cast<int>(wheel));
    }

    float Physics::GetWheelTireLoad(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_tire_load(static_cast<int>(wheel));
    }

    float Physics::GetWheelLateralForce(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_lateral_force(static_cast<int>(wheel));
    }

    float Physics::GetWheelLongitudinalForce(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_longitudinal_force(static_cast<int>(wheel));
    }

    float Physics::GetWheelAngularVelocity(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_angular_velocity(static_cast<int>(wheel));
    }

    float Physics::GetWheelRPM(WheelIndex wheel) const
    {
        // convert angular velocity (rad/s) to RPM
        // rpm = (rad/s) * (60 / 2π) = (rad/s) * 9.5493
        float angular_vel = GetWheelAngularVelocity(wheel);
        return fabsf(angular_vel) * 9.5493f;
    }

    float Physics::GetWheelTemperature(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_temperature(static_cast<int>(wheel));
    }

    float Physics::GetWheelLoadTransfer(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_load_transfer(static_cast<int>(wheel));
    }

    float Physics::GetWheelEffectiveLoad(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_effective_load(static_cast<int>(wheel));
    }

    float Physics::GetWheelTempGripFactor(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 1.0f;
        return car::get_wheel_temp_grip_factor(static_cast<int>(wheel));
    }
    
    float Physics::GetWheelBrakeTemp(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_wheel_brake_temp(static_cast<int>(wheel));
    }
    
    float Physics::GetWheelBrakeEfficiency(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return 1.0f;
        return car::get_wheel_brake_efficiency(static_cast<int>(wheel));
    }
    
    void Physics::SetAbsEnabled(bool enabled)
    {
        if (m_body_type == BodyType::Vehicle)
            car::set_abs_enabled(enabled);
    }
    
    bool Physics::GetAbsEnabled() const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::get_abs_enabled();
    }
    
    bool Physics::IsAbsActive(WheelIndex wheel) const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::is_abs_active(static_cast<int>(wheel));
    }
    
    bool Physics::IsAbsActiveAny() const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::is_abs_active_any();
    }
    
    void Physics::SetTcEnabled(bool enabled)
    {
        if (m_body_type == BodyType::Vehicle)
            car::set_tc_enabled(enabled);
    }
    
    bool Physics::GetTcEnabled() const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::get_tc_enabled();
    }
    
    bool Physics::IsTcActive() const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::is_tc_active();
    }
    
    float Physics::GetTcReduction() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_tc_reduction();
    }
    
    void Physics::SetTurboEnabled(bool enabled)
    {
        if (m_body_type == BodyType::Vehicle)
            car::set_turbo_enabled(enabled);
    }
    
    bool Physics::GetTurboEnabled() const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::get_turbo_enabled();
    }
    
    float Physics::GetBoostPressure() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_boost_pressure();
    }
    
    float Physics::GetBoostMaxPressure() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_boost_max_pressure();
    }
    
    void Physics::SetManualTransmission(bool enabled)
    {
        if (m_body_type == BodyType::Vehicle)
            car::set_manual_transmission(enabled);
    }
    
    bool Physics::GetManualTransmission() const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::get_manual_transmission();
    }
    
    void Physics::ShiftUp()
    {
        if (m_body_type == BodyType::Vehicle)
            car::shift_up();
    }
    
    void Physics::ShiftDown()
    {
        if (m_body_type == BodyType::Vehicle)
            car::shift_down();
    }
    
    void Physics::ShiftToNeutral()
    {
        if (m_body_type == BodyType::Vehicle)
            car::shift_to_neutral();
    }
    
    int Physics::GetCurrentGear() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 1; // neutral
        return car::get_current_gear();
    }
    
    const char* Physics::GetCurrentGearString() const
    {
        if (m_body_type != BodyType::Vehicle)
            return "N";
        return car::get_current_gear_string();
    }
    
    float Physics::GetEngineRPM() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_current_engine_rpm();
    }
    
    float Physics::GetEngineTorque() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_engine_torque_current();
    }
    
    float Physics::GetRedlineRPM() const
    {
        if (m_body_type != BodyType::Vehicle)
            return 0.0f;
        return car::get_redline_rpm();
    }
    
    bool Physics::IsShifting() const
    {
        if (m_body_type != BodyType::Vehicle)
            return false;
        return car::get_is_shifting();
    }
    
    void Physics::SetDrawRaycasts(bool enabled)
    {
        car::set_draw_raycasts(enabled);
    }
    
    bool Physics::GetDrawRaycasts() const
    {
        return car::get_draw_raycasts();
    }
    
    void Physics::SetDrawSuspension(bool enabled)
    {
        car::set_draw_suspension(enabled);
    }
    
    bool Physics::GetDrawSuspension() const
    {
        return car::get_draw_suspension();
    }
    
    void Physics::DrawDebugVisualization()
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        using namespace physx;
        
        // colors for visualization
        const Color color_ray_hit    = Color(0.0f, 1.0f, 0.0f, 1.0f);   // green - ray hit ground
        const Color color_ray_miss   = Color(1.0f, 0.0f, 0.0f, 1.0f);   // red - ray missed
        const Color color_susp_top   = Color(1.0f, 1.0f, 0.0f, 1.0f);   // yellow - suspension top
        const Color color_susp_bot   = Color(0.0f, 0.5f, 1.0f, 1.0f);   // blue - suspension bottom/wheel
        
        // draw raycasts
        if (car::get_draw_raycasts())
        {
            int rays_per_wheel = car::get_debug_rays_per_wheel();
            for (int w = 0; w < static_cast<int>(car::wheel_count); w++)
            {
                for (int r = 0; r < rays_per_wheel; r++)
                {
                    PxVec3 origin, hit_point;
                    bool hit;
                    car::get_debug_ray(w, r, origin, hit_point, hit);
                    
                    math::Vector3 from(origin.x, origin.y, origin.z);
                    math::Vector3 to(hit_point.x, hit_point.y, hit_point.z);
                    
                    Renderer::DrawLine(from, to, hit ? color_ray_hit : color_ray_miss, hit ? color_ray_hit : color_ray_miss);
                }
            }
        }
        
        // draw suspension
        if (car::get_draw_suspension())
        {
            for (int w = 0; w < static_cast<int>(car::wheel_count); w++)
            {
                PxVec3 top, bottom;
                car::get_debug_suspension(w, top, bottom);
                
                math::Vector3 susp_top(top.x, top.y, top.z);
                math::Vector3 susp_bottom(bottom.x, bottom.y, bottom.z);
                
                Renderer::DrawLine(susp_top, susp_bottom, color_susp_top, color_susp_bot);
            }
        }
    }
    
    void Physics::SyncWheelOffsetsFromEntities()
    {
        if (m_body_type != BodyType::Vehicle)
            return;
        
        Entity* vehicle_entity = GetEntity();
        if (!vehicle_entity)
            return;
        
        // get vehicle's world transform to convert wheel world positions to vehicle-local space
        Vector3 vehicle_world_pos = vehicle_entity->GetPosition();
        Quaternion vehicle_world_rot = vehicle_entity->GetRotation();
        Quaternion vehicle_world_rot_inv = vehicle_world_rot.Conjugate();
        
        for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
        {
            Entity* wheel_entity = m_wheel_entities[i];
            if (!wheel_entity)
                continue;
            
            // try to get the actual mesh center from the renderable's bounding box
            // this handles meshes where the origin is not at the geometric center
            Vector3 wheel_world_pos = wheel_entity->GetPosition();
            
            Renderable* renderable = wheel_entity->GetComponent<Renderable>();
            if (renderable)
            {
                renderable->Tick(); // ensure bounding box is up to date
                BoundingBox aabb = renderable->GetBoundingBox();
                wheel_world_pos = aabb.GetCenter(); // use mesh center instead of entity origin
            }
            
            // transform to vehicle-local space
            // this handles cases where wheel is a child of an intermediate entity (e.g. "model")
            Vector3 local_pos = vehicle_world_rot_inv * (wheel_world_pos - vehicle_world_pos);
            
            // update the physics wheel offset x and z to match the mesh position
            car::set_wheel_offset(i, local_pos.x, local_pos.z);
            
            SP_LOG_INFO("SyncWheelOffsetsFromEntities: wheel %d offset set to (%.3f, %.3f)", i, local_pos.x, local_pos.z);
        }
        
        SP_LOG_INFO("SyncWheelOffsetsFromEntities: wheel offsets synced from entity positions");
    }

    void Physics::UpdateWheelTransforms()
    {
        if (m_body_type != BodyType::Vehicle || !Engine::IsFlagSet(EngineMode::Playing))
            return;

        // get steering angle from vehicle system
        float steering = car::get_steering();
        const float max_steering_angle = 35.0f * math::deg_to_rad;
        float steering_angle = steering * max_steering_angle;
        
        // get suspension parameters for position calculation
        float suspension_height = car::cfg.suspension_height;
        float suspension_travel = car::cfg.suspension_travel;

        // update each wheel entity using physics rotation and position data
        for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
        {
            Entity* wheel_entity = m_wheel_entities[i];
            if (!wheel_entity)
                continue;

            bool is_front_wheel = (i == static_cast<int>(WheelIndex::FrontLeft) || i == static_cast<int>(WheelIndex::FrontRight));
            bool is_right_wheel = (i == static_cast<int>(WheelIndex::FrontRight) || i == static_cast<int>(WheelIndex::RearRight));

            // update wheel Y position based on suspension compression
            // compression: 0 = fully extended (wheel at lowest), 1 = fully compressed (wheel at highest)
            float compression = car::get_wheel_compression(i);
            Vector3 current_pos = wheel_entity->GetPositionLocal();
            
            // base Y is at -suspension_height (fully extended position)
            // as compression increases, wheel moves UP by compression * suspension_travel
            // subtract mesh center offset to account for meshes with non-centered origin
            float visual_y = -suspension_height + compression * suspension_travel - m_wheel_mesh_center_offset_y;
            wheel_entity->SetPositionLocal(Vector3(current_pos.x, visual_y, current_pos.z));

            // get wheel rotation from physics (each wheel has its own rotation)
            float wheel_rotation = car::get_wheel_rotation(i);
            Quaternion spin_rotation = Quaternion::FromAxisAngle(Vector3::Right, wheel_rotation);

            // steering rotation for front wheels only (around Y axis)
            Quaternion steer_rotation = Quaternion::Identity;
            if (is_front_wheel)
            {
                steer_rotation = Quaternion::FromAxisAngle(Vector3::Up, steering_angle);
            }
            
            // mirror rotation for right side wheels
            Quaternion mirror_rotation = Quaternion::Identity;
            if (is_right_wheel)
            {
                mirror_rotation = Quaternion::FromAxisAngle(Vector3::Up, math::pi);
            }

            // combine rotations
            Quaternion final_rotation = steer_rotation * spin_rotation * mirror_rotation;
            wheel_entity->SetRotationLocal(final_rotation);
        }

        // note: chassis entity is a child of vehicle_entity, which already follows car::body
        // so the chassis inherits the physics transform automatically - no extra update needed
    }

    void Physics::Create()
    {
        // clear previous state
        Remove();

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        PxScene* scene     = static_cast<PxScene*>(PhysicsWorld::GetScene());

        // material - shared across all shapes (if multiple shapes are used)
        m_material = physics->createMaterial(m_friction, m_friction_rolling, m_restitution);

        // body/controller
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
            desc.radius           = controller_radius;
            desc.height           = standing_height;
            desc.climbingMode     = PxCapsuleClimbingMode::eEASY; // easier handling on steps/slopes
            desc.stepOffset       = 0.3f; // keep under half a meter for better stepping
            desc.slopeLimit       = cosf(60.0f * math::deg_to_rad); // 60° climbable slope
            desc.contactOffset    = 0.01f; // allows early contact without tunneling
            desc.upDirection      = PxVec3(0, 1, 0); // up is y
            desc.nonWalkableMode  = PxControllerNonWalkableMode::ePREVENT_CLIMBING_AND_FORCE_SLIDING;
            
            // optional but recommended: disable callbacks unless needed
            desc.reportCallback   = nullptr;
            desc.behaviorCallback = nullptr;
            
            // apply initial position
            const Vector3& pos = GetEntity()->GetPosition();
            desc.position      = PxExtendedVec3(pos.x, pos.y, pos.z);
            
            // assign material
            desc.material = static_cast<PxMaterial*>(m_material);
            
            // create controller
            m_controller = static_cast<PxControllerManager*>(controller_manager)->createController(desc);
            if (!m_controller)
            {
                SP_LOG_ERROR("failed to create capsule controller");
                static_cast<PxMaterial*>(m_material)->release();
                m_material = nullptr;
                return;
            }
            
            // note: the controller internally references the material, so don't release m_material here
            // it will be released in Remove() when the controller is destroyed
        }
        else if (m_body_type == BodyType::Vehicle)
        {
            // create vehicle
            if (car::create(physics, scene))
            {
                // store the rigid body actor
                m_actors.resize(1, nullptr);
                m_actors[0] = car::body;
                m_actors_active.resize(1, true);
                
                // set initial position - use physics-calculated height for proper ground contact
                // car::create already set correct body height accounting for suspension sag
                // we just use entity's X and Z, but keep the physics Y
                Vector3 pos = GetEntity()->GetPosition();
                PxTransform current_pose = car::body->getGlobalPose();
                car::body->setGlobalPose(PxTransform(PxVec3(pos.x, current_pose.p.y, pos.z)));
                
                // store user data for raycasts
                car::body->userData = reinterpret_cast<void*>(GetEntity());
                
                SP_LOG_INFO("vehicle physics body created successfully");
            }
            else
            {
                SP_LOG_ERROR("failed to create vehicle physics body");
            }
        }
        else
        {
            // mesh
            if (m_body_type == BodyType::Mesh)
            {
                Renderable* renderable = GetEntity()->GetComponent<Renderable>();
                if (!renderable)
                {
                    SP_LOG_ERROR("No Renderable component found for mesh shape");
                    return;
                }

                // get geometry
                vector<uint32_t> indices;
                vector<RHI_Vertex_PosTexNorTan> vertices;
                renderable->GetGeometry(&indices, &vertices);
                if (vertices.empty() || indices.empty())
                {
                    SP_LOG_ERROR("Empty vertex or index data for mesh shape");
                    return;
                }

                // simplify geometry
                const float volume        = renderable->GetBoundingBox().GetVolume();
                const float max_volume    = 100000.0f;
                // simplify geometry based on volume (larger objects get more detail)
                const float volume_factor       = clamp(volume / max_volume, 0.0f, 1.0f);
                const size_t min_index_count    = min<size_t>(indices.size(), 256);
                const size_t max_index_count    = 16'000;
                const size_t target_index_count = clamp<size_t>(static_cast<size_t>(indices.size() * volume_factor), min_index_count, max_index_count);
                geometry_processing::simplify(indices, vertices, target_index_count, false, false);
                
                // warn if we hit the complexity cap (original mesh was very detailed)
                if (indices.size() > max_index_count && target_index_count == max_index_count)
                {
                    SP_LOG_WARNING("Mesh '%s' was simplified to %zu indices. It's still complex and may impact physics performance.", renderable->GetEntity()->GetObjectName().c_str(), target_index_count);
                }

                // convert vertices to physx format
                vector<PxVec3> px_vertices;
                px_vertices.reserve(vertices.size());
                Vector3 scale = GetEntity()->GetScale();
                for (const auto& vertex : vertices)
                {
                    px_vertices.emplace_back(vertex.pos[0] * scale.x, vertex.pos[1] * scale.y, vertex.pos[2] * scale.z);
                }

                // cooking parameters
                PxTolerancesScale _scale;
                _scale.length                          = 1.0f;                         // 1 unit = 1 meter
                Vector3 gravity                        = PhysicsWorld::GetGravity();
                _scale.speed                           = sqrtf(gravity.x * gravity.x + gravity.y * gravity.y + gravity.z * gravity.z); // magnitude of gravity vector
                PxCookingParams params(_scale);         
                params.areaTestEpsilon                 = 0.06f * _scale.length * _scale.length;
                params.planeTolerance                  = 0.0007f;
                params.convexMeshCookingType           = PxConvexMeshCookingType::eQUICKHULL;
                params.suppressTriangleMeshRemapTable  = false;
                params.buildTriangleAdjacencies        = true;
                params.buildGPUData                    = false;
                params.meshPreprocessParams           |= PxMeshPreprocessingFlag::eWELD_VERTICES;
                params.meshWeldTolerance               = 0.01f;
                params.meshAreaMinLimit                = 0.0f;
                params.meshEdgeLengthMaxLimit          = 500.0f;
                params.gaussMapLimit                   = 32;
                params.maxWeightRatioInTet             = FLT_MAX;

                PxInsertionCallback* insertion_callback = PxGetStandaloneInsertionCallback();
                if (IsStatic() || IsKinematic()) // triangle mesh for exact collision (static or kinematic)
                {
                    PxTriangleMeshDesc mesh_desc;
                    mesh_desc.points.count     = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride    = sizeof(PxVec3);
                    mesh_desc.points.data      = px_vertices.data();
                    mesh_desc.triangles.count  = static_cast<PxU32>(indices.size() / 3);
                    mesh_desc.triangles.stride = 3 * sizeof(PxU32);
                    mesh_desc.triangles.data   = indices.data();

                    // create
                    PxTriangleMeshCookingResult::Enum condition;
                    m_mesh = PxCreateTriangleMesh(params, mesh_desc, *insertion_callback, &condition);
                    if (condition != PxTriangleMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create triangle mesh: %d", condition);
                        if (m_mesh)
                        {
                            static_cast<PxTriangleMesh*>(m_mesh)->release();
                            m_mesh = nullptr;
                        }
                        return;
                    }
                }
                else // dynamic: convex mesh
                {
                    PxConvexMeshDesc mesh_desc;
                    mesh_desc.points.count  = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride = sizeof(PxVec3);
                    mesh_desc.points.data   = px_vertices.data();
                    mesh_desc.flags         = PxConvexFlag::eCOMPUTE_CONVEX;

                    // create
                    PxConvexMeshCookingResult::Enum condition;
                    m_mesh = PxCreateConvexMesh(params, mesh_desc, *insertion_callback, &condition);
                    if (!m_mesh || condition != PxConvexMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create convex mesh: %d", condition);
                        if (m_mesh)
                        {
                            static_cast<PxConvexMesh*>(m_mesh)->release();
                            m_mesh = nullptr;
                        }
                        return;
                    }
                }
            }

            CreateBodies();
        }
    }

    void Physics::CreateBodies()
    {
        PxPhysics* physics      = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        Renderable* renderable  = GetEntity()->GetComponent<Renderable>();
        
        if (!renderable)
        {
            SP_LOG_ERROR("No Renderable component found for physics body creation");
            return;
        }

        // create bodies and shapes
        const uint32_t instance_count = renderable->GetInstanceCount();
        m_actors.resize(instance_count, nullptr);
        m_actors_active.resize(instance_count, true); // all actors start active
        for (uint32_t i = 0; i < instance_count; i++)
        {
            math::Matrix transform = renderable->HasInstancing() ? renderable->GetInstance(i, true) : GetEntity()->GetMatrix();
            PxTransform pose(
                PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
            );
            PxRigidActor* actor = nullptr;
            if (IsStatic())
            {
                actor = physics->createRigidStatic(pose);
            }
            else
            {
                actor = physics->createRigidDynamic(pose);
                PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>();
                if (dynamic)
                {
                    dynamic->setMass(m_mass);
                    dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, !m_is_kinematic); // kinematics don't support ccd
                    dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, m_is_kinematic);
                    if (m_center_of_mass != Vector3::Zero)
                    {
                        PxVec3 p(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                        PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &p);
                    }
                    dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
                }
            }
        
            PxShape* shape       = nullptr;
            PxMaterial* material = static_cast<PxMaterial*>(m_material);
            switch (m_body_type)
            {
                case BodyType::Box:
                {
                    Vector3 scale = GetEntity()->GetScale();
                    PxBoxGeometry geometry(scale.x * 0.5f, scale.y * 0.5f, scale.z * 0.5f);
                    shape = physics->createShape(geometry, *material);
                    break;
                }
                case BodyType::Sphere:
                {
                    Vector3 scale = GetEntity()->GetScale();
                    float radius  = max(max(scale.x, scale.y), scale.z) * 0.5f;
                    PxSphereGeometry geometry(radius);
                    shape = physics->createShape(geometry, *material);
                    break;
                }
                case BodyType::Plane:
                {
                    PxPlaneGeometry geometry;
                    shape = physics->createShape(geometry, *material);
                    shape->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
                    break;
                }
                case BodyType::Capsule:
                {
                    Vector3 scale     = GetEntity()->GetScale();
                    float radius      = max(scale.x, scale.z) * 0.5f;
                    float half_height = scale.y * 0.5f;
                    PxCapsuleGeometry geometry(radius, half_height);
                    shape = physics->createShape(geometry, *material);
                    shape->setLocalPose(PxTransform(PxVec3(0, 0, 0), PxQuat(PxHalfPi, PxVec3(0, 0, 1))));
                    break;
                }
                case BodyType::Mesh:
                {
                    if (m_mesh)
                    {
                        if (IsStatic() || IsKinematic())
                        {
                            Vector3 scale = renderable->HasInstancing() ? renderable->GetInstance(i, false).GetScale() : Vector3::One;
                            PxMeshScale mesh_scale(PxVec3(scale.x, scale.y, scale.z)); // this is a runtime transform, cheap for statics but it won't be reflected for the internal baked shape (raycasts etc)
                            PxTriangleMeshGeometry geometry(static_cast<PxTriangleMesh*>(m_mesh), mesh_scale);
                            shape = physics->createShape(geometry, *material);
                        }
                        else
                        {
                            PxConvexMeshGeometry geometry(static_cast<PxConvexMesh*>(m_mesh));
                            shape = physics->createShape(geometry, *material);
                        }
                    }
                    break;
                }
            }
            
            if (shape)
            {
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                actor->attachShape(*shape);
                shape->release(); // release shape reference (actor owns it now)
            }

            if (actor)
            {
                actor->userData = reinterpret_cast<void*>(GetEntity());
                PhysicsWorld::AddActor(actor);
            }

            m_actors[i] = actor;
        }
    }
}
