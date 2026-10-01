
/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "../../profiling/WorldWork.h"
#include "Spline.h"
#include <unordered_set>
#include "Physics.h"
#include "../../profiling/Profiler.h"
#include "Render.h"
#include "Camera.h"
#include "Terrain.h"
#include "../Entity.h"
#include "../../rhi/RHI_Vertex.h"
#include "../../physics/PhysicsWorld.h"
#ifdef DEBUG
    #define _DEBUG 1
    #undef NDEBUG
#else
    #define NDEBUG 1
    #undef _DEBUG
#endif
#define PX_PHYSX_STATIC_LIB
#include <physx/PxPhysicsAPI.h>
#include "../Weather.h"
#include "../../geometry/Mesh.h"
#include "../../geometry/GeneratedCache.h"
#include "../../geometry/GeometryProcessing.h"
#include "../../rendering/Renderer.h"
#include "../../rendering/Material.h"
#include "../../rendering/GeometryBuffer.h"
#include "../../core/ProgressTracker.h"
#include "../../core/ThreadPool.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
using namespace physx;
//============================

#include "../../physics/PhysicsConversions.h"
using namespace spartan::physics_detail;

namespace spartan
{
    namespace
    {
        const float distance_deactivate = 80.0f;
        const float distance_activate   = 40.0f;

        // 1.8m total height for an average adult, a capsule totals cylinder_height + 2 * radius
        const float controller_radius   = 0.25f;
        const float standing_height     = 1.3f;  // cylinder height (total = 1.3 + 0.5 = 1.8m)
        const float crouch_height       = 0.5f;  // cylinder height when crouching (total = 0.5 + 0.5 = 1.0m)

        // derivatives
        const float distance_deactivate_squared = distance_deactivate * distance_deactivate;
        const float distance_activate_squared   = distance_activate * distance_activate;

        PxControllerManager* controller_manager = nullptr;

        bool outside_collision_prepare_range(Entity* entity, bool remember = false)
        {
            Camera* camera = World::GetCamera();
            Render* render = entity->GetComponent<Render>();
            if (!camera || !render)
            {
                return false;
            }
            const Vector3 position = camera->GetEntity()->GetPosition();
            const float distance_squared = Vector3::DistanceSquared(position, render->GetBoundingBox().GetClosestPoint(position));
            const bool outside = distance_squared > distance_deactivate_squared;
            if (outside && remember && std::isfinite(distance_squared))
            {
                const float distance = sqrtf(distance_squared);
                // Distance to a fixed box is 1-Lipschitz: the camera cannot reach
                // the preparation boundary before moving this far. Round inward.
                const float margin = max(0.01f, distance * 0.00001f);
                entity->SleepPhysicsPreTick(position, max(0.0f, distance - distance_deactivate - margin));
            }
            return outside;
        }

        // fft water buoyancy, applied once per fixed physics step to every submerged dynamic body
        namespace buoyancy
        {
            const float water_density   = 1000.0f; // kg per cubic meter
            const float max_accel       = 30.0f;   // cap so featherweight bodies don't rocket out of the water
            const float drag_horizontal = 2.0f;    // velocity damping per second at full submersion
            const float drag_vertical   = 20.0f;   // near critical for the buoyancy spring, floats settle onto the surface instead of bouncing
            const float drag_angular    = 4.0f;    // spin damping per second at full submersion
            const float align_gain      = 10.0f;   // angular acceleration per radian of tilt toward the wave normal, makes floats pitch and roll with the swell
            vector<Physics*> bodies;               // guarded by the physx mutex
            vector<Physics*> floating_bodies;      // created dynamic bodies eligible for buoyancy
        }

    }

