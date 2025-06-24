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
        // controller
        if (m_controller)
        {
            static_cast<PxController*>(m_controller)->release();
            m_controller = nullptr;
        }

        // bodies
        for (auto* body : m_bodies)
        {
            PxRigidActor* actor = static_cast<PxRigidActor*>(body);
            PxScene* scene      = static_cast<PxScene*>(Physics::GetScene());
            if (actor->getScene())
            {
                scene->removeActor(*actor);
            }
            actor->release();
        }
        m_bodies.clear();

        // shape
        if (m_shape)
        {
            static_cast<PxShape*>(m_shape)->release();
            m_shape = nullptr;
        }

        // material
        if (m_material)
        {
            static_cast<PxMaterial*>(m_material)->release();
            m_material = nullptr;
        }
    }

    void PhysicsBody::OnTick()
    {
         // controller
        if (m_body_type == BodyType::Controller)
        {
            if (Engine::IsFlagSet(EngineMode::Playing))
            {
                float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());
                m_velocity.y += Physics::GetGravity().y * delta_time;
                PxVec3 displacement(0.0f, m_velocity.y * delta_time, 0.0f);
                PxControllerFilters filters;
                filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
                PxControllerCollisionFlags collision_flags = static_cast<PxCapsuleController*>(m_controller)->move(displacement, 0.001f, delta_time, filters);
                if (collision_flags & PxControllerCollisionFlag::eCOLLISION_DOWN)
                {
                    m_velocity.y = 0.0f;
                }
                PxExtendedVec3 pos = static_cast<PxCapsuleController*>(m_controller)->getPosition();
                GetEntity()->SetPosition(Vector3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z)));
            }
            else
            {
                Vector3 entity_pos = GetEntity()->GetPosition();
                static_cast<PxCapsuleController*>(m_controller)->setPosition(PxExtendedVec3(entity_pos.x, entity_pos.y, entity_pos.z));
                m_velocity = Vector3::Zero;
            }
        }
        else // regular bodies
        {
            Renderable* renderable                = GetEntity()->GetComponent<Renderable>();
            const vector<math::Matrix>& instances = renderable ? renderable->GetInstances() : vector<math::Matrix>();
            bool has_instances                    = !instances.empty();

            if (!renderable->HasInstancing()) // temp fix as things seem to break for instanced bodies
            {
                for (size_t i = 0; i < m_bodies.size(); i++)
                {
                    PxRigidActor* actor = static_cast<PxRigidActor*>(m_bodies[i]);

                    if (Engine::IsFlagSet(EngineMode::Playing))
                    {
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
                    else
                    {
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
                        if (PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>())
                        {
                            dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                            dynamic->setAngularVelocity(PxVec3(0, 0, 0));
                        }
                    }
                }
            }

            // distance-based activation/deactivation
            if (m_mass == 0.0f && m_body_type != BodyType::Controller)
            {
                if (Camera* camera = World::GetCamera())
                {
                    const Vector3 camera_pos = camera->GetEntity()->GetPosition();
                    PxScene* scene = static_cast<PxScene*>(Physics::GetScene());
            
                    for (void* body : m_bodies)
                    {
                        PxRigidActor* actor   = static_cast<PxRigidActor*>(body);
                        size_t instance_index = reinterpret_cast<size_t>(actor->userData);
            
                        Renderable* renderable          = GetEntity()->GetComponent<Renderable>();
                        const BoundingBox& bounding_box = renderable->HasInstancing() ? renderable->GetBoundingBoxInstance(static_cast<uint32_t>(instance_index)) : renderable->GetBoundingBox();
                        const Vector3 closest_point     = bounding_box.GetClosestPoint(camera_pos);
                        const float distance_to_camera  = Vector3::Distance(camera_pos, closest_point);
            
                        const float distance_deactivate = 80.0f;
                        const float distance_activate   = 40.0f;

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

    void PhysicsBody::Serialize(FileStream* stream)
    {
        stream->Write(m_mass);
        stream->Write(m_friction);
        stream->Write(m_friction_rolling);
        stream->Write(m_restitution);
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
        stream->Read(&m_position_lock);
        stream->Read(&m_rotation_lock);
        m_body_type = BodyType(stream->ReadAs<uint32_t>());
        stream->Read(&m_center_of_mass);

        Create();
    }

    void PhysicsBody::SetMass(float mass)
    {
        m_mass = max(mass, 0.0f);
        for (auto* body : m_bodies)
        {
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setMass(m_mass);
            }
        }
    }

    void PhysicsBody::SetFriction(float friction)
    {
        if (m_friction == friction)
            return;
    
        m_friction = friction;

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
    }

    void PhysicsBody::SetFrictionRolling(float friction_rolling)
    {
        if (m_friction_rolling == friction_rolling)
            return;
    
        m_friction_rolling = friction_rolling;
    
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
    }

    void PhysicsBody::SetRestitution(float restitution)
    {
        if (m_restitution == restitution)
            return;
    
        m_restitution = restitution;
    
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
    }

    void PhysicsBody::SetLinearVelocity(const Vector3& velocity) const
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

    Vector3 PhysicsBody::GetLinearVelocity() const
    {
        if (m_bodies.empty())
            return Vector3::Zero;

        if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(m_bodies[0])->is<PxRigidDynamic>())
        {
            PxVec3 velocity = dynamic->getLinearVelocity();
            return Vector3(velocity.x, velocity.y, velocity.z);
        }

        return Vector3::Zero;
    }

    void PhysicsBody::SetAngularVelocity(const Vector3& velocity) const
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

    void PhysicsBody::ApplyForce(const Vector3& force, PhysicsForce mode) const
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

    void PhysicsBody::SetPositionLock(bool lock)
    {
        SetPositionLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetPositionLock(const Vector3& lock)
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

    void PhysicsBody::SetRotationLock(bool lock)
    {
        SetRotationLock(lock ? Vector3::One : Vector3::Zero);
    }

    void PhysicsBody::SetRotationLock(const Vector3& lock)
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

    void PhysicsBody::SetCenterOfMass(const Vector3& center_of_mass)
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
            SP_LOG_WARNING("RayTraceIsGrounded: This method is not applicable for non-controller bodies.");
        }

        return false;
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
        PxPhysics* physics = static_cast<PxPhysics*>(Physics::GetPhysics());
        PxScene* scene     = static_cast<PxScene*>(Physics::GetScene());

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
            // bodies
            for (auto* body : m_bodies)
            {
                PxRigidActor* actor = static_cast<PxRigidActor*>(body);
                PxShape* shape;
                actor->getShapes(&shape, 1);
                if (shape)
                {
                    actor->detachShape(*shape);
                    shape->release();
                }
                if (actor->getScene())
                {
                    scene->removeActor(*actor);
                }
                actor->release();
            }
            m_bodies.clear();

            // material
            if (m_material)
            {
                static_cast<PxMaterial*>(m_material)->release();
                m_material = nullptr;
            }
            m_material = physics->createMaterial(m_friction, m_friction_rolling, m_restitution);

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
                size_t target_index_count = 1024;
                geometry_processing::simplify(indices, vertices, target_index_count, false);

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
                _scale.length                          = 1.0f;                    // 1 unit = 1 meter
                _scale.speed                           = Physics::GetGravity().y; // gravity is in meters per second
                PxCookingParams params(_scale);         
                params.areaTestEpsilon                 = 0.06f * _scale.length * _scale.length;
                params.planeTolerance                  = 0.0007f;
                params.convexMeshCookingType           = PxConvexMeshCookingType::eQUICKHULL;
                params.suppressTriangleMeshRemapTable  = false;
                params.buildTriangleAdjacencies        = false;
                params.buildGPUData                    = false;
                //params.meshPreprocessParams           |= PxMeshPreprocessingFlag::eWELD_VERTICES;
                //params.meshWeldTolerance               = 0.001f;
                params.meshAreaMinLimit                = 0.0f;
                params.meshEdgeLengthMaxLimit          = 500.0f;
                params.gaussMapLimit                   = 32;
                params.maxWeightRatioInTet             = FLT_MAX;

                PxInsertionCallback* insertion_callback = PxGetStandaloneInsertionCallback();
                if (m_mass == 0.0f) // static: triangle mesh
                {
                    PxTriangleMeshDesc mesh_desc;
                    mesh_desc.points.count     = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride    = sizeof(PxVec3);
                    mesh_desc.points.data      = px_vertices.data();
                    mesh_desc.triangles.count  = static_cast<PxU32>(indices.size() / 3);
                    mesh_desc.triangles.stride = 3 * sizeof(PxU32);
                    mesh_desc.triangles.data   = indices.data();

                    // validate
                    if (!PxValidateTriangleMesh(params, mesh_desc))
                    {
                        SP_LOG_WARNING("Triangle mesh validation failed");
                        return;
                    }

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
                    if (!PxValidateConvexMesh(params, mesh_desc))
                    {
                        SP_LOG_WARNING("Convex mesh validation failed");
                        return;
                    }
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

    void PhysicsBody::CreateBodies()
    {
        PxPhysics* physics                    = static_cast<PxPhysics*>(Physics::GetPhysics());
        PxScene* scene                        = static_cast<PxScene*>(Physics::GetScene());
        Renderable* renderable                = GetEntity()->GetComponent<Renderable>();
        const vector<math::Matrix>& instances = renderable ? renderable->GetInstances() : vector<math::Matrix>();
        size_t instance_count                 = instances.empty() ? 1 : instances.size();

        if (m_bodies.size() != instance_count)
        {
            // clean up existing bodies
            for (auto* body : m_bodies)
            {
                PxRigidActor* actor = static_cast<PxRigidActor*>(body);
                if (actor->getScene())
                {
                    scene->removeActor(*actor);
                }
                actor->release();
            }
            m_bodies.clear();

            // create new bodies
            for (size_t i = 0; i < instance_count; i++)
            {
                math::Matrix transform = instances.empty() ? GetEntity()->GetMatrix() : instances[i];
                PxTransform pose(
                    PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                    PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
                );
                PxRigidActor* actor;
                if (m_mass == 0.0f)
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
                        dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, true);
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
                            if (m_mass == 0.0f)
                            {
                                PxTriangleMeshGeometry geometry(static_cast<PxTriangleMesh*>(m_mesh));
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
                }
                scene->addActor(*actor);

                // store the instance index in userData
                actor->userData = reinterpret_cast<void*>(i);

                m_bodies.push_back(actor);
            }
        }
        else
        {
            // update poses if not playing
            if (!Engine::IsFlagSet(EngineMode::Playing))
            {
                for (size_t i = 0; i < instance_count; i++)
                {
                    math::Matrix transform = instances.empty() ? GetEntity()->GetMatrix() : instances[i];
                    PxTransform pose(
                        PxVec3(transform.GetTranslation().x, transform.GetTranslation().y, transform.GetTranslation().z),
                        PxQuat(transform.GetRotation().x, transform.GetRotation().y, transform.GetRotation().z, transform.GetRotation().w)
                    );
                    static_cast<PxRigidActor*>(m_bodies[i])->setGlobalPose(pose);
                }
            }
        }
    }
}
