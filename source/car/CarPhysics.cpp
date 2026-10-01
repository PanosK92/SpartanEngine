
/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "CarPhysicsState.h"
#include "CarPhysics.h"
#include "../profiling/WorldWork.h"
#include "../world/components/Spline.h"
#include <unordered_set>
#include "../world/components/Physics.h"
#include "../profiling/Profiler.h"
#include "../world/components/Render.h"
#include "../world/components/Camera.h"
#include "../world/components/Terrain.h"
#include "../world/Entity.h"
#include "../rhi/RHI_Vertex.h"
#include "../physics/PhysicsWorld.h"
#include "Car.h"
#include "CarSimulation.h"
#include "CarCalibration.h"
#include "CarChassisCollision.h"
#include "../world/Weather.h"
#include "../geometry/Mesh.h"
#include "../geometry/GeneratedCache.h"
#include "../geometry/GeometryProcessing.h"
#include "../rendering/Renderer.h"
#include "../rendering/Material.h"
#include "../rendering/GeometryBuffer.h"
#include "../core/ProgressTracker.h"
#include "../core/ThreadPool.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
#ifdef DEBUG
    #define _DEBUG 1
    #undef NDEBUG
#else
    #define NDEBUG 1
    #undef _DEBUG
#endif
#define PX_PHYSX_STATIC_LIB
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
using namespace physx;
//============================

#include "../physics/PhysicsConversions.h"
using namespace spartan::physics_detail;

namespace spartan
{
    struct TireDeformationBatch
    {
        std::vector<std::function<void()>> skin, uploads;
    };

    struct TireVisualState
    {
        struct BoundVertex
        {
            car::tire_cage::binding weights;
            Vector3 position, tangent, bitangent;
            uint32_t vertex_index, lod_index;
        };
        struct Part
        {
            uint64_t entity_id;
            std::shared_ptr<Mesh> source, mesh;
            uint32_t submesh;
            uint32_t previous_lod = UINT32_MAX;
            Matrix to_wheel, from_wheel;
            // Bindings are grouped by cage cell so unchanged arcs are never scanned.
            std::vector<BoundVertex> vertices;
            std::array<uint32_t, car::tire_cage::node_count + 1> cell_offsets{};
            static constexpr uint32_t upload_block_size = 256;
            std::array<std::vector<uint32_t>, car::tire_cage::node_count> cell_blocks;
            std::vector<uint8_t> dirty_blocks;
            std::array<Vector3, car::tire_cage::node_count> local_displacement;
            std::array<uint16_t, car::tire_cage::node_count> dirty_cells;
        };
        car::tire_cage cage;
        std::vector<Part> parts;
        bool deformed = false, has_shape = false;
        Vector3 previous_normal;
        float previous_distance = 0, previous_stiffness = 0;
        float previous_pressure = 0, previous_reference_stiffness = 0, previous_ambient_pressure = 0;
        std::array<bool, car::tire_cage::node_count> active_cells{};
    };

    VehiclePhysicsState& CarPhysics::VehicleState() const
    {
        if (!m_vehicle) m_vehicle.reset(new VehiclePhysicsState());
        return *m_vehicle;
    }

    bool CarPhysics::IsVehicleSimulationActive() const
    { return VehicleState().vehicle_simulation_active; }

    void CarPhysics::SetVehicleBrakeReverseEnabled(bool enabled)
    { VehicleState().vehicle_brake_reverse_enabled = enabled; }

    bool CarPhysics::GetVehicleBrakeReverseEnabled() const
    { return VehicleState().vehicle_brake_reverse_enabled; }

    void CarPhysics::SetVehicleFullSteeringLock(bool enabled)
    { VehicleState().vehicle_full_steering_lock = enabled; }

    void CarPhysics::SetVehicleRoadSurface(const math::Vector3& position, const math::Vector3& tangent)
    {
        VehicleState().vehicle_road_surface = true;
        VehicleState().vehicle_road_position = position;
        VehicleState().vehicle_road_tangent = tangent;
        const math::Vector3 across(tangent.z, 0.0f, -tangent.x);
        VehicleState().vehicle_road_normal = math::Vector3::Cross(tangent, across).Normalized();
    }

    VehicleSimMode CarPhysics::GetVehicleSimMode() const
    { return VehicleState().vehicle_sim_mode; }

    Entity* CarPhysics::GetChassisEntity() const
    { return VehicleState().chassis_entity; }

    float CarPhysics::GetWheelRadius() const
    { return VehicleState().wheel_radius; }

    void CarPhysics::SetCar(class Car* car)
    { VehicleState().car = car; }

    class Car* CarPhysics::GetCar() const
    { return m_vehicle ? m_vehicle->car : nullptr; }

    car::Simulation* CarPhysics::GetVehicleSimulation() const
    { return m_vehicle ? m_vehicle->vehicle_simulation.get() : nullptr; }

    void VehiclePhysicsDeleter::operator()(VehiclePhysicsState* state) const { delete state; }

    void RegisterVehicleContacts()
    {
        PhysicsWorld::SetContactObserver([](const PxContactPairHeader& header, const PxContactPair* pairs, uint32_t pair_count) {
            Entity* entities[] = {
                header.actors[0] ? static_cast<Entity*>(header.actors[0]->userData) : nullptr,
                header.actors[1] ? static_cast<Entity*>(header.actors[1]->userData) : nullptr
            };
            // Preserve the full solver impulse, including contacts with anonymous static geometry.
            for (uint32_t i = 0; i < pair_count; ++i)
            {
                PxVec3 impulse(0);
                vector<PxContactPairPoint> contacts(pairs[i].contactCount);
                const PxU32 count = contacts.empty() ? 0 : pairs[i].extractContacts(contacts.data(), static_cast<PxU32>(contacts.size()));
                for (PxU32 j = 0; j < count; ++j) impulse += contacts[j].impulse;
                for (uint32_t actor = 0; actor < 2; ++actor)
                    if (entities[actor]) if (auto* physics = entities[actor]->GetComponent<Physics>())
                        if (auto* body = dynamic_cast<CarPhysics*>(physics->GetCustomBody()))
                        if (auto* simulation = body->GetVehicleSimulation())
                            simulation->record_contact_impulse(actor == 0 ? impulse : -impulse,
                                entities[1 - actor] ? entities[1 - actor]->GetObjectId() : 0, count, unsigned(pairs[i].flags));
            }
            uint32_t chassis_mask = 0;
            for (uint32_t actor = 0; actor < 2; ++actor)
                if (entities[actor]) if (auto* physics = entities[actor]->GetComponent<Physics>())
                    if (auto* body = dynamic_cast<CarPhysics*>(physics->GetCustomBody()))
                        if (auto* simulation = body->GetVehicleSimulation())
                        if (simulation->get_body() == header.actors[actor]) chassis_mask |= 1u << actor;
            return chassis_mask;
        });
    }

    VehiclePhysicsState::VehiclePhysicsState() = default;
    VehiclePhysicsState::~VehiclePhysicsState() = default;

    namespace
    {
        // Resolve generated collision entities before the legacy name fallback.
        car::surface_type classify_ground_actor(const PxRigidActor* actor)
        {
            if (!actor || !actor->userData)
            {
                return car::surface_asphalt;
            }

            Entity* entity = static_cast<Entity*>(actor->userData);
            const Physics* physics = entity->GetComponent<Physics>();
            if (entity->GetComponent<Terrain>() || (physics && physics->GetBodyType() == BodyType::Heightfield))
            {
                return car::surface_dirt;
            }
            if (entity->GetObjectName() == "spline_road_shoulder")
                return car::surface_gravel;
            if (entity->GetObjectName() == "spline_sidewalk")
                return car::surface_concrete;
            if (Render* render = entity->GetComponent<Render>())
            {
                if (Material* material = render->GetMaterial())
                {
                    if (material->GetProperty(MaterialProperty::IsRoadSurface) > 0.0f)
                        return car::surface_asphalt;
                }
            }

            string name = entity->GetObjectName();
            for (char& c : name)
            {
                if (c >= 'A' && c <= 'Z')
                {
                    c = static_cast<char>(c - 'A' + 'a');
                }
            }

            if (name.find("ice") != string::npos || name.find("snow") != string::npos)
            {
                return car::surface_ice;
            }
            if (name.find("grass") != string::npos || name.find("lawn") != string::npos)
            {
                return car::surface_grass;
            }
            if (name.find("dirt") != string::npos)
            {
                return car::surface_dirt;
            }
            if (name.find("gravel") != string::npos || name.find("sand") != string::npos)
            {
                return car::surface_gravel;
            }
            if (name.find("wet") != string::npos || name.find("puddle") != string::npos)
            {
                return car::surface_wet_asphalt;
            }
            if (name.find("concrete") != string::npos)
            {
                return car::surface_concrete;
            }
            return car::surface_asphalt;
        }

        float resolve_water_depth(const PxVec3& point, car::surface_type surface)
        {
            // a surface authored as wet already carries its friction, ice is frozen water
            if (surface == car::surface_ice || surface == car::surface_wet_asphalt)
            {
                return 0.0f;
            }

            const bool porous = surface == car::surface_dirt || surface == car::surface_grass || surface == car::surface_gravel;
            return Weather::GetWaterDepth(Vector3(point.x, point.y, point.z), porous);
        }

    }
    car::Simulation* CarPhysics::EnsureVehicleSimulation()
    {
        if (!VehicleState().vehicle_simulation)
        {
            VehicleState().vehicle_simulation = make_unique<car::Simulation>();
            VehicleState().vehicle_simulation->set_telemetry_path("car_telemetry_" + to_string(GetEntity()->GetObjectId()) + ".csv");
        }
        return VehicleState().vehicle_simulation.get();
    }

    uint32_t CarPhysics::GetVehicleCollisionGroup() const
    {
        return VehicleState().vehicle_simulation ? VehicleState().vehicle_simulation->multibody_collision_group() : 0;
    }