    Physics::Physics(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_static, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_kinematic, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_mass, SetMass, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_friction, SetFriction, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_friction_rolling, SetFrictionRolling, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_restitution, SetRestitution, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_position_lock, SetPositionLock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_rotation_lock, SetRotationLock, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_center_of_mass, SetCenterOfMass, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_velocity, Vector3);
        // runtime physx handles must not be copied through generic component attributes
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_body_type, SetBodyType, BodyType);

        lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
        buoyancy::bodies.push_back(this);
    }

    Physics::~Physics()
    {
        {
            lock_guard<recursive_mutex> lock(PhysicsWorld::GetMutex());
            buoyancy::bodies.erase(remove(buoyancy::bodies.begin(), buoyancy::bodies.end(), this), buoyancy::bodies.end());
        }

        Remove();
    }

    void Physics::Initialize()
    {
        Component::Initialize();
    }

    BodyType Physics::DetectBodyType()
    {
        Render* render = GetEntity()->GetComponent<Render>();
        if (render)
        {
            // check if the mesh is a simple primitive shape (case-insensitive)
            string mesh_name = render->GetMeshName();
            transform(mesh_name.begin(), mesh_name.end(), mesh_name.begin(), ::tolower);

            if (mesh_name.find("cube") != string::npos || mesh_name.find("box") != string::npos)
            {
                return BodyType::Box;
            }
            else if (mesh_name.find("sphere") != string::npos)
            {
                return BodyType::Sphere;
            }
            else if (mesh_name.find("capsule") != string::npos || mesh_name.find("cylinder") != string::npos)
            {
                return BodyType::Capsule;
            }
            else if (mesh_name.find("plane") != string::npos || mesh_name.find("quad") != string::npos)
            {
                return BodyType::Plane;
            }
            else
            {
                // default to mesh collision for complex shapes
                return BodyType::Mesh;
            }
        }

        // no render - default to box (common for invisible colliders/triggers)
        return BodyType::Box;
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
        m_activation_valid = false;
        // serialize physx writes, async scene loading runs this on worker threads in parallel
        lock_guard<recursive_mutex> physx_lock(PhysicsWorld::GetMutex());

        PhysicsWorld::UnregisterStepCallback(this);
        buoyancy::floating_bodies.erase(remove(buoyancy::floating_bodies.begin(), buoyancy::floating_bodies.end(), this), buoyancy::floating_bodies.end());
        if (m_custom_body)
        {
            m_custom_body->Remove();
            if (m_has_custom_actor) m_actors.clear(); // custom body already released its actors
            m_has_custom_actor = false;
        }

        // release controller if it exists
        // skip if controller_manager was already released during shutdown
        if (m_controller && controller_manager)
        {
            static_cast<PxController*>(m_controller)->release();
            m_controller = nullptr;
        }

        // release all actors
        // skip if physics world was already shut down (scene is null)
        for (auto* body : m_actors)
        {
            if (body && PhysicsWorld::GetScene())
            {
                PxRigidActor* actor = static_cast<PxRigidActor*>(body);
                PhysicsWorld::RemoveActor(actor);
                actor->release();
            }
        }
        m_actors.clear();
        m_actors_active.clear();

        // release material (shared by both controller and regular bodies)
        // skip if physics world was already shut down
        if (m_material && PhysicsWorld::GetPhysics())
        {
            static_cast<PxMaterial*>(m_material)->release();
            m_material = nullptr;
        }

        // Release the component's cooked-mesh reference after its actors/shapes.
        // Remember the actual type: body settings may already have changed before Remove().
        if (m_mesh)
        {
            if (PhysicsWorld::GetPhysics())
            {
                if (m_mesh_is_heightfield)
                    static_cast<PxHeightField*>(m_mesh)->release();
                else if (m_mesh_is_convex)
                    static_cast<PxConvexMesh*>(m_mesh)->release();
                else
                    static_cast<PxTriangleMesh*>(m_mesh)->release();
            }
            m_mesh                 = nullptr;
            m_mesh_is_heightfield  = false;
            m_mesh_is_convex       = false;
        }

        // release cloth state
        m_cloth_particles.clear();
        m_cloth_constraints.clear();
        m_cloth_indices.clear();
        m_cloth_base_vertices.clear();
        m_cloth_weld_map.clear();
        m_cloth_vertex_count         = 0;
        m_cloth_global_vertex_offset = 0;
    }

    void Physics::OnInstancesChanged()
    {
        m_instances_dirty = true;
        GetEntity()->RefreshPreTickGate();
    }

    bool Physics::PrepareWorld()
    {
        // The world loader has joined its worker before calling this. Prepare only the
        // nearby collision that would otherwise consume the first editor frames.
        if (!m_needs_creation || (m_is_static && m_body_type == BodyType::Mesh && m_distance_streaming && outside_collision_prepare_range(GetEntity()))) return true;
        m_needs_creation = false;
        Create();
        return true;
    }

    void Physics::PreTick()
    {
        CountWorldWork(WorldWork::physics_pretick_calls);
        // physx treats a main thread write during worker actor creation as concurrent access and corrupts its pruner tree
        if (ProgressTracker::IsLoading())
        {
            return;
        }

        // Static actors do not synchronize transforms from PhysX in play mode.
        // Creation and instance edits still run immediately when requested.
        if (m_is_static && !m_needs_creation && !m_instances_dirty &&
            Engine::IsFlagSet(EngineMode::Playing) && m_body_type != BodyType::Controller &&
            m_body_type != BodyType::Custom && m_body_type != BodyType::Cloth)
        {
            CountWorldWork(WorldWork::physics_static_clean_skips);
            return;
        }

        // deferred creation after loading (render component needs to be available first)
        if (m_needs_creation)
        {
            // Match static collision streaming before cooking, not only after
            // allocating actors for the entire island. Prepare at the outer
            // radius so collision is ready before the 40 m activation boundary.
            if (m_is_static && m_body_type == BodyType::Mesh && m_distance_streaming && outside_collision_prepare_range(GetEntity(), true))
            {
                CountWorldWork(WorldWork::physics_creation_far_skips);
                return;
            }
            // The editor can render and select meshes without cooked collision.
            // Spread initial actor creation across frames instead of cooking the
            // entire island in the first visible frame. Play still creates every
            // required actor synchronously before the next simulation step.
            // Rendering can stop while the editor is minimized or resources are
            // preparing. The engine clock still advances once per world tick.
            static double creation_tick_ms = -1.0;
            static float creation_time_ms = 0.0f;
            const double tick_ms = Timer::GetTimeMs();
            if (creation_tick_ms != tick_ms)
            {
                creation_tick_ms = tick_ms;
                creation_time_ms = 0.0f;
            }
            if (!Engine::IsFlagSet(EngineMode::Playing) && creation_time_ms >= 2.0f)
            {
                return;
            }

            const Stopwatch creation_timer;
            m_needs_creation = false;
            CountWorldWork(WorldWork::physics_created);
            Create();
            creation_time_ms += creation_timer.GetElapsedTimeMs();
        }

        // a live drag rewrites the instance list many times a frame, the actors follow it once here
        if (m_instances_dirty)
        {
            m_instances_dirty = false;
            CountWorldWork(WorldWork::physics_instances_rebuilt);
            RebuildInstanceActors();
        }

        // sync physics transforms to entities before other components (like camera) tick
        // this ensures child entities have up-to-date parent transforms when they compute matrices
        const bool is_playing  = Engine::IsFlagSet(EngineMode::Playing);
        const float delta_time = static_cast<float>(Timer::GetDeltaTimeSec());

        if (!is_playing)
        {
            UpdateShapeGeometry();
        }

        switch (m_body_type)
        {
            case BodyType::Controller:
                CountWorldWork(WorldWork::physics_controller_updates);
                TickController(is_playing, delta_time);
                break;

            case BodyType::Custom:
                if (m_custom_body) m_custom_body->Tick(is_playing);
                break;

            case BodyType::Cloth:
                CountWorldWork(WorldWork::physics_cloth_updates);
                TickCloth(is_playing, delta_time);
                break;

            default:
                if (!m_is_static)
                {
                    CountWorldWork(WorldWork::physics_dynamic_syncs);
                    TickDynamicBodies(is_playing);
                }
                else if (!is_playing)
                {
                    CountWorldWork(WorldWork::physics_editor_static_syncs);
                    SyncStaticPoses();
                }
                break;
        }
        GetEntity()->RefreshPreTickGate();
    }

    void Physics::Tick()
    {
        // the entity is published before Create finishes appending actors, so reading m_actors here would race
        if (ProgressTracker::IsLoading())
        {
            return;
        }

        // distance-based activation/deactivation for static actors
        if (
            m_body_type != BodyType::Controller &&
            m_body_type != BodyType::Cloth &&
            m_is_static &&
            m_distance_streaming
        )
        {
            TickDistanceActivation();
        }
    }

    void Physics::SetDistanceStreaming(bool enabled)
    {
        if (m_distance_streaming == enabled)
        {
            return;
        }
        m_distance_streaming = enabled;
        m_activation_valid   = false;

        if (!enabled)
        {
            // bring back every actor the camera streamed out, and create collision that was deferred for being far away
            for (size_t i = 0; i < m_actors.size() && i < m_actors_active.size(); i++)
            {
                if (!m_actors_active[i] && m_actors[i])
                {
                    PhysicsWorld::AddActor(static_cast<PxRigidActor*>(m_actors[i]));
                    m_actors_active[i] = true;
                    m_actors_active_count++;
                }
            }
            GetEntity()->WakePhysicsPreTick();
            GetEntity()->RefreshPreTickGate();
        }
    }

    void Physics::TickController(bool is_playing, float delta_time)
    {
        if (!m_controller)
        {
            return;
        }

        PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);

        if (is_playing)
        {
            // the editor camera can end up under the terrain, lift the capsule out so it falls onto the surface instead of being trapped
            if (!m_controller_was_playing)
            {
                LiftControllerAboveTerrain();
            }
            m_controller_was_playing = true;

            // apply gravity
            m_velocity.y += PhysicsWorld::GetGravity().y * delta_time;

            // buoyancy, the fft water pushes the capsule up toward a waterline above its center, drag damps the bob
            {
                PxExtendedVec3 position = controller->getPosition();
                const Vector3 world_position = PhysicsWorld::ToWorldPosition(Vector3(static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)));
                float water_height      = 0.0f;
                if (Renderer::GetOceanHeight(world_position.x, world_position.z, water_height))
                {
                    const float waterline = 0.25f; // meters above the capsule center the water settles at
                    const float depth     = water_height - (static_cast<float>(position.y) + waterline);
                    if (depth > 0.0f)
                    {
                        const float buoyancy_gain = 30.0f; // upward acceleration per meter of submersion
                        const float water_drag    = 11.0f; // near critical damping for the gain above, settles without bobbing past the waterline
                        m_velocity.y += (depth * buoyancy_gain - m_velocity.y * water_drag) * delta_time;
                    }
                }
            }

            // move controller
            PxControllerFilters filters;
            filters.mFilterFlags = PxQueryFlag::eSTATIC | PxQueryFlag::eDYNAMIC;
            PxControllerCollisionFlags collision_flags = controller->move(PxVec3(0.0f, m_velocity.y * delta_time, 0.0f), 0.001f, delta_time, filters);

            // reset vertical velocity on ground collision
            if (collision_flags & PxControllerCollisionFlag::eCOLLISION_DOWN)
            {
                m_velocity.y = 0.0f;
            }

            // sync physx -> entity position and compute xz velocity
            PxExtendedVec3 pos_ext   = controller->getPosition();
            Vector3 pos_previous     = GetEntity()->GetPosition();
            Vector3 pos              = PhysicsWorld::ToWorldPosition(Vector3(static_cast<float>(pos_ext.x), static_cast<float>(pos_ext.y), static_cast<float>(pos_ext.z)));
            GetEntity()->SetPosition(pos);

            if (delta_time > 0.0f)
            {
                m_velocity.x = (pos.x - pos_previous.x) / delta_time;
                m_velocity.z = (pos.z - pos_previous.z) / delta_time;
            }
        }
        else
        {
            // editor mode: sync entity -> physx
            Vector3 entity_pos = PhysicsWorld::ToPhysicsPosition(GetEntity()->GetPosition());
            controller->setPosition(PxExtendedVec3(entity_pos.x, entity_pos.y, entity_pos.z));
            m_velocity               = Vector3::Zero;
            m_controller_was_playing = false;
        }
    }

    void Physics::LiftControllerAboveTerrain()
    {
        Terrain* terrain = Terrain::FindActive();
        if (!m_controller || !terrain)
        {
            return;
        }

        PxCapsuleController* controller = static_cast<PxCapsuleController*>(m_controller);
        PxExtendedVec3 position         = controller->getPosition();
        const Vector3 world_position = PhysicsWorld::ToWorldPosition(Vector3(static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)));
        float terrain_height            = 0.0f;
        if (!terrain->SampleHeight(world_position.x, world_position.z, terrain_height))
        {
            return;
        }

        // the controller position is the capsule center, its feet sit half the height plus one radius below it
        const float feet_offset = controller->getHeight() * 0.5f + controller->getRadius();
        const float minimum_y   = terrain_height + feet_offset;
        if (static_cast<float>(position.y) >= minimum_y)
        {
            return;
        }

        // a small lift keeps the capsule from resolving against the surface it just left
        const float clearance = 0.1f;
        const Vector3 lifted  = Vector3(static_cast<float>(position.x), minimum_y + clearance, static_cast<float>(position.z));
        controller->setPosition(PxExtendedVec3(lifted.x, lifted.y, lifted.z));
        GetEntity()->SetPosition(PhysicsWorld::ToWorldPosition(lifted));
        m_velocity = Vector3::Zero;
    }

    void Physics::TickDynamicBodies(bool is_playing)
    {
        Render* render = GetEntity()->GetComponent<Render>();
        if (!render)
        {
            return;
        }

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_actors.size()); i++)
        {
            if (!m_actors[i])
            {
                continue;
            }

            PxRigidActor* actor     = static_cast<PxRigidActor*>(m_actors[i]);
            PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>();

            // get transform (from instance or entity)
            auto get_transform = [&]() -> math::Matrix
            {
                if (render->HasInstancing() && i < render->GetInstanceCount())
                {
                    return render->GetInstance(i, true);
                }
                if (i == 0)
                {
                    return GetEntity()->GetMatrix();
                }
                return math::Matrix(); // invalid - caller should skip
            };

            if (is_playing)
            {
                if (m_is_kinematic && dynamic)
                {
                    // sync entity -> physx (kinematic target)
                    math::Matrix transform = get_transform();
                    if (transform == math::Matrix())
                    {
                        continue;
                    }
                    dynamic->setKinematicTarget(to_px_transform(transform));
                }
                else
                {
                    // sync physx -> entity (simulated dynamic)
                    if (i == 0)
                    {
                        Vector3 pos;
                        Quaternion rot;
                        from_px_transform(actor->getGlobalPose(), pos, rot);
                        GetEntity()->SetPositionAndRotation(pos, rot);
                    }
                }
            }
            else
            {
                // editor mode: sync entity -> physx
                math::Matrix transform = get_transform();
                if (transform == math::Matrix())
                {
                    continue;
                }

                actor->setGlobalPose(to_px_transform(transform));

                // reset velocities for non-kinematics
                if (dynamic && !m_is_kinematic)
                {
                    dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                    dynamic->setAngularVelocity(PxVec3(0, 0, 0));
                }
            }
        }
    }

    void Physics::TickBuoyancy()
    {
        // called from the fixed step loop which already holds the physx mutex
        for (Physics* body : buoyancy::floating_bodies)
        {
            body->ApplyBuoyancy();
        }
    }

    void Physics::ShiftOrigin(const Vector3& shift)
    {
        // The scene has shifted its actors and joint caches. CCT state, removed
        // actors awaiting streaming activation, and vehicle query caches belong
        // to the engine and must follow once, without moving any entity.
        const PxVec3 delta(shift.x, shift.y, shift.z);
        if (controller_manager) controller_manager->shiftOrigin(delta);
        for (Physics* component : buoyancy::bodies)
        {
            for (void* entry : component->m_actors)
            {
                auto* actor = static_cast<PxRigidActor*>(entry);
                if (actor && !actor->getScene())
                {
                    PxTransform pose = actor->getGlobalPose();
                    pose.p -= delta;
                    actor->setGlobalPose(pose);
                }
            }
            if (component->m_custom_body) component->m_custom_body->ShiftOrigin(shift);
        }
    }

    void Physics::ApplyBuoyancy()
    {
        if (!m_enabled || m_is_static || m_is_kinematic)
        {
            return;
        }

        // the controller floats in TickController, vehicles and cloth have their own force models
        if (m_body_type == BodyType::Controller || m_body_type == BodyType::Custom || m_body_type == BodyType::Cloth || m_body_type == BodyType::Plane)
        {
            return;
        }

        const float volume = ComputeVolume();
        if (volume <= 0.0f)
        {
            return;
        }

        const float gravity = -PhysicsWorld::GetGravity().y;
        for (void* entry : m_actors)
        {
            PxRigidActor* actor     = static_cast<PxRigidActor*>(entry);
            PxRigidDynamic* dynamic = actor ? actor->is<PxRigidDynamic>() : nullptr;
            if (!dynamic)
            {
                continue;
            }

            const PxBounds3 bounds  = actor->getWorldBounds();
            const PxVec3 center     = bounds.getCenter();
            const PxVec3 extents    = bounds.getExtents();
            const float body_height = max(bounds.getDimensions().y, 0.01f);

            // four sample points around the footprint, each carries a quarter of the displaced volume,
            // applied off center so a wave slope produces torque and the body pitches and rolls with the swell
            const float span_x = max(extents.x * 0.7f, 0.25f);
            const float span_z = max(extents.z * 0.7f, 0.25f);
            const PxVec3 offsets[4] =
            {
                PxVec3( span_x, 0.0f, 0.0f),
                PxVec3(-span_x, 0.0f, 0.0f),
                PxVec3(0.0f, 0.0f,  span_z),
                PxVec3(0.0f, 0.0f, -span_z)
            };

            // archimedes capped so light bodies stay stable, split across the sample points
            const float force_per_point = min(buoyancy::water_density * gravity * volume, dynamic->getMass() * buoyancy::max_accel) * 0.25f;

            float heights[4] = {};
            bool heights_valid = true;
            for (uint32_t i = 0; i < 4; i++)
            {
                const PxVec3 point = center + offsets[i];
                const Vector3 world_point = PhysicsWorld::ToWorldPosition(from_px_vec3(point));
                if (!Renderer::GetOceanHeight(world_point.x, world_point.z, heights[i]))
                {
                    heights_valid = false;
                    break;
                }
            }

            if (!heights_valid)
            {
                continue;
            }

            float submersion = 0.0f;
            for (uint32_t i = 0; i < 4; i++)
            {
                const PxVec3 point = center + offsets[i];
                const float depth = heights[i] - bounds.minimum.y;
                if (depth <= 0.0f)
                {
                    continue;
                }

                const float point_submersion = min(depth / body_height, 1.0f);
                submersion                  += point_submersion * 0.25f;
                PxRigidBodyExt::addForceAtPos(*dynamic, PxVec3(0.0f, force_per_point * point_submersion, 0.0f), point, PxForceMode::eFORCE);
            }

            if (submersion <= 0.0f)
            {
                continue;
            }

            // the per point force differential is too weak to visibly rotate a small body, so also steer the
            // body up axis toward the wave normal built from the same samples, this is what makes floats wobble
            PxVec3 water_normal = PxVec3((heights[1] - heights[0]) / (2.0f * span_x), 1.0f, (heights[3] - heights[2]) / (2.0f * span_z));
            water_normal        = water_normal.getNormalized();
            const PxVec3 up     = dynamic->getGlobalPose().q.getBasisVector1();
            dynamic->addTorque(up.cross(water_normal) * buoyancy::align_gain * submersion, PxForceMode::eACCELERATION);

            // water resistance, heave is damped near critically so floats ride the surface instead of oscillating past it
            const PxVec3 velocity = dynamic->getLinearVelocity();
            const PxVec3 drag     = PxVec3(velocity.x * buoyancy::drag_horizontal, velocity.y * buoyancy::drag_vertical, velocity.z * buoyancy::drag_horizontal);
            dynamic->addForce(drag * -submersion, PxForceMode::eACCELERATION);
            dynamic->addTorque(dynamic->getAngularVelocity() * -buoyancy::drag_angular * submersion, PxForceMode::eACCELERATION);
        }
    }

    // editor only, keeps a static body under a hand moved entity
    //
    // the entity matrix is hoisted and tested first, rewriting a pose rewrites the static pruner entry
    // behind it, and a scattered prop owns one actor per copy, so doing that unconditionally every frame
    // costs more than the rest of the scene put together
    void Physics::SyncStaticPoses()
    {
        const Matrix matrix = GetEntity()->GetMatrix();
        if (matrix == m_transform_previous)
        {
            return;
        }
        m_transform_previous = matrix;

        Render* render      = GetEntity()->GetComponent<Render>();
        const bool instanced = render && render->HasInstancing();

        // the instance list can shrink before the actors are rebuilt, never index past it
        uint32_t count = static_cast<uint32_t>(m_actors.size());
        if (instanced)
        {
            count = min(count, render->GetInstanceCount());
        }

        for (uint32_t i = 0; i < count; i++)
        {
            PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[i]);
            if (!actor)
            {
                continue;
            }

            // an instanced render owns one actor per copy, writing the entity matrix into all of them
            // stacks every hull on the entity origin instead of leaving them where they were scattered
            actor->setGlobalPose(to_px_transform(instanced ? render->GetInstance(i, true) : matrix));
        }
    }

    void Physics::TickDistanceActivation()
    {
        CountWorldWork(WorldWork::physics_activation_calls);
        if (m_actors.empty()) { CountWorldWork(WorldWork::physics_activation_empty_skips); return; }
        Camera* camera = World::GetCamera();
        Render* render = GetEntity()->GetComponent<Render>();
        if (!camera || !render)
        {
            return;
        }

        const Vector3 camera_pos = camera->GetEntity()->GetPosition();

        // ensure tracking vector matches actor count
        if (m_actors_active.size() != m_actors.size())
        {
            m_actors_active.resize(m_actors.size(), true);
            m_actors_active_count = static_cast<uint32_t>(m_actors.size());
            m_activation_valid = false;
        }

        // a scattered prop entity owns one actor per instance, and a populated world has tens of
        // thousands of them, so walking every instance every frame is the single largest cost in the
        // editor. they all sit inside one world box, and once the set is fully asleep a box that cannot
        // reach the activation radius rejects the whole entity in constant time
        const float box_distance_squared = Vector3::DistanceSquared(
            camera_pos,
            render->GetBoundingBox().GetClosestPoint(camera_pos)
        );

        if (m_actors_active_count == 0 && box_distance_squared > distance_activate_squared)
        {
            CountWorldWork(WorldWork::physics_activation_box_skips);
            return;
        }

        // the instance list can shrink before the actors are rebuilt, never index past it
        const bool instanced = render->HasInstancing();
        uint32_t count       = static_cast<uint32_t>(m_actors.size());
        if (instanced)
        {
            count = min(count, render->GetInstanceCount());
        }

        const Matrix& world = GetEntity()->GetMatrix();
        // Distance to a fixed point (or box) changes by at most the camera's
        // displacement. Until that displacement reaches the closest hysteresis
        // boundary, no actor can change state. Edits invalidate the proof.
        if (instanced && m_activation_valid && world == m_activation_world &&
            (camera_pos == m_activation_camera ||
             Vector3::DistanceSquared(camera_pos, m_activation_camera) < m_activation_slack * m_activation_slack))
        {
            CountWorldWork(WorldWork::physics_activation_cached_skips);
            return;
        }
        float activation_slack = distance_activate;
        for (uint32_t i = 0; i < count; i++)
        {
            PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[i]);
            if (!actor)
            {
                continue;
            }

            CountWorldWork(WorldWork::physics_actors_tested);
            // compute distance to actor
            Vector3 closest_point = instanced
                ? render->GetInstancePosition(i, world)
                : render->GetBoundingBox().GetClosestPoint(camera_pos);
            const float distance_squared = Vector3::DistanceSquared(camera_pos, closest_point);

            // use hysteresis to prevent flickering at boundary
            const bool is_active = m_actors_active[i];
            if (is_active && distance_squared > distance_deactivate_squared)
            {
                CountWorldWork(WorldWork::physics_actors_deactivated);
                PhysicsWorld::RemoveActor(actor);
                m_actors_active[i] = false;
                m_actors_active_count--;
            }
            else if (!is_active && distance_squared <= distance_activate_squared)
            {
                CountWorldWork(WorldWork::physics_actors_activated);
                PhysicsWorld::AddActor(actor);
                m_actors_active[i] = true;
                m_actors_active_count++;
            }
            const float threshold = m_actors_active[i] ? distance_deactivate : distance_activate;
            activation_slack = min(activation_slack, fabsf(sqrtf(distance_squared) - threshold));
        }
        m_activation_camera = camera_pos;
        m_activation_world = world;
        // Round conservatively at the boundary; a false miss only costs a rescan.
        m_activation_slack = max(0.0f, activation_slack - 0.001f);
        m_activation_valid = true;
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
        node.append_attribute("use_convex_hull") = m_use_convex_hull;
        node.append_attribute("distance_streaming") = m_distance_streaming;
        node.append_attribute("body_type")        = static_cast<int>(m_body_type);

        // cloth parameters
        node.append_attribute("cloth_stiffness")      = m_cloth_stiffness;
        node.append_attribute("cloth_damping")         = m_cloth_damping;
        node.append_attribute("cloth_iterations")      = m_cloth_iterations;
        node.append_attribute("cloth_wind_enabled")    = m_cloth_wind_enabled;
        node.append_attribute("cloth_pin_direction_x") = m_cloth_pin_direction.x;
        node.append_attribute("cloth_pin_direction_y") = m_cloth_pin_direction.y;
        node.append_attribute("cloth_pin_direction_z") = m_cloth_pin_direction.z;
    }

    void Physics::Load(pugi::xml_node& node)
    {
        PhysicsSettings settings;
        settings.mass             = node.attribute("mass").as_float(0.001f);
        settings.friction         = node.attribute("friction").as_float(1.0f);
        settings.friction_rolling = node.attribute("friction_rolling").as_float(0.002f);
        settings.restitution      = node.attribute("restitution").as_float(0.2f);
        settings.is_static        = node.attribute("is_static").as_bool(true);
        settings.is_kinematic     = node.attribute("is_kinematic").as_bool(false);
        settings.position_lock.x  = node.attribute("position_lock_x").as_float(0.0f);
        settings.position_lock.y  = node.attribute("position_lock_y").as_float(0.0f);
        settings.position_lock.z  = node.attribute("position_lock_z").as_float(0.0f);
        settings.rotation_lock.x  = node.attribute("rotation_lock_x").as_float(0.0f);
        settings.rotation_lock.y  = node.attribute("rotation_lock_y").as_float(0.0f);
        settings.rotation_lock.z  = node.attribute("rotation_lock_z").as_float(0.0f);
        settings.center_of_mass.x = node.attribute("center_of_mass_x").as_float(0.0f);
        settings.center_of_mass.y = node.attribute("center_of_mass_y").as_float(0.0f);
        settings.center_of_mass.z = node.attribute("center_of_mass_z").as_float(0.0f);
        settings.use_convex_hull = node.attribute("use_convex_hull").as_bool(settings.use_convex_hull);
        settings.distance_streaming = node.attribute("distance_streaming").as_bool(settings.distance_streaming);
        settings.body_type        = static_cast<BodyType>(node.attribute("body_type").as_int(static_cast<int>(BodyType::Max)));

        // cloth parameters
        settings.cloth_stiffness    = node.attribute("cloth_stiffness").as_float(0.9f);
        settings.cloth_damping      = node.attribute("cloth_damping").as_float(0.01f);
        settings.cloth_iterations   = node.attribute("cloth_iterations").as_uint(8);
        settings.cloth_wind_enabled = node.attribute("cloth_wind_enabled").as_bool(true);
        settings.cloth_pin_direction.x = node.attribute("cloth_pin_direction_x").as_float(0.0f);
        settings.cloth_pin_direction.y = node.attribute("cloth_pin_direction_y").as_float(1.0f);
        settings.cloth_pin_direction.z = node.attribute("cloth_pin_direction_z").as_float(0.0f);

        ApplySettings(settings);
    }

    sol::reference Physics::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void Physics::SetMass(float mass)
    {
        // approximate mass from volume
        if (mass == mass_from_volume)
        {
            if (m_body_type == BodyType::Max)
            {
                SP_LOG_WARNING("This call will be ignored. You need to set the body type before setting mass from volume.");
                return;
            }

            if (m_body_type == BodyType::Plane)
            {
                mass = 1.0f; // infinite plane, use default mass
            }
            else if (m_body_type == BodyType::Controller)
            {
                mass = 70.0f; // approximate human mass
            }
            else
            {
                constexpr float density = 1000.0f; // kg per cubic meter, water
                float volume            = ComputeVolume();
                if (volume > 0.0f)
                {
                    mass = volume * density;
                }
            }
        }

        // ensure safe physx mass range
        PhysicsSettings settings = GetSettings();
        settings.mass = mass;
        settings.Validate();
        m_mass = settings.mass;

        if (m_body_type == BodyType::Cloth)
        {
            float inverse_mass = 1.0f / m_mass;
            for (ClothParticle& particle : m_cloth_particles)
            {
                if (particle.inverse_mass > 0.0f)
                {
                    particle.inverse_mass = inverse_mass;
                }
            }
        }

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
        PhysicsSettings settings = GetSettings();
        settings.friction = friction;
        settings.Validate();
        friction = settings.friction;
        if (m_friction == friction)
        {
            return;
        }

        m_friction = friction;
        if (m_material)
        {
            static_cast<PxMaterial*>(m_material)->setStaticFriction(m_friction);
        }
    }

    void Physics::SetFrictionRolling(float friction_rolling)
    {
        PhysicsSettings settings = GetSettings();
        settings.friction_rolling = friction_rolling;
        settings.Validate();
        friction_rolling = settings.friction_rolling;
        if (m_friction_rolling == friction_rolling)
        {
            return;
        }

        m_friction_rolling = friction_rolling;
        if (m_material)
        {
            static_cast<PxMaterial*>(m_material)->setDynamicFriction(m_friction_rolling);
        }
    }

    void Physics::SetRestitution(float restitution)
    {
        PhysicsSettings settings = GetSettings();
        settings.restitution = restitution;
        settings.Validate();
        restitution = settings.restitution;
        if (m_restitution == restitution)
        {
            return;
        }

        m_restitution = restitution;
        if (m_material)
        {
            static_cast<PxMaterial*>(m_material)->setRestitution(m_restitution);
        }
    }

    void Physics::SetLinearVelocity(const Vector3& velocity) const
    {
        if (m_body_type == BodyType::Controller)
        {
            return;
        }

        for (auto* body : m_actors)
        {
            if (!body)
            {
                continue;
            }

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
        {
            return Vector3::Zero;
        }

        if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(m_actors[0])->is<PxRigidDynamic>())
        {
            PxVec3 velocity = dynamic->getLinearVelocity();
            return Vector3(velocity.x, velocity.y, velocity.z);
        }

        return Vector3::Zero;
    }

    Vector3 Physics::GetAngularVelocity() const
    {
        if (m_body_type == BodyType::Controller || m_actors.empty() || !m_actors[0])
        {
            return Vector3::Zero;
        }

        if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(m_actors[0])->is<PxRigidDynamic>())
        {
            PxVec3 velocity = dynamic->getAngularVelocity();
            return Vector3(velocity.x, velocity.y, velocity.z);
        }

        return Vector3::Zero;
    }

    void Physics::SetAngularVelocity(const Vector3& velocity) const
    {
        if (m_body_type == BodyType::Controller)
        {
            return;
        }

        for (auto* body : m_actors)
        {
            if (!body)
            {
                continue;
            }

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
            if (!body)
            {
                continue;
            }

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
        {
            return;
        }

        m_position_lock = lock;
        for (auto* body : m_actors)
        {
            if (!body) continue;
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
        {
            return;
        }

        m_rotation_lock = lock;
        for (auto* body : m_actors)
        {
            if (!body) continue;
            if (PxRigidDynamic* dynamic = static_cast<PxRigidActor*>(body)->is<PxRigidDynamic>())
            {
                dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
            }
        }
    }

    void Physics::SetCenterOfMass(const Vector3& center_of_mass)
    {
        if (m_body_type == BodyType::Controller)
        {
            return;
        }

        m_center_of_mass = center_of_mass;
        for (auto* body : m_actors)
        {
            if (!body)
            {
                continue;
            }

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
        {
            return;
        }

        m_body_type = type;

        // Procedural roads and sidewalks set their type during the main-thread
        // world commit. Let PreTick prepare their collision near the camera,
        // instead of blocking the loading frame on every distant road.
        if (m_is_static && m_body_type == BodyType::Mesh &&
            (ProgressTracker::IsLoading() || (m_distance_streaming && outside_collision_prepare_range(GetEntity()))))
        {
            Remove();
            m_needs_creation = true;
            GetEntity()->RefreshPreTickGate();
            return;
        }
        Create();
    }

    void Physics::SetUseConvexHull(bool enabled)
    {
        if (m_use_convex_hull == enabled)
        {
            return;
        }

        m_use_convex_hull = enabled;

        // the cooked shape is baked at create time, so the switch only takes effect on a rebuild
        if (m_body_type == BodyType::Mesh)
        {
            Create();
        }
    }

    void Physics::SetClothPinDirection(const Vector3& direction)
    {
        Vector3 normalized_direction = direction.LengthSquared() > 0.0001f ? direction.Normalized() : Vector3::Up;
        if (m_cloth_pin_direction == normalized_direction)
        {
            return;
        }

        m_cloth_pin_direction = normalized_direction;
        if (m_body_type == BodyType::Cloth)
        {
            Create();
        }
    }

    bool Physics::IsGrounded() const
    {
        // only controller bodies support ground queries, avoid spamming a warning for other body types
        if (m_body_type != BodyType::Controller)
        {
            return false;
        }

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
        {
            return nullptr;
        }

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
        {
            return nullptr;
        }

        // get the actor used by the controller to avoid returning itself
        PxRigidActor* actor_to_ignore = controller->getActor();

        lock_guard<recursive_mutex> physx_lock(PhysicsWorld::GetMutex());
        if (scene->raycast(ray_start, ray_dir, ray_length, hit, PxHitFlag::eDEFAULT, filter_data))
        {
            for (PxU32 i = 0; i < hit.nbTouches; ++i)
            {
                const PxRaycastHit& current_hit = hit.getTouch(i);

                if (!current_hit.actor || current_hit.actor == actor_to_ignore)
                {
                    continue;
                }

                if (current_hit.actor->userData)
                {
                    return static_cast<Entity*>(current_hit.actor->userData);
                }
            }
        }

        return nullptr;
    }

    float Physics::ComputeVolume()
    {
        Vector3 scale = GetEntity()->GetScale();

        switch (m_body_type)
        {
            case BodyType::Box:
            {
                return scale.x * scale.y * scale.z;
            }
            case BodyType::Sphere:
            {
                // 4/3 pi r cubed, radius is the largest axis halved
                float radius = max(max(scale.x, scale.y), scale.z) * 0.5f;
                return (4.0f / 3.0f) * math::pi * radius * radius * radius;
            }
            case BodyType::Capsule:
            {
                return GetCapsuleVolume();
            }
            case BodyType::Mesh:
            case BodyType::MeshConvex:
            case BodyType::Cloth:
            {
                // approximate with the bounding box, extents are half size
                if (Render* render = GetEntity()->GetComponent<Render>())
                {
                    Vector3 extents = render->GetBoundingBox().GetExtents();
                    return extents.x * extents.y * extents.z * 8.0f;
                }
                return m_body_type == BodyType::Cloth ? 0.01f : 1.0f;
            }
            default:
            {
                return 0.0f;
            }
        }
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

        // eye level for an average adult sits about 0.15m below the top of the head, returned relative to the capsule center
        const float eye_offset_from_top = 0.13f;
        return Vector3(0.0f, (height * 0.5f) + radius - eye_offset_from_top, 0.0f);
    }

    void Physics::SetStatic(bool is_static)
    {
        // return if state hasn't changed
        if (m_is_static == is_static)
        {
            return;
        }

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
        {
            return;
        }

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
            {
                return;
            }

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
        {
            return;
        }

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
        GetEntity()->SetPosition(PhysicsWorld::ToWorldPosition(Vector3(static_cast<float>(pos.x), static_cast<float>(pos.y), static_cast<float>(pos.z))));
    }

    void Physics::SetBodyTransform(const Vector3& position, const Quaternion& rotation, bool reset_simulation)
    {
        // reset interpolation state to avoid lerping from old position to new teleport position
        m_interpolation_initialized = false;
        m_prev_position             = position;
        m_prev_rotation             = rotation;
        m_current_position          = position;
        m_current_rotation          = rotation;

        // for character controllers, use setPosition to teleport
        if (m_body_type == BodyType::Controller && m_controller)
        {
            PxController* controller = static_cast<PxController*>(m_controller);
            const Vector3 local = PhysicsWorld::ToPhysicsPosition(position);
            controller->setPosition(PxExtendedVec3(local.x, local.y, local.z));
            m_velocity = Vector3::Zero; // reset movement velocity
            return;
        }

        if (m_body_type == BodyType::Custom && m_custom_body &&
            m_custom_body->SetTransform(position, rotation, reset_simulation)) return;

        // for regular rigid bodies
        if (!m_actors.empty() && m_actors[0])
        {
            PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[0]);
            PxTransform pose = to_px_transform(position, rotation);
            actor->setGlobalPose(pose);

            if (PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>())
            {
                dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                dynamic->setAngularVelocity(PxVec3(0, 0, 0));
            }
        }
    }

    void Physics::SetMeshConvexSourceEntity(Entity* entity)
    {
        m_mesh_convex_source = entity;

        // if body type is already MeshConvex, recreate the physics shapes
        if (m_body_type == BodyType::MeshConvex)
        {
            Create();
        }
    }

    void Physics::Create()
    {
        struct ParticipationRefresh
        {
            Entity* entity;
            ~ParticipationRefresh() { entity->RefreshPreTickGate(); }
        } refresh{GetEntity()};
        // serializes the whole physx setup, the prefab path runs on loader workers and physx corrupts the scene on concurrent writes
        lock_guard<recursive_mutex> physx_lock(PhysicsWorld::GetMutex());

        // clear previous state
        Remove();

        // auto-detect body type if not explicitly set
        if (m_body_type == BodyType::Max)
        {
            m_body_type = DetectBodyType();
        }

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());

        // material - shared across all shapes (if multiple shapes are used)
        m_material = physics->createMaterial(m_friction, m_friction_rolling, m_restitution);

        // body/controller
        if (m_body_type == BodyType::Controller)
        {
            if (!CreateController())
            {
                return;
            }
        }
        else if (m_body_type == BodyType::Custom)
        {
            void* actor = EnsureCustomBody().Create();
            if (!actor) return;
            m_has_custom_actor = true;
            m_actors.assign(1, actor);
            m_actors_active.assign(1, true);
            m_actors_active_count = 1;
        }
        else if (m_body_type == BodyType::Cloth)
        {
            CreateCloth();
            return;
        }
        else if (m_body_type == BodyType::Heightfield)
        {
            // a terrain grid is never anything but immovable ground
            m_is_static    = true;
            m_is_kinematic = false;

            CreateHeightfield();
            if (!m_mesh)
            {
                return;
            }

            CreateBodies();
            m_scale_previous = GetEntity()->GetScale();
            return;
        }
        else if (m_body_type == BodyType::MeshConvex)
        {
            if (!CreateConvexCompound())
            {
                return;
            }
        }
        else
        {
            if (!CreateShapes())
            {
                return;
            }
        }

        // Keep all components for origin shifts, but only visit eligible bodies
        // when applying buoyancy at the 200 Hz simulation frequency.
        if (!m_is_static && !m_is_kinematic && !m_actors.empty() &&
            m_body_type != BodyType::Controller && m_body_type != BodyType::Custom &&
            m_body_type != BodyType::Cloth && m_body_type != BodyType::Plane)
        {
            buoyancy::floating_bodies.push_back(this);
        }
    }

    // capsule character controller, false when creation failed and Create should stop
    bool Physics::CreateController()
    {
        PxScene* scene     = static_cast<PxScene*>(PhysicsWorld::GetScene());

        if (!controller_manager)
        {
            controller_manager = PxCreateControllerManager(*scene);
            if (!controller_manager)
            {
                SP_LOG_ERROR("Failed to create controller manager");
                return false;
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
        const Vector3 pos = PhysicsWorld::ToPhysicsPosition(GetEntity()->GetPosition());
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
            return false;
        }

        // note: the controller internally references the material, so don't release m_material here
        // it will be released in Remove() when the controller is destroyed

        // tag the cct's internal actor so the simulation filter shader can
        // suppress contacts between the character controller and the vehicle
        PxRigidActor* cct_actor = static_cast<PxController*>(m_controller)->getActor();
        if (cct_actor)
        {
            tag_actor_shapes(cct_actor, 1);
        }

        return true;
    }

    // vehicle chassis driven by the car simulation, false when creation failed and Create should stop

    // one actor with a convex hull per mesh in the source hierarchy, false when creation failed and Create should stop
    bool Physics::CreateConvexCompound()
    {
        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());

        // compound shape built from convex hulls of entity hierarchy meshes
        // this walks all descendants of a source entity and creates a convex hull for each mesh

        Entity* source_entity = m_mesh_convex_source ? m_mesh_convex_source : GetEntity();
        if (!source_entity)
        {
            SP_LOG_ERROR("No source entity for MeshConvex body type");
            return false;
        }

        // collect all entities with render components in the hierarchy
        vector<Entity*> mesh_entities;
        mesh_entities.push_back(source_entity);
        source_entity->GetDescendants(&mesh_entities);

        // filter to only entities with render components
        vector<pair<Entity*, Render*>> render_entities;
        for (Entity* entity : mesh_entities)
        {
            if (Render* render = entity->GetComponent<Render>())
            {
                render_entities.push_back({entity, render});
            }
        }

        if (render_entities.empty())
        {
            SP_LOG_ERROR("No render entities found in hierarchy for MeshConvex");
            return false;
        }

        // create the rigid body at the physics entity's transform
        Vector3 body_pos = GetEntity()->GetPosition();
        Quaternion body_rot = GetEntity()->GetRotation();
        PxTransform body_pose = to_px_transform(body_pos, body_rot);

        PxRigidActor* actor = nullptr;
        if (IsStatic())
        {
            actor = physics->createRigidStatic(body_pose);
        }
        else
        {
            actor = physics->createRigidDynamic(body_pose);
            PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>();
            if (dynamic)
            {
                dynamic->setMass(m_mass);
                dynamic->setRigidBodyFlag(PxRigidBodyFlag::eENABLE_CCD, !m_is_kinematic);
                dynamic->setRigidBodyFlag(PxRigidBodyFlag::eKINEMATIC, m_is_kinematic);
                dynamic->setRigidDynamicLockFlags(build_lock_flags(m_position_lock, m_rotation_lock));
            }
        }

        if (!actor)
        {
            SP_LOG_ERROR("Failed to create rigid actor for MeshConvex");
            return false;
        }

        // cooking parameters for convex hull generation
        PxTolerancesScale px_scale;
        px_scale.length = 1.0f;
        Vector3 gravity = PhysicsWorld::GetGravity();
        px_scale.speed = sqrtf(gravity.x * gravity.x + gravity.y * gravity.y + gravity.z * gravity.z);
        PxCookingParams params(px_scale);
        params.convexMeshCookingType = PxConvexMeshCookingType::eQUICKHULL;
        params.meshPreprocessParams |= PxMeshPreprocessingFlag::eWELD_VERTICES;
        params.meshWeldTolerance = 0.00001f;
        params.gaussMapLimit = 32;

        PxInsertionCallback* insertion_callback = PxGetStandaloneInsertionCallback();
        PxMaterial* material = static_cast<PxMaterial*>(m_material);
        if (!insertion_callback || !material)
        {
            SP_LOG_ERROR("MeshConvex requires valid PhysX cooking and material state");
            actor->release();
            return false;
        }

        // inverse transform to convert world positions to body-local space
        Quaternion body_rot_inv = body_rot.Conjugate();

        int shapes_created = 0;
        for (const auto& render_entity : render_entities)
        {
            Entity* entity = render_entity.first;
            Render* render = render_entity.second;
            // get geometry
            vector<uint32_t> indices;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            render->GetGeometry(&indices, &vertices);
            if (vertices.empty())
            {
                continue;
            }

            // simplify geometry for physics (use moderate detail for convex hulls)
            const size_t max_convex_verts = 256; // physx limit
            if (vertices.size() > max_convex_verts)
            {
                const size_t target_index_count = min<size_t>(indices.size(), max_convex_verts * 3);
                geometry_processing::simplify(indices, vertices, target_index_count, false, false);
            }

            // compute the local transform of this entity relative to the physics body
            Vector3 entity_world_pos = entity->GetPosition();
            Quaternion entity_world_rot = entity->GetRotation();
            Vector3 entity_scale = entity->GetScale();

            // transform entity position to body-local space
            Vector3 local_pos = body_rot_inv * (entity_world_pos - body_pos);
            Quaternion local_rot = body_rot_inv * entity_world_rot;

            // convert vertices to physx format in entity-local space (with scale)
            vector<PxVec3> px_vertices;
            px_vertices.reserve(vertices.size());
            for (const auto& vertex : vertices)
            {
                px_vertices.emplace_back(
                    vertex.pos[0] * entity_scale.x,
                    vertex.pos[1] * entity_scale.y,
                    vertex.pos[2] * entity_scale.z
                );
            }
            PxVec3 minimum(PX_MAX_F32);
            PxVec3 maximum(-PX_MAX_F32);
            bool finite = true;
            for (const PxVec3& vertex : px_vertices)
            {
                if (!vertex.isFinite())
                {
                    finite = false;
                    break;
                }
                minimum.x = min(minimum.x, vertex.x);
                minimum.y = min(minimum.y, vertex.y);
                minimum.z = min(minimum.z, vertex.z);
                maximum.x = max(maximum.x, vertex.x);
                maximum.y = max(maximum.y, vertex.y);
                maximum.z = max(maximum.z, vertex.z);
            }

            auto attach_box = [&](PxVec3 box_min, PxVec3 box_max) -> bool
            {
                // thin or failed hulls still collide as a box
                if (box_min.x > box_max.x)
                {
                    const BoundingBox& mesh_aabb = render->GetBoundingBoxMesh();
                    if (mesh_aabb.IsInfinite())
                    {
                        return false;
                    }

                    const Vector3& mn = mesh_aabb.GetMin();
                    const Vector3& mx = mesh_aabb.GetMax();
                    const float x0 = mn.x * entity_scale.x;
                    const float x1 = mx.x * entity_scale.x;
                    const float y0 = mn.y * entity_scale.y;
                    const float y1 = mx.y * entity_scale.y;
                    const float z0 = mn.z * entity_scale.z;
                    const float z1 = mx.z * entity_scale.z;
                    box_min = PxVec3(min(x0, x1), min(y0, y1), min(z0, z1));
                    box_max = PxVec3(max(x0, x1), max(y0, y1), max(z0, z1));
                }

                const PxVec3 extent = box_max - box_min;
                const float min_half = 0.005f;
                const PxVec3 half(
                    max(extent.x * 0.5f, min_half),
                    max(extent.y * 0.5f, min_half),
                    max(extent.z * 0.5f, min_half)
                );
                const PxVec3 center(
                    (box_min.x + box_max.x) * 0.5f,
                    (box_min.y + box_max.y) * 0.5f,
                    (box_min.z + box_max.z) * 0.5f
                );
                const Vector3 offset = local_rot * Vector3(center.x, center.y, center.z);
                const Vector3 box_pos = local_pos + offset;
                PxBoxGeometry geometry(half.x, half.y, half.z);
                PxShape* box_shape = physics->createShape(geometry, *material);
                if (!box_shape)
                {
                    return false;
                }

                PxTransform local_pose(
                    PxVec3(box_pos.x, box_pos.y, box_pos.z),
                    PxQuat(local_rot.x, local_rot.y, local_rot.z, local_rot.w)
                );
                box_shape->setLocalPose(local_pose);
                box_shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                actor->attachShape(*box_shape);
                box_shape->release();
                return true;
            };

            const PxVec3 extent = maximum - minimum;
            const bool degenerate =
                !finite ||
                px_vertices.size() < 4 ||
                extent.x <= 0.000001f ||
                extent.y <= 0.000001f ||
                extent.z <= 0.000001f;
            if (degenerate)
            {
                if (!finite)
                {
                    minimum = PxVec3(1.0f);
                    maximum = PxVec3(-1.0f);
                }
                if (attach_box(minimum, maximum))
                {
                    shapes_created++;
                }
                continue;
            }

            // create convex mesh
            PxConvexMeshDesc mesh_desc;
            mesh_desc.points.count = static_cast<PxU32>(px_vertices.size());
            mesh_desc.points.stride = sizeof(PxVec3);
            mesh_desc.points.data = px_vertices.data();
            mesh_desc.flags =
                PxConvexFlag::eCOMPUTE_CONVEX |
                PxConvexFlag::eSHIFT_VERTICES;
            mesh_desc.vertexLimit = 64;

            PxConvexMeshCookingResult::Enum condition;
            PxConvexMesh* convex_mesh = PxCreateConvexMesh(params, mesh_desc, *insertion_callback, &condition);
            if (!convex_mesh || condition != PxConvexMeshCookingResult::eSUCCESS)
            {
                if (convex_mesh)
                {
                    convex_mesh->release();
                }
                if (attach_box(minimum, maximum))
                {
                    shapes_created++;
                }
                continue;
            }

            // create shape with local pose relative to body
            PxConvexMeshGeometry geometry(convex_mesh);
            PxShape* shape = physics->createShape(geometry, *material);
            if (shape)
            {
                // set local pose to position this shape relative to body center
                PxTransform local_pose(
                    PxVec3(local_pos.x, local_pos.y, local_pos.z),
                    PxQuat(local_rot.x, local_rot.y, local_rot.z, local_rot.w)
                );
                shape->setLocalPose(local_pose);
                shape->setFlag(PxShapeFlag::eVISUALIZATION, true);
                actor->attachShape(*shape);
                shape->release(); // actor owns the shape now
                shapes_created++;
            }
            else if (attach_box(minimum, maximum))
            {
                shapes_created++;
            }

            convex_mesh->release(); // shape holds its own reference
        }

        if (shapes_created == 0)
        {
            SP_LOG_WARNING(
                "No convex shapes were created for MeshConvex on '%s'",
                GetEntity() ? GetEntity()->GetObjectName().c_str() : "unknown"
            );
            actor->release();
            return false;
        }

        // update mass and inertia based on compound shape
        if (PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>())
        {
            if (m_center_of_mass != Vector3::Zero)
            {
                PxVec3 com(m_center_of_mass.x, m_center_of_mass.y, m_center_of_mass.z);
                PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass, &com);
            }
            else
            {
                PxRigidBodyExt::setMassAndUpdateInertia(*dynamic, m_mass);
            }
        }

        actor->userData = reinterpret_cast<void*>(GetEntity());
        PhysicsWorld::AddActor(actor);

        m_actors.resize(1, nullptr);
        m_actors[0] = actor;
        m_actors_active.assign(1, true);
        m_actors_active_count = 1;

        SP_LOG_INFO("MeshConvex created: %d convex shapes from %zu entities", shapes_created, render_entities.size());

        return true;
    }

    // primitive shapes and triangle or convex meshes from the render component, false when creation failed and Create should stop
    bool Physics::CreateShapes()
    {
        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());

        // mesh
        if (m_body_type == BodyType::Mesh)
        {
            Render* render = GetEntity()->GetComponent<Render>();
            if (!render)
            {
                SP_LOG_ERROR("No Render component found for mesh shape");
                return false;
            }

            // get geometry
            vector<uint32_t> indices;
            vector<RHI_Vertex_PosTexNorTan> vertices;
            render->GetGeometry(&indices, &vertices);
            if (vertices.empty() || indices.empty())
            {
                SP_LOG_ERROR("Empty vertex or index data for mesh shape");
                return false;
            }

            // simplify geometry
            const float volume        = render->GetBoundingBox().GetVolume();
            const float max_volume    = 100000.0f;
            // simplify geometry based on volume (larger objects get more detail)
            const float volume_factor       = clamp(volume / max_volume, 0.0f, 1.0f);
            const size_t min_index_count    = min<size_t>(indices.size(), 256);
            const size_t max_index_count    = 16'000;
            const size_t target_index_count = clamp<size_t>(static_cast<size_t>(indices.size() * volume_factor), min_index_count, max_index_count);
            // Procedural road meshes share exact junction boundaries. Independent
            // simplification can tear those seams and remove flat collision patches.
            const bool preserve_geometry = GetEntity()->GetComponent<Spline>() || render->HasFlag(RenderFlags::PreserveCollisionGeometry);
            // Hash the inputs before simplification/cooking. This also covers scale,
            // gravity-derived tolerances and the collision policy, not just the asset path.
            // Bump the version whenever simplification or cooking settings below change.
            const bool cook_convex = m_use_convex_hull || !(IsStatic() || IsKinematic());
            m_mesh_is_convex = cook_convex;
            generated_cache::Hash collision_hash;
            collision_hash.Add(uint32_t{1});
            collision_hash.Add(uint32_t{PX_PHYSICS_VERSION});
            collision_hash.Add(vertices);
            collision_hash.Add(indices);
            collision_hash.Add(GetEntity()->GetScale());
            collision_hash.Add(PhysicsWorld::GetGravity());
            collision_hash.Add(target_index_count);
            collision_hash.Add(preserve_geometry);
            // preserved geometry includes 3 mm guardrail sheet, a centimetre weld folds both faces into nothing
            const float weld_tolerance = preserve_geometry ? 0.001f : 0.01f;
            if (preserve_geometry)
            {
                collision_hash.Add(weld_tolerance);
            }
            collision_hash.Add(cook_convex);
            collision_hash.Add(render->HasInstancing());
            const auto collision_path = generated_cache::Path(World::GetResourceDirectory(), "collision", collision_hash.value);
            vector<uint8_t> cooked;
            auto load_cooked = [&]() -> void*
            {
                if (cooked.empty()) return nullptr;
                PxDefaultMemoryInputData input(cooked.data(), static_cast<PxU32>(cooked.size()));
                auto* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
                return cook_convex ? static_cast<void*>(physics->createConvexMesh(input))
                                   : static_cast<void*>(physics->createTriangleMesh(input));
            };
            if (generated_cache::Load(collision_path, collision_hash.value, cooked))
                m_mesh = load_cooked();

            if (!m_mesh)
            {
                if (!preserve_geometry)
                    geometry_processing::simplify(indices, vertices, target_index_count, false, false);

                // warn if we hit the complexity cap (original mesh was very detailed)
                if (!preserve_geometry && indices.size() > max_index_count && target_index_count == max_index_count)
                {
                    SP_LOG_WARNING("Mesh '%s' was simplified to %zu indices. It's still complex and may impact physics performance.", render->GetEntity()->GetObjectName().c_str(), target_index_count);
                }

                // convert vertices to physx format
                vector<PxVec3> px_vertices;
                px_vertices.reserve(vertices.size());
                Vector3 scale = GetEntity()->GetScale();
                for (const auto& vertex : vertices)
                {
                    px_vertices.emplace_back(vertex.pos[0] * scale.x, vertex.pos[1] * scale.y, vertex.pos[2] * scale.z);
                }

                // remove degenerate triangles (zero/near-zero area) that would cause physx cooking to fail
                {
                    const float area_epsilon = 1e-6f;
                    vector<uint32_t> valid_indices;
                    valid_indices.reserve(indices.size());

                    for (size_t i = 0; i < indices.size(); i += 3)
                    {
                        const PxVec3& v0 = px_vertices[indices[i]];
                        const PxVec3& v1 = px_vertices[indices[i + 1]];
                        const PxVec3& v2 = px_vertices[indices[i + 2]];

                        // compute triangle area via cross product
                        PxVec3 edge1 = v1 - v0;
                        PxVec3 edge2 = v2 - v0;
                        float area   = edge1.cross(edge2).magnitude() * 0.5f;

                        if (area > area_epsilon)
                        {
                            valid_indices.push_back(indices[i]);
                            valid_indices.push_back(indices[i + 1]);
                            valid_indices.push_back(indices[i + 2]);
                        }
                    }

                    indices = move(valid_indices);
                }

                if (indices.empty())
                {
                    SP_LOG_WARNING("Mesh '%s' has no valid triangles after degenerate removal, skipping physics", GetEntity()->GetObjectName().c_str());
                    return false;
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
                params.meshWeldTolerance               = weld_tolerance;
                params.meshAreaMinLimit                = 0.0f;
                params.meshEdgeLengthMaxLimit          = 500.0f;
                params.gaussMapLimit                   = 32;
                params.maxWeightRatioInTet             = FLT_MAX;

                // triangle mesh for exact collision, hull when the caller asked for one or the body
                // is dynamic, physx cannot simulate a dynamic triangle mesh
                if (!cook_convex)
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
                    PxDefaultMemoryOutputStream output;
                    const bool success = PxCookTriangleMesh(params, mesh_desc, output, &condition);
                    if (success && condition == PxTriangleMeshCookingResult::eSUCCESS)
                    {
                        cooked.assign(output.getData(), output.getData() + output.getSize());
                        m_mesh = load_cooked();
                    }
                    if (!m_mesh || !success || condition != PxTriangleMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create triangle mesh: %d", condition);
                        if (m_mesh)
                        {
                            static_cast<PxTriangleMesh*>(m_mesh)->release();
                            m_mesh = nullptr;
                        }
                        return false;
                    }
                }
                else // convex hull
                {
                    PxConvexMeshDesc mesh_desc;
                    mesh_desc.points.count  = static_cast<PxU32>(px_vertices.size());
                    mesh_desc.points.stride = sizeof(PxVec3);
                    mesh_desc.points.data   = px_vertices.data();
                    mesh_desc.flags         = PxConvexFlag::eCOMPUTE_CONVEX;
                    // Instanced vegetation and rocks can have thousands of extreme
                    // vertices. A bounded hull avoids PhysX's 255-polygon cooking
                    // failure and keeps repeated prop colliders inexpensive.
                    if (render->HasInstancing())
                        mesh_desc.vertexLimit = 64;

                    // create
                    PxConvexMeshCookingResult::Enum condition;
                    auto cook = [&]()
                    {
                        PxDefaultMemoryOutputStream output;
                        if (PxCookConvexMesh(params, mesh_desc, output, &condition) && condition == PxConvexMeshCookingResult::eSUCCESS)
                        {
                            cooked.assign(output.getData(), output.getData() + output.getSize());
                            m_mesh = load_cooked();
                        }
                    };
                    cook();
                    if (render->HasInstancing() && condition == PxConvexMeshCookingResult::ePOLYGONS_LIMIT_REACHED)
                    {
                        if (m_mesh) static_cast<PxConvexMesh*>(m_mesh)->release();
                        mesh_desc.flags |= PxConvexFlag::eQUANTIZE_INPUT;
                        mesh_desc.quantizedCount = 64;
                        m_mesh = nullptr;
                        cook();
                    }
                    if (!m_mesh || condition != PxConvexMeshCookingResult::eSUCCESS)
                    {
                        SP_LOG_ERROR("Failed to create convex mesh: %d", condition);
                        if (m_mesh)
                        {
                            static_cast<PxConvexMesh*>(m_mesh)->release();
                            m_mesh = nullptr;
                        }
                        return false;
                    }
                }
                if (m_mesh) generated_cache::Save(collision_path, collision_hash.value, cooked);
            }
        }

        CreateBodies();
        m_scale_previous = GetEntity()->GetScale();

        return true;
    }

    void Physics::UpdateShapeGeometry()
    {
        Vector3 scale = GetEntity()->GetScale();
        if (scale == m_scale_previous)
        {
            return;
        }

        m_scale_previous = scale;

        for (uint32_t i = 0; i < static_cast<uint32_t>(m_actors.size()); i++)
        {
            PxRigidActor* actor = static_cast<PxRigidActor*>(m_actors[i]);
            if (!actor)
            {
                continue;
            }

            PxShape* shape = nullptr;
            if (actor->getNbShapes() == 0)
            {
                continue;
            }
            actor->getShapes(&shape, 1);
            if (!shape)
            {
                continue;
            }

            switch (m_body_type)
            {
                case BodyType::Box:
                    shape->setGeometry(PxBoxGeometry(scale.x * 0.5f, scale.y * 0.5f, scale.z * 0.5f));
                    break;
                case BodyType::Sphere:
                {
                    float radius = max(max(scale.x, scale.y), scale.z) * 0.5f;
                    shape->setGeometry(PxSphereGeometry(radius));
                    break;
                }
                case BodyType::Capsule:
                {
                    float radius      = max(scale.x, scale.z) * 0.5f;
                    float half_height = scale.y * 0.5f;
                    shape->setGeometry(PxCapsuleGeometry(radius, half_height));
                    break;
                }
                default:
                    break;
            }
        }
    }

    void Physics::CreateHeightfield()
    {
        // the component sits on a tile child of the terrain, so each body covers only the slice of the
        // grid its tile draws, that keeps distance activation able to drop the ones far from the camera
        Entity* entity   = GetEntity();
        Terrain* terrain = entity->GetComponent<Terrain>();
        int tile_index   = -1;
        if (!terrain && entity->GetParent())
        {
            terrain    = entity->GetParent()->GetComponent<Terrain>();
            tile_index = Terrain::ParseTileIndex(entity);
        }

        if (!terrain || !terrain->HasHeightfield())
        {
            // a saved world can restore this component before the terrain has generated, the terrain
            // rebuilds the body itself once its grid exists
            SP_LOG_WARNING("a heightfield body needs a generated terrain component on itself or its parent");
            return;
        }

        const vector<Vector3>& positions = terrain->GetPositions();
        const uint32_t grid_width        = terrain->GetDenseWidth();  // samples along local x
        const uint32_t grid_height       = terrain->GetDenseHeight(); // samples along local z
        if (positions.size() < static_cast<size_t>(grid_width) * static_cast<size_t>(grid_height))
        {
            SP_LOG_ERROR("terrain grid holds fewer positions than its dimensions declare");
            return;
        }

        // the sample window, neighbouring tiles share their boundary row and column so the surfaces
        // meet without a seam and without overlapping each other
        uint32_t x_start = 0;
        uint32_t z_start = 0;
        uint32_t x_count = grid_width;
        uint32_t z_count = grid_height;
        if (tile_index >= 0)
        {
            const uint32_t n  = max(terrain->GetTileCountAxis(), 1u);
            const uint32_t tx = static_cast<uint32_t>(tile_index) % n;
            const uint32_t tz = static_cast<uint32_t>(tile_index) / n;

            x_start = (tx * (grid_width - 1)) / n;
            z_start = (tz * (grid_height - 1)) / n;
            x_count = ((tx + 1) * (grid_width - 1)) / n - x_start + 1;
            z_count = ((tz + 1) * (grid_height - 1)) / n - z_start + 1;
        }

        if (x_count < 2 || z_count < 2)
        {
            SP_LOG_WARNING("terrain tile %d is too small to build a heightfield from", tile_index);
            return;
        }

        generated_cache::Hash cache_key;
        cache_key.Add(uint32_t(1)); cache_key.Add(uint32_t(PX_PHYSICS_VERSION));
        cache_key.Add(x_count); cache_key.Add(z_count);
        for (uint32_t z = 0; z < z_count; ++z)
            cache_key.Bytes(positions.data() + size_t(z_start + z) * grid_width + x_start, size_t(x_count) * sizeof(Vector3));
        const auto cache_path = generated_cache::Path(World::GetResourceDirectory(), "heightfields", cache_key.value);
        vector<uint8_t> cooked;
        vector<float> metadata;
        float height_centre = 0, height_scale = 1;
        if (generated_cache::Load(cache_path, cache_key.value, cooked, metadata) && !cooked.empty() &&
            metadata.size() == 2 && std::isfinite(metadata[0]) && std::isfinite(metadata[1]) && metadata[1] > 0)
        {
            PxDefaultMemoryInputData input(cooked.data(), static_cast<PxU32>(cooked.size()));
            m_mesh = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics())->createHeightField(input);
            height_centre = metadata[0]; height_scale = metadata[1];
        }
        if (!m_mesh)
        {
            float height_min = positions[static_cast<size_t>(z_start) * grid_width + x_start].y;
            float height_max = height_min;
            for (uint32_t z = 0; z < z_count; z++)
            {
                const size_t source_row = static_cast<size_t>(z_start + z) * grid_width + x_start;
                for (uint32_t x = 0; x < x_count; x++)
                {
                    const float height = positions[source_row + x].y;
                    height_min         = min(height_min, height);
                    height_max         = max(height_max, height);
                }
            }

            // heights are int16, mapping the window's own range onto the full range keeps the step near a
            // millimetre, a fixed scale would cost metres of precision on a tall map
            height_centre = (height_max + height_min) * 0.5f;
            height_scale  = max((height_max - height_min) / 65534.0f, 1e-4f);

            // physx rows run along local x and columns along local z, the terrain grid is stored row major
            // in z, so the two indices swap on the way in
            const uint32_t sample_count = x_count * z_count;
            vector<PxHeightFieldSample> samples(sample_count);
            for (uint32_t z = 0; z < z_count; z++)
            {
                const size_t source_row = static_cast<size_t>(z_start + z) * grid_width + x_start;
                for (uint32_t x = 0; x < x_count; x++)
                {
                    const float quantised = (positions[source_row + x].y - height_centre) / height_scale;
                    samples[x * z_count + z].height = static_cast<PxI16>(clamp(quantised, -32767.0f, 32767.0f));
                }
            }

            PxHeightFieldDesc desc;
            desc.nbRows         = x_count;
            desc.nbColumns      = z_count;
            desc.format         = PxHeightFieldFormat::eS16_TM;
            desc.samples.data   = samples.data();
            desc.samples.stride = sizeof(PxHeightFieldSample);

            PxDefaultMemoryOutputStream output;
            if (PxCookHeightField(desc, output))
            {
                cooked.assign(output.getData(), output.getData() + output.getSize());
                PxDefaultMemoryInputData input(cooked.data(), static_cast<PxU32>(cooked.size()));
                m_mesh = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics())->createHeightField(input);
                if (m_mesh) generated_cache::Save(cache_path, cache_key.value, cooked, vector<float>{height_centre, height_scale});
        }
        if (!m_mesh) m_mesh = PxCreateHeightField(desc, *PxGetStandaloneInsertionCallback());
        }
        if (!m_mesh)
        {
            SP_LOG_ERROR("failed to create the terrain heightfield");
            return;
        }
        m_mesh_is_heightfield = true;

        // a physx grid grows out of its corner, so the shape is pushed back to where the window starts
        // in terrain space and then lifted to the middle of the height range
        const TerrainGridMapping mapping = terrain->GetGridMapping();
        m_heightfield_scale_row          = mapping.scale_x;
        m_heightfield_scale_column       = mapping.scale_z;
        m_heightfield_scale_height       = height_scale;
        m_heightfield_offset             = Vector3(
            -mapping.offset_x + static_cast<float>(x_start) * mapping.scale_x,
            height_centre,
            -mapping.offset_z + static_cast<float>(z_start) * mapping.scale_z
        );

        // the shape pose is relative to the tile entity, which already carries the tile's own offset
        if (tile_index >= 0)
        {
            m_heightfield_offset.x -= entity->GetPositionLocal().x;
            m_heightfield_offset.z -= entity->GetPositionLocal().z;
        }
    }

    void Physics::CreateBodies()
    {
        m_activation_valid = false;
        PxPhysics* physics      = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        Render* render  = GetEntity()->GetComponent<Render>();

        // determine instance count - use render if available, otherwise single instance
        const uint32_t instance_count = render ? render->GetInstanceCount() : 1;

        // create bodies and shapes
        m_actors.resize(instance_count, nullptr);
        m_actors_active.assign(instance_count, false);
        m_actors_active_count = 0;
        Camera* camera = World::GetCamera();
        const bool stream_static = IsStatic() && render && camera && m_distance_streaming;
        const Vector3 camera_position = camera ? camera->GetEntity()->GetPosition() : Vector3::Zero;
        const Matrix& world = GetEntity()->GetMatrix();
        for (uint32_t i = 0; i < instance_count; i++)
        {
            math::Matrix transform = (render && render->HasInstancing()) ? render->GetInstance(i, true) : GetEntity()->GetMatrix();
            PxTransform pose = to_px_transform(transform);
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
                        // must mirror the cooking branch in Create, the pointer is one or the other
                        const bool is_convex = m_use_convex_hull || !(IsStatic() || IsKinematic());

                        // per instance scale, a scattered prop varies size per copy and without this
                        // every instance collides at the size of the source mesh
                        Vector3 scale = render->HasInstancing() ? render->GetInstance(i, false).GetScale() : Vector3::One;
                        PxMeshScale mesh_scale(PxVec3(scale.x, scale.y, scale.z)); // runtime transform, cheap for statics but not reflected in the baked shape (raycasts etc)

                        if (is_convex)
                        {
                            PxConvexMeshGeometry geometry(static_cast<PxConvexMesh*>(m_mesh), mesh_scale);
                            shape = physics->createShape(geometry, *material);
                        }
                        else
                        {
                            PxTriangleMeshGeometry geometry(static_cast<PxTriangleMesh*>(m_mesh), mesh_scale);
                            shape = physics->createShape(geometry, *material);
                        }
                    }
                    break;
                }
                case BodyType::Heightfield:
                {
                    if (m_mesh)
                    {
                        PxHeightFieldGeometry geometry(
                            static_cast<PxHeightField*>(m_mesh),
                            PxMeshGeometryFlags(),
                            m_heightfield_scale_height,
                            m_heightfield_scale_row,
                            m_heightfield_scale_column
                        );
                        shape = physics->createShape(geometry, *material);
                        shape->setLocalPose(PxTransform(PxVec3(m_heightfield_offset.x, m_heightfield_offset.y, m_heightfield_offset.z)));
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
                // Match the first distance tick without first inserting the entire
                // tile into the broadphase and immediately removing distant copies.
                const Vector3 closest_point = stream_static
                    ? (render->HasInstancing() ? render->GetInstancePosition(i, world)
                                              : render->GetBoundingBox().GetClosestPoint(camera_position))
                    : camera_position;
                if (!stream_static || Vector3::DistanceSquared(camera_position, closest_point) <= distance_deactivate_squared)
                {
                    PhysicsWorld::AddActor(actor);
                    m_actors_active[i] = true;
                    ++m_actors_active_count;
                }
            }

            m_actors[i] = actor;
        }

        // the actors now match the instance list, a notification raised before this point is stale
        m_instances_dirty = false;
    }

    // a scattered prop owns one actor per render instance, when the terrain hides, restores or snaps
    // instances the list changes size and the actors are rebuilt from the cooked shape already held in
    // m_mesh, going through Create would recook the hull for every drag step
    void Physics::RebuildInstanceActors()
    {
        // only the body types that CreateBodies lays out per instance
        switch (m_body_type)
        {
            case BodyType::Box:
            case BodyType::Sphere:
            case BodyType::Capsule:
            case BodyType::Plane:
            case BodyType::Mesh:
            case BodyType::Heightfield:
                break;
            default:
                return;
        }

        // never created, or the shape cook failed, there is nothing to lay out
        if (!m_material || !PhysicsWorld::GetScene())
        {
            return;
        }
        if ((m_body_type == BodyType::Mesh || m_body_type == BodyType::Heightfield) && !m_mesh)
        {
            return;
        }

        lock_guard<recursive_mutex> physx_lock(PhysicsWorld::GetMutex());

        // release the old set, distance activation may have already pulled some out of the scene
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
        m_actors_active_count = 0;

        CreateBodies();
    }

    void Physics::CreateCloth()
    {
        Render* render = GetEntity()->GetComponent<Render>();
        if (!render)
        {
            SP_LOG_ERROR("Cloth requires a Render component");
            return;
        }

        // extract geometry from the render's mesh
        vector<uint32_t> indices;
        vector<RHI_Vertex_PosTexNorTan> vertices;
        render->GetGeometry(&indices, &vertices);
        if (vertices.empty() || indices.empty())
        {
            SP_LOG_ERROR("Cloth mesh has no geometry");
            return;
        }

        Mesh* source_mesh = render->GetMesh();
        uint32_t source_sub_mesh_index = render->GetSubMeshIndex();
        shared_ptr<Mesh> cloth_mesh = make_shared<Mesh>();
        cloth_mesh->SetObjectName(source_mesh->GetObjectName());
        cloth_mesh->SetType(source_mesh->GetType());
        cloth_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        cloth_mesh->ReserveSubMeshes(source_sub_mesh_index + 1);
        vector<RHI_Vertex_PosTexNorTan> cloth_mesh_vertices = vertices;
        vector<uint32_t> cloth_mesh_indices = indices;
        cloth_mesh->AddGeometry(cloth_mesh_vertices, cloth_mesh_indices, false, source_sub_mesh_index);
        cloth_mesh->CreateGpuBuffers();
        render->SetMesh(cloth_mesh.get(), source_sub_mesh_index);
        m_cloth_mesh = move(cloth_mesh);

        // store the global geometry buffer offset so we can update vertices in-place later
        m_cloth_global_vertex_offset = render->GetVertexOffset();
        m_cloth_vertex_count         = render->GetVertexCount();

        // entity transform for converting local-space vertices to world space
        Vector3 entity_pos   = GetEntity()->GetPosition();
        Quaternion entity_rot = GetEntity()->GetRotation();
        Vector3 entity_scale  = GetEntity()->GetScale();

        // initialize particles from mesh vertices (local space, pre-scaled)
        m_cloth_particles.resize(vertices.size());
        for (size_t i = 0; i < vertices.size(); i++)
        {
            Vector3 local_pos(
                vertices[i].pos[0] * entity_scale.x,
                vertices[i].pos[1] * entity_scale.y,
                vertices[i].pos[2] * entity_scale.z
            );

            Vector3 world_pos = entity_pos + entity_rot * local_pos;

            m_cloth_particles[i].position          = world_pos;
            m_cloth_particles[i].previous_position = world_pos;
            m_cloth_particles[i].inverse_mass      = 1.0f / max(m_mass, 0.001f);
        }

        // imported meshes duplicate vertices at seams and hard edges, weld the coincident ones so the cloth stays connected
        {
            const float weld_epsilon = 1e-4f;

            m_cloth_weld_map.resize(vertices.size());
            map<tuple<int32_t, int32_t, int32_t>, uint32_t> position_to_canonical;
            const float quantize = 1.0f / weld_epsilon;

            for (uint32_t i = 0; i < static_cast<uint32_t>(m_cloth_particles.size()); i++)
            {
                const Vector3& pos = m_cloth_particles[i].position;
                auto key = make_tuple(
                    static_cast<int32_t>(roundf(pos.x * quantize)),
                    static_cast<int32_t>(roundf(pos.y * quantize)),
                    static_cast<int32_t>(roundf(pos.z * quantize))
                );

                auto it = position_to_canonical.find(key);
                if (it != position_to_canonical.end())
                {
                    m_cloth_weld_map[i] = it->second;
                    m_cloth_particles[i].inverse_mass = 0.0f; // slave: driven by canonical
                }
                else
                {
                    position_to_canonical[key] = i;
                    m_cloth_weld_map[i] = i;
                }
            }
        }

        float max_pin_projection = -FLT_MAX;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cloth_particles.size()); i++)
        {
            if (m_cloth_weld_map[i] == i)
            {
                max_pin_projection = max(max_pin_projection, Vector3::Dot(m_cloth_particles[i].position, m_cloth_pin_direction));
            }
        }

        const float pin_threshold = 0.05f;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cloth_particles.size()); i++)
        {
            if (m_cloth_weld_map[i] == i && Vector3::Dot(m_cloth_particles[i].position, m_cloth_pin_direction) >= max_pin_projection - pin_threshold)
            {
                m_cloth_particles[i].inverse_mass = 0.0f;
            }
        }

        // store triangle indices and original vertices for normal recalculation + tex/tan preservation
        m_cloth_indices = indices;
        m_cloth_base_vertices = vertices;

        // build distance constraints from triangle edges using welded (canonical) indices
        // so constraints span across uv seams and hard edges
        auto edge_key = [](uint32_t a, uint32_t b) -> uint64_t
        {
            if (a > b)
            {
                swap(a, b);
            }
            return (static_cast<uint64_t>(a) << 32) | static_cast<uint64_t>(b);
        };

        unordered_set<uint64_t> seen_edges;
        for (size_t i = 0; i < indices.size(); i += 3)
        {
            uint32_t tri[3] = { indices[i], indices[i + 1], indices[i + 2] };
            for (int e = 0; e < 3; e++)
            {
                uint32_t a = m_cloth_weld_map[tri[e]];
                uint32_t b = m_cloth_weld_map[tri[(e + 1) % 3]];
                if (a == b)
                {
                    continue;
                }

                uint64_t key = edge_key(a, b);
                if (seen_edges.insert(key).second)
                {
                    ClothConstraint c;
                    c.index_a     = a;
                    c.index_b     = b;
                    c.rest_length = (m_cloth_particles[a].position - m_cloth_particles[b].position).Length();
                    m_cloth_constraints.push_back(c);
                }
            }
        }

        uint32_t canonical_count = 0;
        for (uint32_t i = 0; i < static_cast<uint32_t>(m_cloth_weld_map.size()); i++)
        {
            if (m_cloth_weld_map[i] == i)
            {
                canonical_count++;
            }
        }

        // the renderer may have already built this entity's blas without the update bit,
        // so invalidate it to force a rebuild with ALLOW_UPDATE_BIT on the next frame
        render->SetAllowBlasUpdate(true);
        render->InvalidateAccelerationStructure();

        SP_LOG_INFO("Cloth created: %u particles (%zu vertices, %u welded), %zu constraints, %zu triangles",
            canonical_count, m_cloth_particles.size(), static_cast<uint32_t>(m_cloth_particles.size()) - canonical_count,
            m_cloth_constraints.size(), indices.size() / 3);
    }

    void Physics::TickCloth(bool is_playing, float delta_time)
    {
        if (!is_playing || m_cloth_particles.empty())
        {
            return;
        }

        // sub-step with a fixed maximum dt to prevent simulation explosion from frame spikes
        // (the synchronous gpu upload in UpdateVertices can stall the cpu, causing large delta_time values)
        const float max_dt      = 1.0f / 60.0f;
        const float clamped_dt  = min(delta_time, max_dt);
        const float dt          = clamped_dt;
        const float damping     = 1.0f - m_cloth_damping;
        const Vector3 gravity   = PhysicsWorld::GetGravity();

        // verlet integration
        for (auto& p : m_cloth_particles)
        {
            if (p.inverse_mass == 0.0f)
            {
                continue;
            }

            Vector3 velocity = (p.position - p.previous_position) * damping;
            p.previous_position = p.position;
            p.position += velocity + gravity * (dt * dt);
        }

        // wind
        if (m_cloth_wind_enabled)
        {
            float time = static_cast<float>(Timer::GetTimeSec());
            for (auto& p : m_cloth_particles)
            {
                if (p.inverse_mass == 0.0f)
                {
                    continue;
                }

                Vector3 wind = World::SampleWind(p.position, time);
                p.position += wind * (p.inverse_mass * dt * dt);
            }
        }

        // constraint projection
        for (uint32_t iter = 0; iter < m_cloth_iterations; iter++)
        {
            for (const auto& c : m_cloth_constraints)
            {
                ClothParticle& pa = m_cloth_particles[c.index_a];
                ClothParticle& pb = m_cloth_particles[c.index_b];

                Vector3 delta       = pb.position - pa.position;
                float current_length = delta.Length();
                if (current_length < 1e-7f)
                {
                    continue;
                }

                float diff         = (current_length - c.rest_length) / current_length;
                float total_weight = pa.inverse_mass + pb.inverse_mass;
                if (total_weight == 0.0f)
                {
                    continue;
                }

                Vector3 correction = delta * (diff * m_cloth_stiffness / total_weight);
                pa.position += correction * pa.inverse_mass;
                pb.position -= correction * pb.inverse_mass;
            }
        }

        // sync welded vertices: copy canonical positions to their duplicates
        if (!m_cloth_weld_map.empty())
        {
            for (uint32_t i = 0; i < static_cast<uint32_t>(m_cloth_particles.size()); i++)
            {
                uint32_t canonical = m_cloth_weld_map[i];
                if (canonical != i)
                {
                    m_cloth_particles[i].position          = m_cloth_particles[canonical].position;
                    m_cloth_particles[i].previous_position = m_cloth_particles[canonical].previous_position;
                }
            }
        }

        // ground collision (simple y=0 plane)
        const float ground_y = 0.0f;
        for (auto& p : m_cloth_particles)
        {
            if (p.position.y < ground_y)
            {
                p.position.y = ground_y;
            }
        }

        // clamp particle positions to a sane world-space bound to prevent diverged
        // particles from producing nan/inf vertex data that crashes the gpu
        const float position_limit = 10000.0f;
        for (auto& p : m_cloth_particles)
        {
            if (p.inverse_mass == 0.0f)
            {
                continue;
            }

            p.position.x = clamp(p.position.x, -position_limit, position_limit);
            p.position.y = clamp(p.position.y, -position_limit, position_limit);
            p.position.z = clamp(p.position.z, -position_limit, position_limit);

            p.previous_position.x = clamp(p.previous_position.x, -position_limit, position_limit);
            p.previous_position.y = clamp(p.previous_position.y, -position_limit, position_limit);
            p.previous_position.z = clamp(p.previous_position.z, -position_limit, position_limit);
        }

        // write updated positions back to render vertices and recalculate normals
        if (m_cloth_base_vertices.empty())
        {
            return;
        }

        // copy cached vertices to preserve tex/tan attributes
        vector<RHI_Vertex_PosTexNorTan> updated_vertices = m_cloth_base_vertices;

        // get the entity's inverse transform to convert world-space particles back to local space
        Vector3 entity_pos    = GetEntity()->GetPosition();
        Quaternion entity_rot = GetEntity()->GetRotation();
        Quaternion inv_rot    = entity_rot.Conjugate();
        Vector3 entity_scale  = GetEntity()->GetScale();
        Vector3 inv_scale(
            entity_scale.x != 0.0f ? 1.0f / entity_scale.x : 0.0f,
            entity_scale.y != 0.0f ? 1.0f / entity_scale.y : 0.0f,
            entity_scale.z != 0.0f ? 1.0f / entity_scale.z : 0.0f
        );

        // copy particle positions into vertex buffer (convert back to local space)
        for (uint32_t i = 0; i < m_cloth_vertex_count && i < static_cast<uint32_t>(m_cloth_particles.size()); i++)
        {
            Vector3 local_pos = inv_rot * (m_cloth_particles[i].position - entity_pos);
            local_pos.x *= inv_scale.x;
            local_pos.y *= inv_scale.y;
            local_pos.z *= inv_scale.z;

            updated_vertices[i].pos[0] = local_pos.x;
            updated_vertices[i].pos[1] = local_pos.y;
            updated_vertices[i].pos[2] = local_pos.z;
        }

        // recalculate normals from triangle faces, accumulated in float buffers and packed at the end
        vector<Vector3> tmp_normals(updated_vertices.size(), Vector3::Zero);
        vector<Vector3> tmp_tangents(updated_vertices.size(), Vector3::Zero);

        for (size_t i = 0; i + 2 < m_cloth_indices.size(); i += 3)
        {
            uint32_t i0 = m_cloth_indices[i];
            uint32_t i1 = m_cloth_indices[i + 1];
            uint32_t i2 = m_cloth_indices[i + 2];

            if (i0 >= m_cloth_vertex_count || i1 >= m_cloth_vertex_count || i2 >= m_cloth_vertex_count)
            {
                continue;
            }

            Vector3 v0(updated_vertices[i0].pos[0], updated_vertices[i0].pos[1], updated_vertices[i0].pos[2]);
            Vector3 v1(updated_vertices[i1].pos[0], updated_vertices[i1].pos[1], updated_vertices[i1].pos[2]);
            Vector3 v2(updated_vertices[i2].pos[0], updated_vertices[i2].pos[1], updated_vertices[i2].pos[2]);

            Vector3 edge1  = v1 - v0;
            Vector3 edge2  = v2 - v0;
            Vector3 normal = Vector3::Cross(edge1, edge2);

            tmp_normals[i0] += normal;
            tmp_normals[i1] += normal;
            tmp_normals[i2] += normal;
        }

        // recalculate tangents from triangle uvs and deformed positions
        for (size_t i = 0; i + 2 < m_cloth_indices.size(); i += 3)
        {
            uint32_t i0 = m_cloth_indices[i];
            uint32_t i1 = m_cloth_indices[i + 1];
            uint32_t i2 = m_cloth_indices[i + 2];

            if (i0 >= m_cloth_vertex_count || i1 >= m_cloth_vertex_count || i2 >= m_cloth_vertex_count)
            {
                continue;
            }

            Vector3 p0(updated_vertices[i0].pos[0], updated_vertices[i0].pos[1], updated_vertices[i0].pos[2]);
            Vector3 p1(updated_vertices[i1].pos[0], updated_vertices[i1].pos[1], updated_vertices[i1].pos[2]);
            Vector3 p2(updated_vertices[i2].pos[0], updated_vertices[i2].pos[1], updated_vertices[i2].pos[2]);

            Vector3 edge1 = p1 - p0;
            Vector3 edge2 = p2 - p0;

            Vector2 uv0 = updated_vertices[i0].get_uv();
            Vector2 uv1 = updated_vertices[i1].get_uv();
            Vector2 uv2 = updated_vertices[i2].get_uv();
            float du1 = uv1.x - uv0.x;
            float dv1 = uv1.y - uv0.y;
            float du2 = uv2.x - uv0.x;
            float dv2 = uv2.y - uv0.y;

            float denom = du1 * dv2 - du2 * dv1;
            if (fabsf(denom) < 1e-7f)
            {
                continue;
            }

            float inv_denom = 1.0f / denom;
            Vector3 tangent = (edge1 * dv2 - edge2 * dv1) * inv_denom;

            tmp_tangents[i0] += tangent;
            tmp_tangents[i1] += tangent;
            tmp_tangents[i2] += tangent;
        }

        // normalize and orthogonalize, pack into the vertex
        for (uint32_t i = 0; i < m_cloth_vertex_count; i++)
        {
            Vector3 n = tmp_normals[i];
            float n_len = n.Length();
            if (n_len > 1e-7f)
            {
                n /= n_len;
                updated_vertices[i].set_normal(n);
            }
            else
            {
                n = updated_vertices[i].get_normal();
            }

            Vector3 t = tmp_tangents[i];
            t = t - n * Vector3::Dot(n, t);
            float t_len = t.Length();
            if (t_len > 1e-7f)
            {
                t /= t_len;
                updated_vertices[i].set_tangent(t);
            }
        }

        // push to gpu
        GeometryBuffer::UpdateVertices(updated_vertices.data(), m_cloth_global_vertex_offset, m_cloth_vertex_count);

        // signal that the blas needs an in-place refit so ray-traced shadows track the deformed mesh
        if (Render* render = GetEntity()->GetComponent<Render>())
        {
            render->SetNeedsBlasRefit(true);
        }
    }
    namespace { Physics::BodyFactory custom_body_factory = nullptr; }

    void Physics::SetBodyFactory(BodyFactory factory) { custom_body_factory = factory; }

    PhysicsBody& Physics::EnsureCustomBody() const
    {
        SP_ASSERT_MSG(custom_body_factory, "This world requires an application physics body factory");
        if (!m_custom_body) m_custom_body = custom_body_factory(*const_cast<Physics*>(this));
        SP_ASSERT(m_custom_body);
        return *m_custom_body;
    }
    PhysicsSettings Physics::GetSettings() const
    {
        PhysicsSettings settings;
        settings.mass = m_mass;
        settings.friction = m_friction;
        settings.friction_rolling = m_friction_rolling;
        settings.restitution = m_restitution;
        settings.is_static = m_is_static;
        settings.is_kinematic = m_is_kinematic;
        settings.position_lock = m_position_lock;
        settings.rotation_lock = m_rotation_lock;
        settings.center_of_mass = m_center_of_mass;
        settings.use_convex_hull = m_use_convex_hull;
        settings.distance_streaming = m_distance_streaming;
        settings.body_type = m_body_type;
        settings.cloth_stiffness = m_cloth_stiffness;
        settings.cloth_damping = m_cloth_damping;
        settings.cloth_iterations = m_cloth_iterations;
        settings.cloth_wind_enabled = m_cloth_wind_enabled;
        settings.cloth_pin_direction = m_cloth_pin_direction;
        return settings;
    }

    void Physics::ApplySettings(const PhysicsSettings& value)
    {
        PhysicsSettings settings = value;
        settings.Validate();
        m_mass = settings.mass;
        m_friction = settings.friction;
        m_friction_rolling = settings.friction_rolling;
        m_restitution = settings.restitution;
        m_is_static = settings.is_static;
        m_is_kinematic = settings.is_kinematic;
        m_position_lock = settings.position_lock;
        m_rotation_lock = settings.rotation_lock;
        m_center_of_mass = settings.center_of_mass;
        m_use_convex_hull = settings.use_convex_hull;
        m_distance_streaming = settings.distance_streaming;
        m_body_type = settings.body_type;
        m_cloth_stiffness = settings.cloth_stiffness;
        m_cloth_damping = settings.cloth_damping;
        m_cloth_iterations = settings.cloth_iterations;
        m_cloth_wind_enabled = settings.cloth_wind_enabled;
        m_cloth_pin_direction = settings.cloth_pin_direction;
        // One deferred rebuild after all authored values have been applied.
        m_needs_creation = true;
        GetEntity()->RefreshPreTickGate();
    }

    void Physics::CopyFrom(const Component& source)
    {
        SP_ASSERT(source.GetType() == ComponentType::Physics);
        ApplySettings(static_cast<const Physics&>(source).GetSettings());
    }
    void PhysicsSettings::Validate()
    {
        mass = isfinite(mass) ? clamp(mass, 0.001f, 10000.0f) : 1.0f;
        friction = isfinite(friction) ? max(friction, 0.0f) : 0.4f;
        friction_rolling = isfinite(friction_rolling) ? max(friction_rolling, 0.0f) : 0.4f;
        restitution = isfinite(restitution) ? clamp(restitution, 0.0f, 1.0f) : 0.2f;
        if (body_type < BodyType::Box || body_type > BodyType::Max) body_type = BodyType::Max;
        cloth_stiffness = isfinite(cloth_stiffness) ? clamp(cloth_stiffness, 0.0f, 1.0f) : 0.9f;
        cloth_damping = isfinite(cloth_damping) ? clamp(cloth_damping, 0.0f, 1.0f) : 0.01f;
        cloth_iterations = clamp(cloth_iterations, 1u, 32u);
        cloth_pin_direction = cloth_pin_direction.IsFinite() && cloth_pin_direction.LengthSquared() > 0.0001f ? cloth_pin_direction.Normalized() : Vector3::Up;
    }
    void Physics::SetClothStiffness(float stiffness)
    {
        PhysicsSettings settings = GetSettings();
        settings.cloth_stiffness = stiffness;
        settings.Validate();
        m_cloth_stiffness = settings.cloth_stiffness;
    }
    void Physics::SetClothDamping(float damping)
    {
        PhysicsSettings settings = GetSettings();
        settings.cloth_damping = damping;
        settings.Validate();
        m_cloth_damping = settings.cloth_damping;
    }
    void Physics::SetClothIterations(uint32_t count)
    {
        PhysicsSettings settings = GetSettings();
        settings.cloth_iterations = count;
        settings.Validate();
        m_cloth_iterations = settings.cloth_iterations;
    }
}
