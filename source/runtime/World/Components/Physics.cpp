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
#include "Physics.h"
#include "Renderable.h"
#include "Camera.h"
#include "../Entity.h"
#include "../../RHI/RHI_Vertex.h"
#include "../../Physics/PhysicsWorld.h"
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
    namespace vehicle
    {
        // it uses the new vehicle2 api
    }

    namespace
    {
        const float distance_deactivate = 80.0f;
        const float distance_activate   = 40.0f;
        const float standing_height     = 1.8f;
        const float crouch_height       = 0.7f;

        void* controller_manager = nullptr;
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
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_body_type, SetBodyType, BodyType);
    }

    Physics::~Physics()
    {
        OnRemove();
    }

    void Physics::OnInitialize()
    {
        Component::OnInitialize();
    }

    void Physics::OnRemove()
    {
        if (m_controller)
        {
            static_cast<PxController*>(m_controller)->release();
            m_controller = nullptr;
            m_material   = nullptr; // the controller owns the material
        }

        for (auto* body : m_bodies)
        {
            if (body)
            {
                PxRigidActor* actor = static_cast<PxRigidActor*>(body);
                PxScene* scene      = static_cast<PxScene*>(PhysicsWorld::GetScene());
        
                if (actor->getScene())
                {
                    scene->removeActor(*actor);
                }

                actor->release();
            }
        }
        m_bodies.clear();

        if (PxMaterial* material = static_cast<PxMaterial*>(m_material))
        {
            material->release();
            m_material = nullptr;
        }
    }

    void Physics::OnTick()
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
                GetEntity()->SetPosition(Vector3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)));

                // compute velocity for xz
                m_velocity.x = (pos.x - pos_previous.x) / delta_time;
                m_velocity.z = (pos.z - pos_previous.z) / delta_time;
            }
            else
            {
                Vector3 entity_pos = GetEntity()->GetPosition();
                static_cast<PxCapsuleController*>(m_controller)->setPosition(PxExtendedVec3(entity_pos.x, entity_pos.y, entity_pos.z));
                m_velocity = Vector3::Zero;
            }
        }
        else if (!m_is_static)
        {
            Renderable* renderable = GetEntity()->GetComponent<Renderable>();
            const static vector<math::Matrix> empty_instances;
            const vector<math::Matrix>& instances = renderable ? renderable->GetInstances() : empty_instances;
            bool has_instances = !instances.empty();
            for (size_t i = 0; i < m_bodies.size(); i++)
            {
                PxRigidActor* actor = static_cast<PxRigidActor*>(m_bodies[i]);
                PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>();
                if (Engine::IsFlagSet(EngineMode::Playing))
                {
                    if (m_is_kinematic && dynamic)
                    {
                        // Sync entity -> PhysX (kinematic target)
                        math::Matrix transform;
                        if (has_instances && i < instances.size())
                        {
                            transform = instances[i];
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
                        // Sync PhysX -> entity (simulated dynamic)
                        PxTransform pose = actor->getGlobalPose();
                        math::Matrix transform = math::Matrix::CreateTranslation(Vector3(pose.p.x, pose.p.y, pose.p.z)) * math::Matrix::CreateRotation(Quaternion(pose.q.x, pose.q.y, pose.q.z, pose.q.w));
                        if (has_instances && renderable && i < instances.size())
                        {
                            renderable->SetInstance(static_cast<uint32_t>(i), transform);
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
                    // Editor mode: Sync entity -> PhysX, reset velocities only for non-kinematics
                    math::Matrix transform;
                    if (has_instances && i < instances.size())
                    {
                        transform = instances[i];
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

        // distance-based activation/deactivation
        if (m_body_type != BodyType::Controller && m_is_static)
        {
            if (Camera* camera = World::GetCamera())
            {
                const Vector3 camera_pos = camera->GetEntity()->GetPosition();
                PxScene* scene           = static_cast<PxScene*>(PhysicsWorld::GetScene());

                if (Renderable* renderable = GetEntity()->GetComponent<Renderable>())
                {
                    for (uint32_t i = 0; i < static_cast<uint32_t>(m_bodies.size()); i++)
                    {
                        if (PxRigidActor* actor = static_cast<PxRigidActor*>(m_bodies[i]))
                        { 
                            const BoundingBox& bounding_box = renderable->HasInstancing() ? renderable->GetBoundingBoxInstance(static_cast<uint32_t>(i)) : renderable->GetBoundingBox();
                            const Vector3 closest_point     = bounding_box.GetClosestPoint(camera_pos);
                            const float distance_to_camera  = Vector3::Distance(camera_pos, closest_point);
                            if (distance_to_camera > distance_deactivate && actor->getScene())
                            {
                                scene->removeActor(*actor);
                            }
                            else if (distance_to_camera <= distance_activate && !actor->getScene())
                            {
                                scene->addActor(*actor);
                            }
                        }
                    }
                }
            }
        }

        // handle water body buoyancy
        if (m_body_type == BodyType::Water && Engine::IsFlagSet(EngineMode::Playing))
        {
            float water_density       = 1000.0f; // default water density (kg/m³)
            PxScene* scene            = static_cast<PxScene*>(PhysicsWorld::GetScene());
            PxRigidActor* water_actor = static_cast<PxRigidActor*>(m_bodies[0]);
            PxShape* shape;
            water_actor->getShapes(&shape, 1);
            if (!shape)
                return;

            // perform overlap query
            PxGeometryHolder geometry = shape->getGeometry();
            PxTransform shape_pose = water_actor->getGlobalPose();
            PxOverlapBufferN<10> hit_buffer; // 10 overlapping actors max
            PxQueryFilterData filter_data;
            filter_data.flags = PxQueryFlag::eDYNAMIC | PxQueryFlag::eSTATIC; // dynamic and static (controller)
            
            if (scene->overlap(geometry.any(), shape_pose, hit_buffer, filter_data))
            {
                for (PxU32 i = 0; i < hit_buffer.nbTouches; ++i)
                {
                    PxOverlapHit& hit = hit_buffer.touches[i];
                    PxRigidActor* other_actor = hit.actor;
                    if (!other_actor || other_actor == water_actor)
                        continue;
            
                    Entity* other_entity = static_cast<Entity*>(other_actor->userData);
                    if (!other_entity)
                        continue;
            
                    Physics* other_physics = other_entity->GetComponent<Physics>();
                    if (!other_physics)
                        continue;
            
                    BodyType body_type = other_physics->GetBodyType();
                    if (other_physics->IsStatic() && body_type != BodyType::Controller)
                        continue;
            
                    float vertical_velocity             = other_physics->GetLinearVelocity().y;
                    float mass                          = other_physics->GetMass();
                    float delta_time                    = static_cast<float>(Timer::GetDeltaTimeSec());
                    const float desired_upward_velocity = 1.2f;
                    float velocity_error                = desired_upward_velocity - vertical_velocity;
                    float force_to_apply                = velocity_error * mass / delta_time;
                    const float max_force               = 500.0f;
                    force_to_apply                      = clamp(force_to_apply, -max_force, max_force);
                    Vector3 buoyancy_force(0.0f, force_to_apply, 0.0f);

                    if (body_type == BodyType::Controller)
                    {
                        other_physics->m_velocity.y += (force_to_apply / mass) * delta_time;
                    }
                    else
                    {
                        other_physics->ApplyForce(buoyancy_force, PhysicsForce::Constant);
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
                    float cylinder_height = scale.y - 2.0f * radius; // height of cylindrical part
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
        for (auto* body : m_bodies)
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

        for (auto* body : m_bodies)
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
        
        if (m_bodies.empty())
            return Vector3::Zero;
            
        if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(m_bodies[0])->is<PxRigidDynamic>())
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

        for (auto* body : m_bodies)
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

        for (auto* body : m_bodies)
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
        for (auto* body : m_bodies)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
                if (m_position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
                if (m_position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
                if (m_position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
                if (m_rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
                if (m_rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
                if (m_rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
                dynamic->setRigidDynamicLockFlags(flags);
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
        for (auto* body : m_bodies)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
                if (m_position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
                if (m_position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
                if (m_position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
                if (m_rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
                if (m_rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
                if (m_rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
                dynamic->setRigidDynamicLockFlags(flags);
            }
        }
    }

    void Physics::SetCenterOfMass(const Vector3& center_of_mass)
    {
        if (m_body_type == BodyType::Controller)
            return;
    
        m_center_of_mass = center_of_mass;
        for (auto* body : m_bodies)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                if (m_center_of_mass != Vector3::Zero)
                {
                    PxVec3 p(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                    PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &p);
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
        
        // relative local position to the top of the capsule (from the capsule's center)
        return Vector3(0.0f, (height * 0.5f) + radius, 0.0f);
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

    void Physics::Create()
    {
        // clear previous state
        OnRemove();

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
            desc.radius           = 0.5f; // stable size for ground contact
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
                desc.material->release();
                return;
            }
            
            // cleanup
            desc.material->release();
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
                const float volume_factor = clamp(volume / max_volume, 0.0f, 1.0f); // aka simplification ratio
                size_t min_index_count    = min<size_t>(indices.size(), 256);
                size_t target_index_count = clamp<size_t>(static_cast<size_t>(indices.size() * volume_factor), min_index_count, 16'000);
                geometry_processing::simplify(indices, vertices, target_index_count, false, false);
                if (target_index_count > 16000)
                {
                    SP_LOG_WARNING("Mesh '%s' was simplified to %d indices. It's still complex and may impact physics performance.", renderable->GetEntity()->GetObjectName().c_str(), target_index_count);
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
                _scale.speed                           = PhysicsWorld::GetGravity().y; // gravity is in meters per second
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
        PxPhysics* physics                    = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        PxScene* scene                        = static_cast<PxScene*>(PhysicsWorld::GetScene());
        Renderable* renderable                = GetEntity()->GetComponent<Renderable>();
        const vector<math::Matrix>& instances = renderable ? renderable->GetInstances() : vector<math::Matrix>();
        size_t instance_count                 = instances.empty() ? 1 : instances.size();

        // create bodies and shapes
        m_bodies.resize(instance_count, nullptr);
        for (size_t i = 0; i < instance_count; i++)
        {
            math::Matrix transform = instances.empty() ? GetEntity()->GetMatrix() : instances[i];
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
                    PxRigidDynamicLockFlags flags = PxRigidDynamicLockFlags(0);
                    if (m_position_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_X;
                    if (m_position_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Y;
                    if (m_position_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_LINEAR_Z;
                    if (m_rotation_lock.x) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_X;
                    if (m_rotation_lock.y) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Y;
                    if (m_rotation_lock.z) flags |= PxRigidDynamicLockFlag::eLOCK_ANGULAR_Z;
                    dynamic->setRigidDynamicLockFlags(flags);
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
                            Vector3 scale = instance_count > 1 ? instances[i].GetScale() : Vector3::One;
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
                case BodyType::Water:
                {
                    Vector3 extents = renderable->GetBoundingBox().GetExtents();

                    // controls the body overlap volume
                    const float height_extent = 2.0f;
                
                    // build geometry that extends downward from the water surface
                    PxBoxGeometry geometry(extents.x, height_extent * 0.5f, extents.z);
                
                    // offset the shape so its top aligns with the visual water surface
                    PxTransform offset(PxVec3(0.0f, -height_extent * 0.5f, 0.0f));
                
                    // create shape and assign offset
                    shape = physics->createShape(geometry, *material);
                    shape->setLocalPose(offset);
                    shape->setFlag(PxShapeFlag::eSIMULATION_SHAPE, false); // disable simulation
                    shape->setFlag(PxShapeFlag::eTRIGGER_SHAPE, true);     // enable trigger
                    break;
                }
            }
            if (shape)
            {
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                actor->attachShape(*shape);
            }
            actor->userData = reinterpret_cast<void*>(GetEntity());
            scene->addActor(*actor);
        
            m_bodies[i] = actor;
        }
    }
}