    void CarPhysics::Tick(bool is_playing)
    {
        CountWorldWork(WorldWork::physics_vehicle_updates);
        if (!VehicleState().vehicle_simulation_active || !GetVehicleSimulation() || !GetVehicleSimulation()->get_body())
        {
            return;
        }

        PxRigidActor* actor = GetVehicleSimulation()->get_body();

        if (is_playing)
        {
            // driven from PhysicsWorld's substep callback so forces stay in lockstep with the integration, otherwise the chassis wobbles

            // get the raw physx pose, this is the same pose used by the wheel debug shapes
            Vector3 physics_pos;
            Quaternion physics_rot;
            from_px_transform(actor->getGlobalPose(), physics_pos, physics_rot);

            // extrapolate the leftover time so the chassis stays smooth between fixed steps, bounded by one physx step
            PxRigidDynamic* dynamic         = actor->is<PxRigidDynamic>();
            Vector3 physics_vel             = dynamic ? from_px_vec3(dynamic->getLinearVelocity())  : Vector3::Zero;
            Vector3 physics_ang_vel         = dynamic ? from_px_vec3(dynamic->getAngularVelocity()) : Vector3::Zero;
            float leftover_time             = PhysicsWorld::GetInterpolationAlpha() * PhysicsWorld::GetFixedTimeStep();
            Vector3 render_pos              = physics_pos + physics_vel * leftover_time;
            Quaternion render_rot           = physics_rot;
            float ang_speed                 = physics_ang_vel.Length();
            if (ang_speed > 0.001f)
            {
                Vector3 axis         = physics_ang_vel / ang_speed;
                float angle          = ang_speed * leftover_time;
                Quaternion delta_rot = Quaternion::FromAxisAngle(axis, angle);
                render_rot           = delta_rot * physics_rot;
                render_rot.Normalize();
            }
            VehicleState().vehicle_physics_position = physics_pos;
            VehicleState().vehicle_physics_rotation = physics_rot;
            VehicleState().vehicle_render_position  = render_pos;
            VehicleState().vehicle_render_rotation  = render_rot;
            GetEntity()->SetPositionAndRotation(render_pos, render_rot);

            // update wheel visuals
            if (VehicleState().vehicle_sim_mode == VehicleSimMode::Cheap)
            {
                UpdateCheapWheelTransforms();
            }
            else
            {
                UpdateWheelTransforms();
            }

            // tick the car (input, camera, sounds, telemetry)
            if (VehicleState().car)
            {
                VehicleState().car->Tick();
            }
        }
        else
        {
            for (int i = 0; i < 4; ++i)
                if (VehicleState().tire_visuals[i]) UpdateTireDeformation(i, false);

            // editor mode: sync entity -> physx, reset velocities
            VehicleState().wheel_offsets_synced      = false;

            VehicleState().vehicle_physics_position  = GetEntity()->GetPosition();
            VehicleState().vehicle_physics_rotation  = GetEntity()->GetRotation();
            VehicleState().vehicle_render_position   = VehicleState().vehicle_physics_position;
            VehicleState().vehicle_render_rotation   = VehicleState().vehicle_physics_rotation;

            actor->setGlobalPose(to_px_transform(GetEntity()->GetPosition(), GetEntity()->GetRotation()));

            if (PxRigidDynamic* dynamic = actor->is<PxRigidDynamic>())
            {
                dynamic->setLinearVelocity(PxVec3(0, 0, 0));
                dynamic->setAngularVelocity(PxVec3(0, 0, 0));
            }

            // still tick so play stop can restore skeleton body visibility and m_was_playing
            if (VehicleState().car)
            {
                VehicleState().car->Tick();
            }
        }
    }

    void CarPhysics::TickVehicleSubstep(float dt)
    {
        if (!VehicleState().vehicle_simulation_active || !GetVehicleSimulation() || !GetVehicleSimulation()->get_body())
        {
            return;
        }

        // Derive parking from ownership every physics step, including spawn,
        // reset and driver exit. Traffic and external controllers own their inputs.
        VehicleState().vehicle_simulation->set_parked(VehicleState().car && !VehicleState().car->IsOccupied()
            && !VehicleState().car->IsExternallyControlled() && !VehicleState().vehicle_simulation->dyno.mounted);

        // explicit spring, damper and tire forces held over several physx steps go unstable,
        // so a reduced rate only applies to the kinematic cheap mode
        if (VehicleState().vehicle_simulation_interval > 0.0f && VehicleState().vehicle_sim_mode == VehicleSimMode::Cheap)
        {
            VehicleState().vehicle_simulation_accumulator += dt;
            if (VehicleState().vehicle_simulation_accumulator + 0.000001f < VehicleState().vehicle_simulation_interval)
            {
                return;
            }
            dt = VehicleState().vehicle_simulation_accumulator;
            VehicleState().vehicle_simulation_accumulator = 0.0f;
            VehicleState().vehicle_simulation->clear_force_accumulators();
        }

        if (VehicleState().vehicle_sim_mode == VehicleSimMode::Cheap)
        {
            TickVehicleCheapSubstep(dt);
            return;
        }

        // sync wheel offsets once at start of play
        if (!VehicleState().wheel_offsets_synced)
        {
            SyncWheelOffsetsFromEntities();
            VehicleState().wheel_offsets_synced = true;
        }

        // Classify each contact before its force is evaluated in this substep.
        VehicleState().vehicle_simulation->surface_resolver = classify_ground_actor;
        VehicleState().vehicle_simulation->water_resolver   = resolve_water_depth;
        const auto& environment = World::GetEnvironment();
        if (!VehicleState().vehicle_simulation->environment_enabled)
        {
            for (int i = 0; i < 4; ++i)
            {
                auto& wheel = VehicleState().vehicle_simulation->get_wheel_state(i);
                wheel.thermal.core = environment.air_temperature;
                for (float& zone : wheel.thermal.surface) zone = environment.air_temperature;
                wheel.brake_temp = environment.air_temperature;
            }
        }
        VehicleState().vehicle_simulation->environment_enabled = true;
        VehicleState().vehicle_simulation->ambient_temperature = environment.air_temperature;
        VehicleState().vehicle_simulation->road_temperature = environment.road_temperature;
        VehicleState().vehicle_simulation->ambient_pressure = environment.pressure;
        VehicleState().vehicle_simulation->air_density = environment.air_density;
        const auto wind = World::SampleWind(GetEntity()->GetPosition(), static_cast<float>(Timer::GetTimeSec()));
        VehicleState().vehicle_simulation->wind_velocity = PxVec3(wind.x, wind.y, wind.z);
        VehicleState().vehicle_simulation->tick(dt);
    }

    void CarPhysics::TickVehicleCheapSubstep(float dt)
    {
        PxRigidDynamic* body = VehicleState().vehicle_simulation->get_body();
        if (!body || dt <= 0.0f)
        {
            return;
        }

        // pedals land in input_target, cheap mode still needs the smooth copy into input
        VehicleState().vehicle_simulation->update_input(dt);

        // cheap cars are held at ride height, gravity would sink the chassis hull into the road
        body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);

        const car::car_preset& spec = VehicleState().vehicle_simulation->get_spec();
        const float wheelbase = std::max(spec.wheelbase > 0.0f ? spec.wheelbase : 2.7f, 1.5f);
        const float max_steer = std::clamp(
            spec.max_steer_angle > 0.0f ? spec.max_steer_angle : 0.6f,
            0.25f,
            0.9f
        );
        const float wheel_radius = std::max(
            (spec.front_wheel_radius + spec.rear_wheel_radius) * 0.5f,
            0.2f
        );
        // body origin sits wheel radius plus suspension height above the road, same as full sim spawn
        const float ride_height = wheel_radius + std::max(VehicleState().vehicle_simulation->get_config().suspension_height, 0.1f);
        const float accel = 8.0f;
        const float brake_decel = 14.0f;
        const float lateral_damp = 10.0f;
        const float drag = 0.35f;

        Vector3 position;
        Quaternion rotation;
        from_px_transform(body->getGlobalPose(), position, rotation);

        // snap height to the road so wheels sit on the surface
        Vector3 hit_position;
        const Vector3 ray_origin = position + Vector3::Up * 3.0f;
        if (VehicleState().vehicle_road_surface)
        {
            // Road traffic uses the authored surface even outside streamed collision.
            // Keep chassis pitch/roll aligned to the grade so contact impulses cannot
            // tip the cheap suspension-less body and wedge its nose into the asphalt.
            const Vector3 normal = VehicleState().vehicle_road_normal;
            Vector3 heading = rotation * Vector3::Forward;
            heading.y = -(normal.x * heading.x + normal.z * heading.z) / std::max(normal.y, 0.1f);
            rotation = Quaternion::Lerp(Quaternion::FromLookRotation(heading.Normalized(), normal),
                Quaternion::FromLookRotation(VehicleState().vehicle_road_tangent, normal), std::min(dt * 8.0f, 1.0f));
            const Vector3 offset = position - VehicleState().vehicle_road_position;
            position.y = VehicleState().vehicle_road_position.y - (normal.x * offset.x + normal.z * offset.z) / std::max(normal.y, 0.1f) + ride_height;
            // Overlapping graded approaches can sit slightly above the route's
            // centre line. Support the whole chassis, not just its midpoint, so
            // its bumper clears the next deck before reaching the seam.
            Vector3 planar_heading(heading.x, 0.0f, heading.z);
            planar_heading.Normalize();
            const float route_ride_y = position.y;
            const Vector3 across(planar_heading.z, 0.0f, -planar_heading.x);
            for (float along : {-1.0f, 0.0f, 1.0f})
            {
                for (float side : {-1.0f, 0.0f, 1.0f})
                {
                    const Vector3 probe_offset = planar_heading * (along * (wheelbase * 0.5f + 1.5f))
                        + across * (side * std::max(spec.width * 0.5f, 1.1f));
                    Vector3 support;
                    if (PhysicsWorld::RaycastStatic(position + probe_offset + Vector3::Up, Vector3::Down, 3.0f, support))
                    {
                        const float support_y = support.y + (normal.x * probe_offset.x + normal.z * probe_offset.z) / std::max(normal.y, 0.1f) + ride_height;
                        if (support_y - position.y < 0.75f) position.y = std::max(position.y, support_y);
                    }
                }
            }
            // Allow the collision hull a little suspension travel over a raised
            // seam; the visual wheel radius is not the hull's lowest corner.
            if (position.y > route_ride_y + 0.02f) position.y += 0.2f;
            body->setGlobalPose(to_px_transform(position, rotation));
        }
        else if (PhysicsWorld::RaycastStatic(ray_origin, Vector3::Down, 8.0f, hit_position))
        {
            position.y = hit_position.y + ride_height;
            body->setGlobalPose(
                to_px_transform(position, rotation)
            );
        }

        if (VehicleState().vehicle_simulation->is_parked())
        {
            body->setLinearVelocity(PxVec3(0.0f));
            body->setAngularVelocity(PxVec3(0.0f));
            VehicleState().cheap_steer_angle = 0.0f;
            return;
        }

        Vector3 forward = rotation * Vector3::Forward;
        forward.y = 0.0f;
        if (forward.LengthSquared() > 0.0001f)
        {
            forward.Normalize();
        }
        else
        {
            forward = Vector3::Forward;
        }
        Vector3 right = rotation * Vector3::Right;
        right.y = 0.0f;
        if (right.LengthSquared() > 0.0001f)
        {
            right.Normalize();
        }
        else
        {
            right = Vector3::Right;
        }

        PxVec3 px_vel = body->getLinearVelocity();
        Vector3 velocity(px_vel.x, 0.0f, px_vel.z);
        const float forward_speed = Vector3::Dot(velocity, forward);
        const float lateral_speed = Vector3::Dot(velocity, right);

        const float throttle = std::clamp(VehicleState().vehicle_simulation->get_throttle(), 0.0f, 1.0f);
        const float brake = std::clamp(VehicleState().vehicle_simulation->get_brake(), 0.0f, 1.0f);
        const float steer = std::clamp(VehicleState().vehicle_simulation->get_steering(), -1.0f, 1.0f);
        const float handbrake = std::clamp(VehicleState().vehicle_simulation->get_handbrake(), 0.0f, 1.0f);

        float drive = throttle * accel;
        if (fabsf(forward_speed) > 0.5f)
        {
            drive -= brake * brake_decel * (forward_speed >= 0.0f ? 1.0f : -1.0f);
        }
        else if (brake > throttle && VehicleState().vehicle_brake_reverse_enabled)
        {
            // arcade reverse when nearly stopped
            drive = -brake * accel * 0.65f;
        }
        drive -= handbrake * brake_decel * (forward_speed >= 0.0f ? 1.0f : -1.0f) * 0.75f;

        Vector3 planar_velocity = forward * forward_speed + right * lateral_speed;
        planar_velocity += forward * (drive * dt);
        planar_velocity -= right * lateral_speed * std::clamp(lateral_damp * dt, 0.0f, 1.0f);
        planar_velocity *= std::max(1.0f - drag * dt, 0.0f);
        velocity.x = planar_velocity.x;
        velocity.y = 0.0f;
        velocity.z = planar_velocity.z;

        const float signed_speed = Vector3::Dot(velocity, forward);
        const float steer_speed_scale = std::clamp(fabsf(signed_speed) / 12.0f, 0.0f, 1.0f);
        VehicleState().cheap_steer_angle = steer * max_steer * (VehicleState().vehicle_full_steering_lock ? 1.0f : 0.35f + 0.65f * steer_speed_scale);
        float yaw_rate = 0.0f;
        if (fabsf(signed_speed) > 0.2f)
        {
            yaw_rate = (signed_speed / wheelbase) * tanf(VehicleState().cheap_steer_angle);
        }

        if (VehicleState().vehicle_road_surface)
        {
            // Ambient traffic follows the lane even through a tight elbow. Keep
            // dynamic collision response, but remove accumulated lateral drift
            // instead of letting the suspension-less bicycle cut road corners.
            Vector3 lane_forward(VehicleState().vehicle_road_tangent.x, 0.0f, VehicleState().vehicle_road_tangent.z);
            lane_forward.Normalize();
            Vector3 error = VehicleState().vehicle_road_position - position;
            error.y = 0.0f;
            Vector3 correction = (error - lane_forward * Vector3::Dot(error, lane_forward)) * 6.0f;
            if (correction.LengthSquared() > 4.0f) correction = correction.Normalized() * 2.0f;
            velocity = lane_forward * std::max(signed_speed, 0.0f) + correction;
            yaw_rate = 0.0f; // orientation follows the same graded lane above
        }
        body->setLinearVelocity(PxVec3(velocity.x, 0.0f, velocity.z));
        body->setAngularVelocity(PxVec3(0.0f, yaw_rate, 0.0f));
        body->wakeUp();

        const float roll_speed = signed_speed / std::max(wheel_radius, 0.05f);
        VehicleState().cheap_wheel_roll += roll_speed * dt;
    }

    void CarPhysics::SetVehiclePreset(const car::car_preset& preset)
    {
        EnsureVehicleSimulation()->load_car(preset);
    }

    void CarPhysics::SetVehicleSimulationActive(bool active)
    {
        if (physics.GetBodyType() != BodyType::Custom || VehicleState().vehicle_simulation_active == active)
        {
            return;
        }

        car::Simulation* simulation = EnsureVehicleSimulation();
        if (!active)
        {
            simulation->set_force_retention(false);
        }
        simulation->set_simulation_enabled(active);
        if (active)
        {
            simulation->set_force_retention(VehicleState().vehicle_simulation_interval > 0.0f && VehicleState().vehicle_sim_mode == VehicleSimMode::Cheap);
            VehicleState().wheel_offsets_synced = false;

            if (VehicleState().vehicle_sim_mode == VehicleSimMode::Cheap)
            {
                simulation->set_mechanism_simulation_enabled(false);
            }
        }
        VehicleState().vehicle_simulation_accumulator = 0.0f;
        VehicleState().vehicle_simulation_active = active;
    }

    void CarPhysics::SetVehicleSimMode(VehicleSimMode mode)
    {
        if (VehicleState().vehicle_sim_mode == mode)
        {
            return;
        }

        // body not ready yet, remember the mode for create
        if (physics.GetBodyType() != BodyType::Custom || !VehicleState().vehicle_simulation || !VehicleState().vehicle_simulation->get_body())
        {
            VehicleState().vehicle_sim_mode = mode;
            return;
        }

        PxRigidDynamic* body = VehicleState().vehicle_simulation->get_body();
        Vector3 position;
        Quaternion rotation;
        from_px_transform(body->getGlobalPose(), position, rotation);
        const Vector3 linear_velocity = physics.GetLinearVelocity();
        Vector3 angular_velocity = Vector3::Zero;
        {
            const PxVec3 px_ang = body->getAngularVelocity();
            angular_velocity = Vector3(px_ang.x, px_ang.y, px_ang.z);
        }

        if (mode == VehicleSimMode::Cheap)
        {
            CaptureCheapWheelRestPoses();
            // clear while mechanisms are still simulated, then disable them
            VehicleState().vehicle_simulation->clear_force_accumulators();
            VehicleState().vehicle_simulation->set_mechanism_simulation_enabled(false);
            body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
            VehicleState().cheap_wheel_roll = 0.0f;
            VehicleState().cheap_steer_angle = 0.0f;
            this->physics.SetBodyTransform(position, rotation, false);
            physics.SetLinearVelocity(linear_velocity);
            physics.SetAngularVelocity(angular_velocity);
        }
        else
        {
            // cheap mode leaves mechanism actors frozen at old world poses while the
            // chassis keeps moving, re enabling them without a rebuild detonates constraints
            PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
            PxScene* scene = static_cast<PxScene*>(PhysicsWorld::GetScene());
            if (!VehicleState().vehicle_simulation->ensure_multibody(physics, scene))
            {
                SP_LOG_ERROR("failed to build suspension assembly for full sim mode");
                return;
            }
            body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, false);
            VehicleState().wheel_offsets_synced = false;
            // rebuild at the current chassis pose with zeroed velocities first
            this->physics.SetBodyTransform(position, rotation, true);
            VehicleState().vehicle_simulation->set_mechanism_simulation_enabled(true);
            // leave velocities zero, restoring cheap chassis speed onto a fresh assembly launches the car
        }

        VehicleState().vehicle_sim_mode = mode;
        VehicleState().vehicle_simulation_accumulator = 0.0f;
        VehicleState().vehicle_simulation->set_force_retention(VehicleState().vehicle_simulation_interval > 0.0f && mode == VehicleSimMode::Cheap);

    }

    void CarPhysics::SetVehicleSimulationFrequency(float frequency)
    {
        car::Simulation* simulation = EnsureVehicleSimulation();
        const float fixed_time_step = PhysicsWorld::GetFixedTimeStep();
        const float requested_interval = frequency > 0.0f ? 1.0f / frequency : 0.0f;
        VehicleState().vehicle_simulation_interval = requested_interval > fixed_time_step ? requested_interval : 0.0f;
        VehicleState().vehicle_simulation_accumulator = 0.0f;
        simulation->clear_force_accumulators();
        simulation->set_force_retention(VehicleState().vehicle_simulation_interval > 0.0f && VehicleState().vehicle_sim_mode == VehicleSimMode::Cheap);
    }

    void CarPhysics::SetVehicleThrottle(float value)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return;
        }

        VehicleState().vehicle_simulation->set_throttle(value);
    }

    void CarPhysics::SetVehicleBrake(float value)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return;
        }

        VehicleState().vehicle_simulation->set_brake(value);
    }

    void CarPhysics::SetVehicleSteering(float value)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return;
        }

        VehicleState().vehicle_simulation->set_steering(value);
    }

    void CarPhysics::SetVehicleHandbrake(float value)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return;
        }

        VehicleState().vehicle_simulation->set_handbrake(value);
    }

    void CarPhysics::SetWheelEntity(WheelIndex wheel, Entity* entity)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            SP_LOG_WARNING("SetWheelEntity only works with Vehicle body type");
            return;
        }

        int index = static_cast<int>(wheel);
        if (index >= 0 && index < static_cast<int>(WheelIndex::Count))
        {
            if (VehicleState().wheel_entities[index] != entity)
            {
                VehicleState().tire_visuals[index].reset();
                VehicleState().cheap_wheel_rest_captured[index] = false;
            }
            VehicleState().wheel_entities[index] = entity;
            VehicleState().wheel_calipers[index] = entity ? entity->GetChildByName("brake_caliper") : nullptr;

            // sync the physics wheel offset from the entity position
            if (entity)
            {
                if (!VehicleState().cheap_wheel_rest_captured[index])
                {
                    VehicleState().cheap_wheel_local_pos[index] = entity->GetPositionLocal();
                    VehicleState().cheap_wheel_local_rot[index] = entity->GetRotationLocal();
                    VehicleState().cheap_wheel_rest_captured[index] = true;
                }

                Entity* vehicle_entity = GetEntity();
                if (vehicle_entity)
                {
                    // transform wheel world position to vehicle-local space
                    Vector3 vehicle_world_pos = vehicle_entity->GetPosition();
                    Quaternion vehicle_world_rot = vehicle_entity->GetRotation();
                    Quaternion vehicle_world_rot_inv = vehicle_world_rot.Conjugate();

                    // try to get the actual mesh center from the render's bounding box
                    Vector3 wheel_world_pos = entity->GetPosition();
                    Render* render = entity->GetComponent<Render>();
                    if (render)
                    {
                        render->Tick();
                        BoundingBox aabb = render->GetBoundingBox();
                        wheel_world_pos = aabb.GetCenter();
                        Vector3 center_offset = entity->GetRotation().Conjugate() * (wheel_world_pos - entity->GetPosition());
                        VehicleState().wheel_mesh_center_offsets[index] = center_offset.IsFinite() ? center_offset : Vector3::Zero;
                    }

                    Vector3 local_pos = vehicle_world_rot_inv * (wheel_world_pos - vehicle_world_pos);
                    VehicleState().vehicle_simulation->set_wheel_offset(index, local_pos.x, local_pos.z);
                }
            }
        }
    }

    Entity* CarPhysics::GetWheelEntity(WheelIndex wheel) const
    {
        int index = static_cast<int>(wheel);
        if (index >= 0 && index < static_cast<int>(WheelIndex::Count))
        {
            return VehicleState().wheel_entities[index];
        }
        return nullptr;
    }

    void CarPhysics::SetChassisEntity(Entity* entity, const vector<Entity*>& entities_to_exclude)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            SP_LOG_WARNING("SetChassisEntity only works with Vehicle body type");
            return;
        }

        VehicleState().chassis_entity = entity;
        VehicleState().chassis_entities_to_exclude = entities_to_exclude;
        if (entity)
        {
            // store base position so we can offset from it
            VehicleState().chassis_base_pos = entity->GetPositionLocal();
            SP_LOG_INFO("SetChassisEntity: chassis set to '%s', base_pos=(%.2f, %.2f, %.2f), excluding %zu entities",
                entity->GetObjectName().c_str(), VehicleState().chassis_base_pos.x, VehicleState().chassis_base_pos.y, VehicleState().chassis_base_pos.z, entities_to_exclude.size());

            BuildChassisConvexShapes(entity, entities_to_exclude);

            // re-tag after shape replacement
            if (PxRigidDynamic* body = VehicleState().vehicle_simulation->get_body())
            {
                tag_actor_shapes(body, 2, VehicleState().vehicle_simulation->multibody_collision_group());
            }
        }
        else
        {
            SP_LOG_WARNING("SetChassisEntity: entity is null!");
        }
    }

    void CarPhysics::BuildChassisConvexShapes(Entity* chassis_entity, const vector<Entity*>& entities_to_exclude)
    {
        SP_PROFILE_CPU();
        if (!VehicleState().vehicle_simulation->get_body() || !chassis_entity)
        {
            return;
        }

        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        if (!physics)
        {
            return;
        }

        // setting the chassis mutates shapes on a body that is already attached to the
        // scene, async load runs this on worker threads so serialize against other physx writes
        lock_guard<recursive_mutex> physx_lock(PhysicsWorld::GetMutex());

        // collect all entities with render components in the hierarchy
        vector<Entity*> mesh_entities;
        mesh_entities.push_back(chassis_entity);
        chassis_entity->GetDescendants(&mesh_entities);

        // helper to check if an entity should be excluded
        auto should_exclude = [&entities_to_exclude](Entity* ent) -> bool
        {
            for (Entity* excluded : entities_to_exclude)
            {
                if (ent == excluded)
                {
                    return true;
                }

                // also check if this entity is a descendant of an excluded entity
                Entity* parent = ent->GetParent();
                while (parent)
                {
                    if (parent == excluded)
                    {
                        return true;
                    }
                    parent = parent->GetParent();
                }
            }
            return false;
        };

        // filter to only entities with render components, excluding specified entities
        vector<pair<Entity*, Render*>> render_entities;
        for (Entity* ent : mesh_entities)
        {
            // skip inactive entities and excluded entities
            if (!ent->GetActive())
            {
                continue;
            }
            if (should_exclude(ent))
            {
                continue;
            }

            if (Render* render = ent->GetComponent<Render>())
            {
                render_entities.push_back({ent, render});
            }
        }

        if (render_entities.empty())
        {
            SP_LOG_WARNING("No render entities found in chassis hierarchy (after exclusions)");
            return;
        }

        SP_LOG_INFO(
            "BuildChassisConvexShapes: collecting verts from %zu mesh entities (excluded %zu)",
            render_entities.size(),
            entities_to_exclude.size()
        );

        const car::config& car_cfg = VehicleState().vehicle_simulation->get_config();
        const float wheel_well_min_y = -car_cfg.suspension_height;
        const float body_floor_min_y = -(car_cfg.suspension_height + 0.18f);
        auto is_in_wheel_well = [&](const Vector3& v)
        {
            for (int i = 0; i < car::wheel_count; ++i)
            {
                const PxVec3 offset = VehicleState().vehicle_simulation->get_wheel_offset(i);
                if (fabsf(v.x - offset.x) <= car_cfg.wheel_width_for(i) * 1.35f &&
                    fabsf(v.z - offset.z) <= car_cfg.wheel_radius_for(i) * 1.45f)
                    return true;
            }
            return false;
        };

        vector<car::chassis_collision::triangle> surface;
        size_t source_vertex_count = 0;
        // Matrix decomposition introduces tiny scale differences with heading.
        // Remove that roundoff so identical traffic cars share the same fit.
        Vector3 actor_scale = GetEntity()->GetScale();
        actor_scale.x = roundf(actor_scale.x * 1000000.0f) / 1000000.0f;
        actor_scale.y = roundf(actor_scale.y * 1000000.0f) / 1000000.0f;
        actor_scale.z = roundf(actor_scale.z * 1000000.0f) / 1000000.0f;
        SP_PROFILE_CPU_START("chassis_collect_geometry");
        vector<uint32_t> indices;
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<PxVec3> points;
        vector<uint8_t> point_valid;
        for (const auto& [ent, render] : render_entities)
        {
            indices.clear();
            vertices.clear();
            render->GetGeometry(&indices, &vertices);
            if (vertices.empty()) continue;

            // Compose in body space. Going through world space loses precision far
            // from the origin and the former path applied chassis scale twice.
            Matrix mesh_to_body = Matrix::Identity;
            Entity* ancestor = ent;
            while (ancestor && ancestor != GetEntity())
            {
                mesh_to_body = mesh_to_body * ancestor->GetLocalMatrix();
                ancestor = ancestor->GetParent();
            }
            if (!ancestor) continue;
            mesh_to_body = mesh_to_body * Matrix(Vector3::Zero, Quaternion::Identity, actor_scale);

            points.clear();
            point_valid.clear();
            points.reserve(vertices.size());
            point_valid.reserve(vertices.size());
            for (const auto& vertex : vertices)
            {
                Vector3 v = Vector3(vertex.pos[0], vertex.pos[1], vertex.pos[2]) * mesh_to_body;
                // Retain existing suspension/curb clearance independently of hull fitting.
                if (v.y < wheel_well_min_y && is_in_wheel_well(v)) v.y = wheel_well_min_y;
                else if (v.y < body_floor_min_y) v.y = body_floor_min_y;
                points.emplace_back(v.x, v.y, v.z);
                // Test each transformed vertex once. PxVec3::isFinite invokes
                // the Windows double classifier for each coordinate; repeating
                // that for every indexed corner dominated traffic creation.
                point_valid.push_back(v.IsFinite());
            }
            source_vertex_count += points.size();
            for (size_t i = 0; i + 2 < indices.size(); i += 3)
            {
                const uint32_t a = indices[i], b = indices[i + 1], c = indices[i + 2];
                if (a >= points.size() || b >= points.size() || c >= points.size()) continue;
                if (!point_valid[a] || !point_valid[b] || !point_valid[c]) continue;
                surface.push_back({points[a], points[b], points[c]});
            }
        }
        SP_PROFILE_CPU_END();

        PxCookingParams params(physics->getTolerancesScale());
        params.convexMeshCookingType = PxConvexMeshCookingType::eQUICKHULL;
        params.gaussMapLimit = 32;
        PxInsertionCallback* insertion = PxGetStandaloneInsertionCallback();
        if (!insertion || surface.empty()) return;

        static car::chassis_collision::cache chassis_cache; // protected by physx_lock
        auto hulls = [&]()
        {
            ScopedTimeBlock block("chassis_fit_or_cache");
            return chassis_cache.get(surface, params, *insertion, World::GetResourceDirectory());
        }();
        if (hulls.empty())
        {
            SP_LOG_WARNING("BuildChassisConvexShapes: fitting failed, retaining previous chassis");
            return;
        }
        vector<PxConvexMesh*> meshes;
        vector<PxVec3> aero_vertices;
        size_t hull_vertex_count = 0;
        for (const auto& hull : hulls)
        {
            meshes.push_back(hull.get());
            hull_vertex_count += hull->getNbVertices();
            aero_vertices.insert(aero_vertices.end(), hull->getVertices(), hull->getVertices() + hull->getNbVertices());
        }
        const bool attached = [&]()
        {
            ScopedTimeBlock block("chassis_attach");
            return VehicleState().vehicle_simulation->set_chassis(meshes, aero_vertices, physics);
        }();
        if (!attached)
        {
            SP_LOG_ERROR("Failed to set chassis compound");
            return;
        }
        SP_LOG_INFO("BuildChassisConvexShapes: %zu adaptive hulls, %zu collision vertices from %zu source vertices",
            hulls.size(), hull_vertex_count, source_vertex_count);
    }

    void CarPhysics::SetWheelRadius(float radius)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            SP_LOG_WARNING("SetWheelRadius only works with Vehicle body type");
            return;
        }

        // a bad radius would divide through every tire force, so reject it here and continue with a sane default
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            SP_LOG_WARNING("SetWheelRadius: refusing non finite or non positive radius %.3f, keeping %.3f", radius, VehicleState().wheel_radius);
            return;
        }

        radius = std::max(radius, 0.05f);

        VehicleState().wheel_radius = radius;

        car::config& config = VehicleState().vehicle_simulation->get_config();
        config.front_wheel_radius = radius;
        config.rear_wheel_radius  = radius;

        // recalculate and update body height based on actual wheel radius
        if (PxRigidDynamic* body = VehicleState().vehicle_simulation->get_body())
        {
            // the body is in the scene, async load reaches this from car prefab workers
            lock_guard<recursive_mutex> physx_lock(PhysicsWorld::GetMutex());

            // spawn at the resting height, radius + suspension_height - sag, so the car does not bounce through its travel
            float front_mass_per_wheel = VehicleState().vehicle_simulation->chassis_mass() * VehicleState().vehicle_simulation->get_weight_distribution_front() * 0.5f;
            float front_omega = 2.0f * math::pi * VehicleState().vehicle_simulation->get_spec().front_spring_freq;
            float front_stiffness = front_mass_per_wheel * front_omega * front_omega;
            float front_load = front_mass_per_wheel * 9.81f;
            float expected_sag = std::clamp(front_load / front_stiffness, 0.0f, config.suspension_travel * 0.8f);
            const float correct_body_height = radius + config.suspension_height - expected_sag + 0.02f;

            // update body position with correct height
            PxTransform pose = body->getGlobalPose();
            pose.p.y = correct_body_height;
            body->setGlobalPose(pose);

            if (!VehicleState().vehicle_simulation->rebuild_vehicle_geometry())
            {
                SP_LOG_ERROR("failed to rebuild suspension after wheel radius change");
            }

            SP_LOG_INFO("SetWheelRadius: adjusted body height to %.3f for radius %.3f", correct_body_height, radius);
        }
        else
        {
            VehicleState().vehicle_simulation->rebuild_vehicle_geometry();
        }

        SP_LOG_INFO("SetWheelRadius: wheel radius set to %.3f", radius);
    }

    void CarPhysics::ComputeWheelRadiusFromEntity(Entity* wheel_entity)
    {
        if (!wheel_entity)
        {
            SP_LOG_WARNING("ComputeWheelRadiusFromEntity: wheel_entity is null");
            return;
        }

        // get the render component to access the bounding box
        Render* render = wheel_entity->GetComponent<Render>();
        if (!render)
        {
            SP_LOG_WARNING("ComputeWheelRadiusFromEntity: wheel entity has no Render component");
            return;
        }

        // force bounding box update to reflect current entity transform (including scale)
        // this is needed because the bounding box is lazily updated during Tick()
        render->Tick();

        // get the aabb - this is in world space (transformed by entity matrix including scale)
        BoundingBox aabb = render->GetBoundingBox();
        Vector3 extents = aabb.GetExtents(); // half-sizes, already scaled

        // a default constructed bbox has m_min = inf and m_max = -inf which produces a non finite
        // center and a negative infinity extent. refuse to feed that into SetWheelRadius
        Vector3 aabb_center = aabb.GetCenter();
        if (extents.IsNaN() || aabb_center.IsNaN() || !std::isfinite(extents.x) || !std::isfinite(extents.y) || !std::isfinite(extents.z))
        {
            SP_LOG_WARNING("ComputeWheelRadiusFromEntity: non finite bbox on '%s', keeping previous radius %.3f",
                wheel_entity->GetObjectName().c_str(), VehicleState().wheel_radius);
            return;
        }

        // the wheel radius is the largest extent (wheels are usually symmetric)
        // for a wheel mesh, this gives us the actual visual radius
        float radius = max(max(extents.x, extents.y), extents.z);
        if (!std::isfinite(radius) || radius <= 0.0f)
        {
            SP_LOG_WARNING("ComputeWheelRadiusFromEntity: computed radius %.3f on '%s' is invalid, keeping previous radius %.3f",
                radius, wheel_entity->GetObjectName().c_str(), VehicleState().wheel_radius);
            return;
        }

        VehicleState().wheel_radius = radius;

        SP_LOG_INFO("ComputeWheelRadiusFromEntity: computed radius=%.3f from entity '%s' (extents: %.3f, %.3f, %.3f)",
            radius, wheel_entity->GetObjectName().c_str(), extents.x, extents.y, extents.z);
    }

    float CarPhysics::GetSuspensionHeight() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_config().suspension_height;
    }

    void CarPhysics::ScaleWheelEntityToDimensions(Entity* wheel_entity, float target_radius, float target_width)
    {
        if (!wheel_entity || !std::isfinite(target_radius) || !std::isfinite(target_width) || target_radius <= 0.0f || target_width <= 0.0f)
        {
            return;
        }

        Render* render = wheel_entity->GetComponent<Render>();
        if (!render)
        {
            return;
        }

        // scale is absolute and derives only from unscaled local mesh bounds
        Vector3 extents = render->GetBoundingBoxMesh().GetExtents();
        if (!extents.IsFinite() || extents.x <= 0.0001f || extents.y <= 0.0001f || extents.z <= 0.0001f)
        {
            return;
        }

        // Wheel assets use X for the axle; preserve the requested front/rear tire widths.
        const float measured_radius = max(extents.y, extents.z);
        const Vector3 scale(target_width / (extents.x * 2.0f), target_radius / measured_radius, target_radius / measured_radius);
        if (!scale.IsFinite() || scale.x <= 0.0f || scale.y <= 0.0f || scale.z <= 0.0f)
        {
            return;
        }

        wheel_entity->SetScaleLocal(scale);
        render->Tick();
    }

    float CarPhysics::GetVehicleThrottle() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_throttle();
    }

    float CarPhysics::GetVehicleBrake() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_brake();
    }

    float CarPhysics::GetVehicleSteering() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_steering();
    }

    float CarPhysics::GetVehicleHandbrake() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_handbrake();
    }

    bool CarPhysics::IsWheelGrounded(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->is_wheel_grounded(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelCompression(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_compression(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelSuspensionForce(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_suspension_force(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelSlipAngle(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_slip_angle(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelSlipRatio(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_slip_ratio(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelTireLoad(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_tire_load(static_cast<int>(wheel));
    }

    Vector3 CarPhysics::GetWheelContactPoint(WheelIndex wheel) const
    {
        int i = static_cast<int>(wheel);
        if (physics.GetBodyType() != BodyType::Custom || i < 0 || i >= car::wheel_count)
        {
            return Vector3::Zero;
        }
        return PhysicsWorld::ToWorldPosition(from_px_vec3(VehicleState().vehicle_simulation->get_wheel_state(i).contact_point));
    }

    Vector3 CarPhysics::GetWheelContactNormal(WheelIndex wheel) const
    {
        int i = static_cast<int>(wheel);
        if (physics.GetBodyType() != BodyType::Custom || i < 0 || i >= car::wheel_count)
        {
            return Vector3::Up;
        }
        return from_px_vec3(VehicleState().vehicle_simulation->get_wheel_state(i).contact_normal);
    }

    float CarPhysics::GetWheelSlipMagnitude(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        int i           = static_cast<int>(wheel);
        float slip_ratio = VehicleState().vehicle_simulation->get_wheel_slip_ratio(i);
        float slip_angle = VehicleState().vehicle_simulation->get_wheel_slip_angle(i);
        return sqrtf(slip_ratio * slip_ratio + slip_angle * slip_angle);
    }

    float CarPhysics::GetWheelWidth(WheelIndex wheel) const
    {
        int i = static_cast<int>(wheel);
        if (physics.GetBodyType() != BodyType::Custom || i < 0 || i >= car::wheel_count)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_config().wheel_width_for(i);
    }

    float CarPhysics::GetWheelFrictionUse(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_friction_use(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelPeakLateralForce(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_peak_lateral_force(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelPeakLongitudinalForce(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_peak_longitudinal_force(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelLateralForce(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_lateral_force(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelLongitudinalForce(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_longitudinal_force(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelAngularVelocity(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_angular_velocity(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelRPM(WheelIndex wheel) const
    {
        // convert angular velocity (rad/s) to RPM
        // rpm = (rad/s) * (60 / 2π) = (rad/s) * 9.5493
        float angular_vel = GetWheelAngularVelocity(wheel);
        return fabsf(angular_vel) * 9.5493f;
    }

    float CarPhysics::GetWheelTemperature(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_temperature(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelTempGripFactor(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 1.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_temp_grip_factor(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelBrakeTemp(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_brake_temp(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelBrakeEfficiency(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 1.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_brake_efficiency(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelSurfaceTemp(WheelIndex wheel, int zone) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_surface_temp(static_cast<int>(wheel), zone);
    }

    float CarPhysics::GetWheelCoreTemp(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_core_temp(static_cast<int>(wheel));
    }

    float CarPhysics::GetTirePressure() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_tire_pressure();
    }

    float CarPhysics::GetTirePressureOptimal() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_tire_pressure_optimal();
    }

    void CarPhysics::SetAbsEnabled(bool enabled)
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->set_abs_enabled(enabled);
        }
    }

    bool CarPhysics::GetAbsEnabled() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->get_abs_enabled();
    }

    bool CarPhysics::IsAbsActive(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->is_abs_active(static_cast<int>(wheel));
    }

    bool CarPhysics::IsAbsActiveAny() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->is_abs_active_any();
    }

    float CarPhysics::GetAbsPhase() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_abs_phase();
    }

    void CarPhysics::SetTcEnabled(bool enabled)
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->set_tc_enabled(enabled);
        }
    }

    bool CarPhysics::GetTcEnabled() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->get_tc_enabled();
    }

    bool CarPhysics::IsTcActive() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->is_tc_active();
    }

    float CarPhysics::GetTcReduction() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_tc_reduction();
    }

    bool CarPhysics::IsBurnoutActive() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->is_burnout_active();
    }

    void CarPhysics::SetTurboEnabled(bool enabled)
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->set_turbo_enabled(enabled);
        }
    }

    bool CarPhysics::GetTurboEnabled() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->get_turbo_enabled();
    }

    float CarPhysics::GetBoostPressure() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_boost_pressure();
    }

    float CarPhysics::GetBoostMaxPressure() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_boost_max_pressure();
    }

    void CarPhysics::SetDrsEnabled(bool enabled)
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->set_drs_enabled(enabled);
        }
    }

    bool CarPhysics::GetDrsEnabled() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->get_drs_enabled();
    }

    void CarPhysics::SetDrsActive(bool active)
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->set_drs_active(active);
        }
    }

    bool CarPhysics::GetDrsActive() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->get_drs_active();
    }

    void CarPhysics::SetDiffType(int type)
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->set_diff_type(type);
        }
    }

    int CarPhysics::GetDiffType() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 2;
        }
        return VehicleState().vehicle_simulation->get_diff_type();
    }

    const char* CarPhysics::GetDiffTypeName() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return "N/A";
        }
        return VehicleState().vehicle_simulation->get_diff_type_name();
    }

    float CarPhysics::GetWheelWear(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_wear(static_cast<int>(wheel));
    }

    float CarPhysics::GetWheelWearGripFactor(WheelIndex wheel) const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 1.0f;
        }
        return VehicleState().vehicle_simulation->get_wheel_wear_grip_factor(static_cast<int>(wheel));
    }

    void CarPhysics::ResetTireWear()
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->reset_tire_wear();
        }
    }

    void CarPhysics::SetManualTransmission(bool enabled)
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->set_manual_transmission(enabled);
        }
    }

    bool CarPhysics::GetManualTransmission() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->get_manual_transmission();
    }

    void CarPhysics::ShiftUp()
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->shift_up();
        }
    }

    void CarPhysics::ShiftDown()
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->shift_down();
        }
    }

    void CarPhysics::ShiftToNeutral()
    {
        if (physics.GetBodyType() == BodyType::Custom)
        {
            VehicleState().vehicle_simulation->shift_to_neutral();
        }
    }

    int CarPhysics::GetCurrentGear() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 1;
        } // neutral
        return VehicleState().vehicle_simulation->get_current_gear();
    }

    const char* CarPhysics::GetCurrentGearString() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return "N";
        }
        return VehicleState().vehicle_simulation->get_gear_string();
    }

    float CarPhysics::GetEngineRPM() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_current_engine_rpm();
    }

    float CarPhysics::GetEngineTorque() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_engine_torque_current();
    }

    float CarPhysics::GetMotorTorque() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_motor_torque();
    }

    float CarPhysics::GetIdleRPM() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_idle_rpm();
    }

    float CarPhysics::GetRedlineRPM() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return 0.0f;
        }
        return VehicleState().vehicle_simulation->get_redline_rpm();
    }

    bool CarPhysics::IsShifting() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return false;
        }
        return VehicleState().vehicle_simulation->get_is_shifting();
    }

    Vector3 CarPhysics::TransformVehiclePointToRender(const Vector3& point) const
    {
        return VehicleState().vehicle_render_position + VehicleState().vehicle_render_rotation * (VehicleState().vehicle_physics_rotation.Conjugate() * (point - VehicleState().vehicle_physics_position));
    }

    Quaternion CarPhysics::TransformVehicleRotationToRender(const Quaternion& rotation) const
    {
        Quaternion result = VehicleState().vehicle_render_rotation * VehicleState().vehicle_physics_rotation.Conjugate() * rotation;
        result.Normalize();
        return result;
    }

    void CarPhysics::SyncWheelOffsetsFromEntities()
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return;
        }

        Entity* vehicle_entity = GetEntity();
        if (!vehicle_entity)
        {
            return;
        }

        // get vehicle's world transform to convert wheel world positions to vehicle-local space
        Vector3 vehicle_world_pos = vehicle_entity->GetPosition();
        Quaternion vehicle_world_rot = vehicle_entity->GetRotation();
        Quaternion vehicle_world_rot_inv = vehicle_world_rot.Conjugate();

        for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
        {
            Entity* wheel_entity = VehicleState().wheel_entities[i];
            if (!wheel_entity)
            {
                continue;
            }

            // skip wheels with non finite world transforms, the bbox derived from such a transform
            // would otherwise crash the frustum culler assert when render->Tick runs below
            if (!wheel_entity->GetMatrix().IsFinite())
            {
                SP_LOG_WARNING("non finite world matrix on wheel '%s' in SyncWheelOffsetsFromEntities, skipping", wheel_entity->GetObjectName().c_str());
                continue;
            }

            // try to get the actual mesh center from the render's bounding box
            // this handles meshes where the origin is not at the geometric center
            Vector3 wheel_world_pos = wheel_entity->GetPosition();

            Render* render = wheel_entity->GetComponent<Render>();
            if (render)
            {
                render->Tick(); // ensure bounding box is up to date
                BoundingBox aabb = render->GetBoundingBox();
                Vector3 aabb_center = aabb.GetCenter();
                if (!aabb_center.IsNaN())
                {
                    wheel_world_pos = aabb_center;
                }
                else
                {
                    SP_LOG_WARNING("non finite bbox center on wheel '%s' in SyncWheelOffsetsFromEntities, using entity origin", wheel_entity->GetObjectName().c_str());
                }
            }

            // transform to vehicle-local space
            // this handles cases where wheel is a child of an intermediate entity (e.g. "model")
            Vector3 local_pos = vehicle_world_rot_inv * (wheel_world_pos - vehicle_world_pos);

            // update the physics wheel offset x and z to match the mesh position
            VehicleState().vehicle_simulation->set_wheel_offset(i, local_pos.x, local_pos.z);
        }

        if (VehicleState().vehicle_simulation->has_multibody() && !VehicleState().vehicle_simulation->rebuild_multibody())
        {
            SP_LOG_ERROR("failed to rebuild car suspension after wheel synchronization");
        }
    }

    void CarPhysics::SetCenterOfMassOffset(const Vector3& offset)
    {
        SetCenterOfMassOffset(offset.x, offset.y, offset.z);
    }

    void CarPhysics::SetCenterOfMassOffset(float x, float y, float z)
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            SP_LOG_WARNING("SetCenterOfMassOffset only works with Vehicle body type");
            return;
        }

        VehicleState().vehicle_simulation->set_center_of_mass(x, y, z);
    }

    Vector3 CarPhysics::GetCenterOfMassOffset() const
    {
        if (physics.GetBodyType() != BodyType::Custom)
        {
            return Vector3::Zero;
        }

        return Vector3(VehicleState().vehicle_simulation->get_center_of_mass_x(), VehicleState().vehicle_simulation->get_center_of_mass_y(), VehicleState().vehicle_simulation->get_center_of_mass_z());
    }

    void CarPhysics::UpdateWheelTransforms()
    {
        if (physics.GetBodyType() != BodyType::Custom || !Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        TireDeformationBatch batch;
        std::array<TireDeformationBatch, 4> wheel_batches;
        std::array<bool, 4> valid_wheels{};
        batch.skin.reserve(256);
        batch.uploads.reserve(16);
        for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
        {
            Entity* wheel_entity = VehicleState().wheel_entities[i];
            if (!wheel_entity)
            {
                continue;
            }

            bool is_right_wheel = (i == static_cast<int>(WheelIndex::FrontRight) || i == static_cast<int>(WheelIndex::RearRight));
            PxRigidDynamic* wheel_actor = VehicleState().vehicle_simulation->get_multibody_state().corners[i].wheel_body;
            if (!wheel_actor)
            {
                continue;
            }

            Vector3 wheel_position;
            Quaternion wheel_rotation;
            from_px_transform(wheel_actor->getGlobalPose(), wheel_position, wheel_rotation);
            if (!wheel_position.IsFinite() || !wheel_rotation.IsFinite())
            {
                continue;
            }

            if (is_right_wheel)
            {
                wheel_rotation *= Quaternion::FromAxisAngle(Vector3::Up, math::pi);
            }
            wheel_position = TransformVehiclePointToRender(wheel_position);
            wheel_rotation = TransformVehicleRotationToRender(wheel_rotation);
            wheel_position -= wheel_rotation * VehicleState().wheel_mesh_center_offsets[i];
            wheel_entity->SetPositionAndRotation(wheel_position, wheel_rotation);

            valid_wheels[i] = true;

            // The caliper shares the axle origin and scale, but follows the upright,
            // not the spinning wheel body. This also retains steering and camber.
            if (Entity* caliper = VehicleState().wheel_calipers[i])
            {
                if (PxRigidDynamic* upright = VehicleState().vehicle_simulation->get_multibody_state().corners[i].upright)
                {
                    Vector3 upright_position;
                    Quaternion upright_rotation;
                    from_px_transform(upright->getGlobalPose(), upright_position, upright_rotation);
                    if (upright_rotation.IsFinite())
                    {
                        if (is_right_wheel)
                            upright_rotation *= Quaternion::FromAxisAngle(Vector3::Up, math::pi);
                        caliper->SetRotation(TransformVehicleRotationToRender(upright_rotation));
                    }
                }
            }
        }
        SP_PROFILE_CPU_START("tire_deformation_all_wheels");
        // Imported mesh creation/rebinding stays on the main thread. Once the
        // private meshes exist, each wheel owns all of its mutable cage/render
        // state and can prepare independently; the physics snapshot is read-only.
        const auto& config = VehicleState().vehicle_simulation->get_config();
        const auto& spec = VehicleState().vehicle_simulation->get_spec();
        bool can_prepare_in_parallel = true;
        for (int i = 0; i < 4; ++i)
            if (valid_wheels[i] && (!VehicleState().tire_visuals[i] ||
                VehicleState().tire_visuals[i]->cage.radius != config.wheel_radius_for(i) ||
                VehicleState().tire_visuals[i]->cage.width != (i < 2 ? spec.front_wheel_width : spec.rear_wheel_width)))
                can_prepare_in_parallel = false;
        auto prepare = [&](uint32_t first, uint32_t last)
        {
            for (uint32_t i = first; i < last; ++i)
                if (valid_wheels[i]) UpdateTireDeformation(i, true, &wheel_batches[i]);
        };
        SP_PROFILE_CPU_START("tire_prepare_batch");
        if (can_prepare_in_parallel) ThreadPool::ParallelLoop(prepare, 4);
        else prepare(0, 4);
        SP_PROFILE_CPU_END();
        for (auto& wheel : wheel_batches)
        {
            for (auto& job : wheel.skin) batch.skin.push_back(std::move(job));
            for (auto& upload : wheel.uploads) batch.uploads.push_back(std::move(upload));
        }
        // One dispatch for the whole car, not a dispatch/barrier for every
        // material of every wheel. Mesh uploads run only after all jobs join.
        if (!batch.skin.empty())
        {
            SP_PROFILE_CPU_START("tire_mesh_skin_batch");
            std::atomic<uint32_t> next_job{0};
            ThreadPool::ParallelLoop([&](uint32_t, uint32_t)
            {
                uint32_t job;
                while ((job = next_job.fetch_add(1, std::memory_order_relaxed)) < batch.skin.size())
                    batch.skin[job]();
            }, static_cast<uint32_t>(batch.skin.size()));
            SP_PROFILE_CPU_END();
            for (auto& upload : batch.uploads) upload();
        }
        SP_PROFILE_CPU_END();
    }

    void CarPhysics::UpdateTireDeformation(int wheel_index, bool grounded, TireDeformationBatch* batch)
    {
        SP_PROFILE_CPU();
        Entity* wheel_entity = VehicleState().wheel_entities[wheel_index];
        if (!wheel_entity || !VehicleState().vehicle_simulation) return;
        const auto& state = VehicleState().vehicle_simulation->get_wheel_state(wheel_index);
        const auto& spec = VehicleState().vehicle_simulation->get_spec();
        const auto& cfg = VehicleState().vehicle_simulation->get_config();
        const float radius = cfg.wheel_radius_for(wheel_index);
        const float width = wheel_index < 2 ? spec.front_wheel_width : spec.rear_wheel_width;
        if (radius <= 0 || width <= 0) return;

        // Cheap/distant traffic keeps its undeformed mesh. Only physical tires
        // near the camera need skinning; restore the original if leaving range.
        if (Camera* camera = World::GetCamera())
            grounded &= Vector3::DistanceSquared(camera->GetEntity()->GetPosition(), wheel_entity->GetPosition()) < 40.0f * 40.0f;
        grounded &= state.grounded && state.tire_load > 0;
        auto& visual = VehicleState().tire_visuals[wheel_index];
        if (!visual && !grounded) return;
        const Quaternion rotation = wheel_entity->GetRotation();
        const Vector3 hub = wheel_entity->GetPosition() + rotation * VehicleState().wheel_mesh_center_offsets[wheel_index];
        const Matrix wheel_frame(hub, rotation, Vector3::One);

        // Build immutable bindings once per wheel/size. Never modify cached
        // imported geometry: all four tires and all cars share those resources.
        if (visual && (visual->cage.radius != radius || visual->cage.width != width))
        {
            for (auto& part : visual->parts)
                if (Entity* entity = World::GetEntityById(part.entity_id))
                    if (Render* render = entity->GetComponent<Render>())
                    {
                        render->SetMesh(part.source.get(), part.submesh);
                        render->ClearBoundingBoxOverride();
                        render->SetAllowBlasUpdate(false);
                    }
            visual.reset();
        }
        if (!visual)
        {
            auto pending = std::make_unique<TireVisualState>();
            pending->cage.initialize(radius, width);
            std::vector<Entity*> entities = {wheel_entity};
            wheel_entity->GetDescendants(&entities);
            for (Entity* entity : entities)
            {
                Render* render = entity->GetComponent<Render>();
                if (!render || !render->GetMesh() || !render->GetMaterial()) continue;
                // Explicit hero-asset contract: rigid rim, rotor and caliper are
                // never bound. Sidewall lettering follows the same rubber cage.
                const std::string& material = render->GetMaterial()->GetObjectName();
                if (material.find("rubber") == std::string::npos && material.find("sidewall") == std::string::npos) continue;
                TireVisualState::Part part;
                part.entity_id = entity->GetObjectId();
                part.source = std::static_pointer_cast<Mesh>(render->GetMesh()->shared_from_this());
                part.submesh = render->GetSubMeshIndex();
                for (const auto& other : pending->parts)
                    if (other.source == part.source) part.mesh = other.mesh;
                if (!part.mesh) part.mesh = part.source->CreateSkinnedInstance();
                if (!part.mesh) return; // retry after import has published the GPU geometry
                part.mesh->SetResourceFilePath(part.source->GetResourceFilePath());
                part.to_wheel = entity->GetMatrix() * wheel_frame.Inverted();
                part.from_wheel = part.to_wheel.Inverted();
                const auto& source_vertices = part.source->GetVertices();
                for (const auto& lod : part.source->GetSubMesh(part.submesh).lods)
                {
                    auto& bindings = part.vertices;
                    bindings.reserve(bindings.size() + lod.vertex_count);
                    for (uint32_t j = 0; j < lod.vertex_count; ++j)
                    {
                        const auto& vertex = source_vertices[lod.vertex_offset + j];
                        const Vector3 p = vertex.get_position(), n = vertex.get_normal();
                        Vector3 t = vertex.get_tangent();
                        if (t.LengthSquared() < 0.1f) t = n.Cross(std::abs(n.y) < 0.9f ? Vector3::Up : Vector3::Right).Normalized();
                        const Vector3 b = n.Cross(t).Normalized();
                        const Vector3 origin = part.to_wheel * Vector3::Zero;
                        TireVisualState::BoundVertex bound;
                        bound.vertex_index = lod.vertex_offset + j;
                        bound.lod_index = static_cast<uint32_t>(&lod - part.source->GetSubMesh(part.submesh).lods.data());
                        bound.position = p;
                        bound.tangent = t;
                        bound.bitangent = b;
                        if (bound.lod_index == 0) pending->cage.include_contact_vertex(part.to_wheel * p);
                        bound.weights = pending->cage.bind(part.to_wheel * p,
                            part.to_wheel * t - origin, part.to_wheel * b - origin);
                        bindings.push_back(bound);
                    }
                }
                std::stable_sort(part.vertices.begin(), part.vertices.end(), [](const auto& a, const auto& b)
                {
                    return a.weights.nodes[0] < b.weights.nodes[0];
                });
                for (const auto& bound : part.vertices) ++part.cell_offsets[bound.weights.nodes[0] + 1];
                for (size_t cell = 1; cell < part.cell_offsets.size(); ++cell)
                    part.cell_offsets[cell] += part.cell_offsets[cell - 1];
                part.dirty_blocks.resize((source_vertices.size() + TireVisualState::Part::upload_block_size - 1) /
                    TireVisualState::Part::upload_block_size);
                for (const auto& bound : part.vertices)
                    part.cell_blocks[bound.weights.nodes[0]].push_back(bound.vertex_index / TireVisualState::Part::upload_block_size);
                for (auto& blocks : part.cell_blocks)
                {
                    std::sort(blocks.begin(), blocks.end());
                    blocks.erase(std::unique(blocks.begin(), blocks.end()), blocks.end());
                }
                pending->parts.push_back(std::move(part));
            }
            if (pending->parts.empty()) return;
            for (auto& part : pending->parts)
                if (Entity* entity = World::GetEntityById(part.entity_id))
                    if (Render* render = entity->GetComponent<Render>())
                    {
                        render->SetOwnedMesh(part.mesh);
                        render->SetMesh(part.mesh.get(), part.submesh);
                        render->SetAllowBlasUpdate(true);
                    }
            visual = std::move(pending);
        }

        Vector3 normal = from_px_vec3(state.contact_normal);
        normal = rotation.Conjugate() * (VehicleState().vehicle_render_rotation * (VehicleState().vehicle_physics_rotation.Conjugate() * normal));
        const Vector3 point = rotation.Conjugate() * (TransformVehiclePointToRender(from_px_vec3(state.contact_point)) - hub);
        float distance = -normal.Dot(point);
        grounded &= car::prepare_tire_contact_plane(normal, distance, radius);
        const float stiffness = car::loaded_tire_stiffness(spec, state.pressure_bar) / std::max(spec.tire_vertical_stiffness, 1.0f);
        const float ambient_bar = VehicleState().vehicle_simulation->ambient_pressure / 100000.0f;
        // At rest, keep the solved shape until the support plane moves by a
        // tenth of a millimetre. Compare to the last solve so motion accumulates.
        const bool upload = !visual->has_shape || grounded != visual->deformed || (grounded &&
            ((normal - visual->previous_normal).LengthSquared() > 1e-6f ||
             std::abs(distance - visual->previous_distance) > 0.0001f ||
             std::abs(stiffness - visual->previous_stiffness) > 0.001f ||
             std::abs(state.pressure_bar - visual->previous_pressure) > 0.001f ||
             std::abs(spec.tire_vertical_stiffness - visual->previous_reference_stiffness) > 1.0f ||
             std::abs(ambient_bar - visual->previous_ambient_pressure) > 0.001f));
        const auto previous_cells = visual->active_cells;
        if (upload)
        {
            SP_PROFILE_CPU_START("tire_cage_solve");
            visual->cage.solve(normal, distance, stiffness, grounded,
                state.pressure_bar, spec.tire_vertical_stiffness, ambient_bar);
            SP_PROFILE_CPU_END();
            visual->previous_normal = normal;
            visual->previous_distance = distance;
            visual->previous_stiffness = stiffness;
            visual->previous_pressure = state.pressure_bar;
            visual->previous_reference_stiffness = spec.tire_vertical_stiffness;
            visual->previous_ambient_pressure = ambient_bar;
            visual->has_shape = true;
            visual->active_cells.fill(false);
            for (int band = 0; band < car::tire_cage::bands - 1; ++band)
                for (int lane = 0; lane < car::tire_cage::lanes - 1; ++lane)
                    for (int a = 0; a < car::tire_cage::sectors; ++a)
                        for (int r = 0; r < 2; ++r)
                            for (int x = 0; x < 2; ++x)
                                for (int t = 0; t < 2; ++t)
                                    visual->active_cells[car::tire_cage::index(band, lane, a)] |=
                                        visual->cage.displacement[car::tire_cage::index(band + r, lane + x, a + t)].LengthSquared() > 1e-12f;
        }
        for (auto& part : visual->parts)
        {
            Entity* entity = World::GetEntityById(part.entity_id);
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            if (!render || render->GetMesh() != part.mesh.get()) continue;
            // A conservative world bound also selects the renderer's deformable
            // meshlet path: rest-pose cone/bounds tests cannot reject this tire.
            const BoundingBox local = part.source->GetSubMesh(part.submesh).lods[0].aabb;
            const BoundingBox world = local * entity->GetMatrix();
            render->SetBoundingBoxOverride(BoundingBox(world.GetMin() - Vector3(0.06f), world.GetMax() + Vector3(0.06f)));
            const uint32_t current_lod = render->GetLodIndex();
            const bool lod_changed = current_lod != part.previous_lod;
            if (upload || lod_changed)
            {
                auto& vertices = part.mesh->GetVertices();
                const auto& original = part.source->GetVertices();
                const Vector3 origin = part.from_wheel * Vector3::Zero;
                const Vector3 wheel_origin = part.to_wheel * Vector3::Zero;
                const Vector3 plane_normal(
                    normal.Dot(part.to_wheel * Vector3::Right - wheel_origin),
                    normal.Dot(part.to_wheel * Vector3::Up - wheel_origin),
                    normal.Dot(part.to_wheel * Vector3::Forward - wheel_origin));
                const float plane_distance = normal.Dot(wheel_origin) + distance;
                const Vector3 contact_direction = part.from_wheel * normal - origin;
                SP_PROFILE_CPU_START("tire_mesh_prepare");
                // Transform the 576 cage displacements once instead of applying
                // three matrices to every affected render vertex.
                auto& local_displacement = part.local_displacement;
                auto& dirty_cells = part.dirty_cells;
                uint32_t dirty_count = 0;
                std::fill(part.dirty_blocks.begin(), part.dirty_blocks.end(), 0);
                for (uint16_t cell = 0; cell < car::tire_cage::node_count; ++cell)
                {
                    local_displacement[cell] = part.from_wheel * visual->cage.displacement[cell] - origin;
                    if ((lod_changed || visual->active_cells[cell] || previous_cells[cell]) &&
                        part.cell_offsets[cell] != part.cell_offsets[cell + 1])
                    {
                        dirty_cells[dirty_count++] = cell;
                        for (uint32_t block : part.cell_blocks[cell]) part.dirty_blocks[block] = 1;
                    }
                }
                auto skin_range = [&part, &vertices, &original, &local_displacement,
                    visual_ptr = visual.get(), current_lod, grounded, plane_normal, plane_distance, contact_direction](uint32_t begin, uint32_t end)
                {
                    for (uint32_t j = begin; j < end; ++j)
                    {
                        const auto& bound = part.vertices[j];
                        // BLAS uses LOD 0; rasterization uses current_lod.
                        if (bound.lod_index != 0 && bound.lod_index != current_lod) continue;
                        auto& vertex = vertices[bound.vertex_index];
                        if (!visual_ptr->active_cells[bound.weights.nodes[0]])
                        {
                            vertex = original[bound.vertex_index];
                            continue;
                        }
                        Vector3 p = bound.position, t = bound.tangent, b = bound.bitangent;
                        for (int k = 0; k < 8; ++k)
                        {
                            const Vector3& d = local_displacement[bound.weights.nodes[k]];
                            p += d * bound.weights.weight[k];
                            t += d * bound.weights.tangent_weight[k];
                            b += d * bound.weights.bitangent_weight[k];
                        }
                        // Contact belongs to actual rubber vertices, not to the
                        // enclosing cage. Resolve interpolation error against
                        // the same physical support plane, including its tangent
                        // derivative, so a rounded tread cannot penetrate it.
                        if (grounded) car::project_tire_vertex(p, t, b, plane_normal, plane_distance, contact_direction);
                        vertex.set_position(p);
                        const Vector3 n = t.Cross(b);
                        // Octahedral packing is scale invariant; no square roots needed.
                        vertex.set_normal(n.LengthSquared() > 1e-12f ? n : original[bound.vertex_index].get_normal());
                        vertex.set_tangent(t.LengthSquared() > 1e-12f ? t : original[bound.vertex_index].get_tangent());
                    }
                };
                // Cells have very different vertex counts. Split dense cells,
                // then let workers pull bounded jobs across all four wheels.
                for (uint32_t c = 0; c < dirty_count; ++c)
                {
                    const uint16_t cell = dirty_cells[c];
                    const uint32_t last = part.cell_offsets[cell + 1];
                    for (uint32_t first = part.cell_offsets[cell]; first < last; first += 1024)
                    {
                        const uint32_t end = std::min(first + 1024, last);
                        if (batch) batch->skin.push_back([skin_range, first, end]() { skin_range(first, end); });
                        else skin_range(first, end);
                    }
                }
                SP_PROFILE_CPU_END();
                auto upload_ranges = [&part, &vertices, render, dirty_count, current_lod]()
                {
                    // Merge adjacent dirty blocks into uploads. This includes arcs
                    // leaving contact, whose vertices were restored above.
                    for (uint32_t block = 0; block < part.dirty_blocks.size();)
                    {
                        if (!part.dirty_blocks[block]) { ++block; continue; }
                        const uint32_t first = block++;
                        while (block < part.dirty_blocks.size() && part.dirty_blocks[block]) ++block;
                        const uint32_t begin = first * TireVisualState::Part::upload_block_size;
                        const uint32_t end = std::min(block * TireVisualState::Part::upload_block_size,
                            static_cast<uint32_t>(vertices.size()));
                        const auto& lods = part.source->GetSubMesh(part.submesh).lods;
                        for (uint32_t lod_index : {0u, current_lod})
                        {
                            const auto& lod = lods[lod_index];
                            const uint32_t first_vertex = std::max(begin, lod.vertex_offset);
                            const uint32_t last_vertex = std::min(end, lod.vertex_offset + lod.vertex_count);
                            if (last_vertex > first_vertex)
                                part.mesh->UploadVertexRange(first_vertex, last_vertex - first_vertex);
                            if (current_lod == 0) break;
                        }
                    }
                    if (dirty_count) render->SetNeedsBlasRefit(true);
                };
                if (batch && dirty_count) batch->uploads.push_back(upload_ranges);
                else upload_ranges();
                part.previous_lod = current_lod;
            }

        }
        visual->deformed = grounded;
    }

    void CarPhysics::CaptureCheapWheelRestPoses()
    {
        for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
        {
            Entity* wheel_entity = VehicleState().wheel_entities[i];
            if (!wheel_entity)
            {
                continue;
            }
            if (!VehicleState().cheap_wheel_rest_captured[i])
            {
                VehicleState().cheap_wheel_local_pos[i] = wheel_entity->GetPositionLocal();
                VehicleState().cheap_wheel_local_rot[i] = wheel_entity->GetRotationLocal();
                VehicleState().cheap_wheel_rest_captured[i] = true;
            }
        }
    }

    void CarPhysics::UpdateTrafficWheels(float speed, float curvature, float delta_time)
    {
        if (VehicleState().vehicle_simulation_active || physics.GetBodyType() != BodyType::Custom) return;
        const auto& preset = EnsureVehicleSimulation()->get_spec();
        const float radius = std::max((preset.front_wheel_radius + preset.rear_wheel_radius) * 0.5f, 0.2f);
        VehicleState().cheap_wheel_roll = fmodf(VehicleState().cheap_wheel_roll + speed * delta_time / radius, math::pi * 2.0f);
        VehicleState().cheap_steer_angle = std::clamp(atanf(curvature * preset.wheelbase), -preset.max_steer_angle, preset.max_steer_angle);
        UpdateCheapWheelTransforms();
    }

    void CarPhysics::UpdateCheapWheelTransforms()
    {
        if (physics.GetBodyType() != BodyType::Custom || !Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        CaptureCheapWheelRestPoses();

        Entity* vehicle_entity = GetEntity();
        if (!vehicle_entity)
        {
            return;
        }

        const Vector3 vehicle_position = vehicle_entity->GetPosition();
        const Quaternion vehicle_rotation = vehicle_entity->GetRotation();
        Vector3 car_right = vehicle_rotation * Vector3::Right;
        Vector3 car_up = vehicle_rotation * Vector3::Up;
        if (car_right.LengthSquared() > 0.0001f)
        {
            car_right.Normalize();
        }
        if (car_up.LengthSquared() > 0.0001f)
        {
            car_up.Normalize();
        }

        const Quaternion spin = Quaternion::FromAxisAngle(car_right, VehicleState().cheap_wheel_roll);
        const Quaternion steer_q = Quaternion::FromAxisAngle(car_up, VehicleState().cheap_steer_angle);

        for (int i = 0; i < static_cast<int>(WheelIndex::Count); i++)
        {
            Entity* wheel_entity = VehicleState().wheel_entities[i];
            if (!wheel_entity || !VehicleState().cheap_wheel_rest_captured[i])
            {
                continue;
            }

            const bool is_front =
                i == static_cast<int>(WheelIndex::FrontLeft) ||
                i == static_cast<int>(WheelIndex::FrontRight);
            const Quaternion base = vehicle_rotation * VehicleState().cheap_wheel_local_rot[i];
            Quaternion wheel_rotation = (is_front ? steer_q : Quaternion::Identity) * spin * base;
            Vector3 wheel_position = vehicle_position + vehicle_rotation * VehicleState().cheap_wheel_local_pos[i];
            wheel_position -= wheel_rotation * VehicleState().wheel_mesh_center_offsets[i];
            if (!wheel_position.IsFinite() || !wheel_rotation.IsFinite())
            {
                continue;
            }
            wheel_entity->SetPositionAndRotation(wheel_position, wheel_rotation);
            if (Entity* caliper = VehicleState().wheel_calipers[i])
                caliper->SetRotation((is_front ? steer_q : Quaternion::Identity) * base);
            UpdateTireDeformation(i, false);
        }
    }

    void* CarPhysics::Create()
    {
        PxPhysics* physics = static_cast<PxPhysics*>(PhysicsWorld::GetPhysics());
        PxScene* scene     = static_cast<PxScene*>(PhysicsWorld::GetScene());

        EnsureVehicleSimulation();
        PhysicsWorld::RebaseOrigin(GetEntity()->GetPosition());
        car::setup_params params;
        params.physics = physics;
        params.scene   = scene;
        params.create_mechanisms = VehicleState().vehicle_sim_mode == VehicleSimMode::Full;

        if (VehicleState().vehicle_simulation->setup(params))
        {
            const Vector3 origin = PhysicsWorld::GetOrigin();
            VehicleState().vehicle_simulation->shift_origin(PxVec3(origin.x, origin.y, origin.z) - VehicleState().vehicle_simulation->get_scene_origin());
            PxRigidDynamic* body = VehicleState().vehicle_simulation->get_body();

            Vector3 pos = PhysicsWorld::ToPhysicsPosition(GetEntity()->GetPosition());
            PxTransform current_pose = body->getGlobalPose();
            body->setGlobalPose(PxTransform(PxVec3(pos.x, current_pose.p.y, pos.z)));
            if (params.create_mechanisms)
            {
                if (!VehicleState().vehicle_simulation->rebuild_multibody(false))
                {
                    SP_LOG_ERROR("failed to place car suspension assembly");
                }
            }
            if (VehicleState().chassis_entity)
            {
                BuildChassisConvexShapes(VehicleState().chassis_entity, VehicleState().chassis_entities_to_exclude);
            }
            body->userData = reinterpret_cast<void*>(GetEntity());
            tag_actor_shapes(body, 2, VehicleState().vehicle_simulation->multibody_collision_group());
            if (!VehicleState().vehicle_simulation_active)
            {
                VehicleState().vehicle_simulation->set_simulation_enabled(false);
            }
            else if (VehicleState().vehicle_sim_mode == VehicleSimMode::Cheap)
            {
                VehicleState().vehicle_simulation->set_mechanism_simulation_enabled(false);
                body->setActorFlag(PxActorFlag::eDISABLE_GRAVITY, true);
            }

            // run the vehicle force model in lockstep with the fixed physics step
            PhysicsWorld::RegisterStepCallback(this, [this](float dt) { TickVehicleSubstep(dt); });
        }
        else
        {
            SP_LOG_ERROR("failed to create vehicle physics body");
        }

        return GetVehicleSimulation()->get_body();
    }
    void CarPhysics::Remove()
    {
        PhysicsWorld::UnregisterStepCallback(this);
        if (!m_vehicle) return;
        if (VehicleState().vehicle_simulation && VehicleState().vehicle_simulation->get_body())
        {
            PhysicsWorld::RemoveActor(
                VehicleState().vehicle_simulation->get_body()
            );
            VehicleState().vehicle_simulation->destroy();
        }

    }
    void CarPhysics::ShiftOrigin(const Vector3& shift)
    {
        if (VehicleState().vehicle_simulation) VehicleState().vehicle_simulation->shift_origin(to_px_vec3(shift));
    }
    bool CarPhysics::SetTransform(const Vector3& position, const Quaternion& rotation, bool rebuild_vehicle)
    {
        if (!m_vehicle) return false;
        m_vehicle->vehicle_physics_position = position;
        m_vehicle->vehicle_physics_rotation = rotation;
        m_vehicle->vehicle_render_position = position;
        m_vehicle->vehicle_render_rotation = rotation;
        // for vehicles, use the simulation body directly
        PxRigidDynamic* vehicle_body = VehicleState().vehicle_simulation ? VehicleState().vehicle_simulation->get_body() : nullptr;
        if (physics.GetBodyType() == BodyType::Custom && vehicle_body)
        {
            PhysicsWorld::RebaseOrigin(position);
            PxTransform pose = to_px_transform(position, rotation);
            VehicleState().vehicle_simulation->record_reset();
            vehicle_body->setGlobalPose(pose);
            vehicle_body->setLinearVelocity(PxVec3(0, 0, 0));
            vehicle_body->setAngularVelocity(PxVec3(0, 0, 0));

            // reset wheel angular velocities
            for (int i = 0; i < 4; i++)
            {
                VehicleState().vehicle_simulation->set_wheel_angular_velocity(i, 0.0f);
            }

            if (rebuild_vehicle && VehicleState().vehicle_simulation->has_multibody())
            {
                if (!VehicleState().vehicle_simulation->rebuild_multibody(false))
                {
                    SP_LOG_ERROR("failed to rebuild suspension after vehicle reset");
                }
                VehicleState().vehicle_simulation->reset_drivetrain_transients();
                VehicleState().vehicle_simulation->reset_wheel_thermals();
            }
            VehicleState().vehicle_simulation->clear_force_accumulators();
            return true;
        }

        return false;
    }
    CarPhysics::~CarPhysics() { Remove(); }
    CarPhysics& CarPhysics::Get(const Physics& owner)
    {
        return static_cast<CarPhysics&>(owner.EnsureCustomBody());
    }
    void CarPhysics::Initialize()
    {
        Physics::SetBodyFactory([](Physics& owner) -> unique_ptr<PhysicsBody> { return make_unique<CarPhysics>(owner); });
        RegisterVehicleContacts();
    }
}
