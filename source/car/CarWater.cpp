/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include "CarWater.h"
#include "CarSimulation.h"
#include "../rendering/Renderer.h"
#include "../world/OceanWake.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace physx;
//============================

namespace spartan
{
    namespace
    {
        constexpr float water_density  = 1025.0f; // kg/m^3, sea water
        constexpr float gravity        = 9.81f;
        constexpr float column_spacing = 0.2f;    // m, hull slice width
        constexpr float drag_hull      = 0.9f;    // bluff body pushed broadside through water
        constexpr float drag_slam      = 1.0f;    // a flat underbody meeting the surface
        constexpr float drag_tire      = 0.7f;
        // of the hull envelope, the share that keeps water out, the cabin and trunk hold air until the seals give,
        // the engine bay, wheel wells and underbody flood at once, the metal and glass stay, at a quarter a
        // sports car keeps its tires on the bed until the water nears the door handles
        constexpr float sealed_dry     = 0.25f;
        constexpr float sealed_swamped = 0.1f;
        constexpr float flood_rate     = 1.0f / 40.0f; // per second at a metre of head over the floor
        constexpr float drain_rate     = 1.0f / 25.0f; // per second once the car is back on land
        constexpr float tire_air       = 0.45f;        // of a tire's displaced volume, the share that is air
        constexpr float intake_hold    = 0.4f;         // seconds of water over the intake before the engine swallows it
        constexpr float wave_reach     = 6.0f;         // m above the calm sea the waves can reach, cars higher up skip every query

        float circle_segment_area(float radius, float depth)
        {
            const float d = PxClamp(depth, 0.0f, 2.0f * radius);
            const float h = radius - d;
            return radius * radius * acosf(PxClamp(h / radius, -1.0f, 1.0f)) - h * sqrtf(PxMax(2.0f * radius * d - d * d, 0.0f));
        }

        // vertical rays through every chassis hull, the union of the hits per slice is the column
        void build_hull(PxRigidDynamic* body, CarWaterState& state)
        {
            PxShape* shapes[32];
            const PxU32 count = body->getShapes(shapes, 32);
            const void* key   = count > 0 ? static_cast<const void*>(shapes[0]) : nullptr;
            if (key == state.hull_key && count == state.hull_shape_count)
            {
                return;
            }
            state.hull_key         = key;
            state.hull_shape_count = count;
            state.columns.clear();
            state.hull_volume      = 0.0f;

            PxBounds3 bounds = PxBounds3::empty();
            for (PxU32 i = 0; i < count; i++)
            {
                PxBounds3 shape_bounds;
                if (PxGeometryQuery::computeGeomBounds(shape_bounds, shapes[i]->getGeometry(), shapes[i]->getLocalPose()))
                {
                    bounds.include(shape_bounds);
                }
            }
            if (bounds.isEmpty())
            {
                return;
            }

            const PxVec3 size  = bounds.getDimensions();
            const int count_x  = PxMax(1, static_cast<int>(ceilf(size.x / column_spacing)));
            const int count_z  = PxMax(1, static_cast<int>(ceilf(size.z / column_spacing)));
            const float step_x = size.x / count_x;
            const float step_z = size.z / count_z;
            const float reach  = size.y + 1.0f;
            state.column_area  = step_x * step_z;
            state.center_x     = bounds.getCenter().x;
            state.center_z     = bounds.getCenter().z;
            state.half_x       = size.x * 0.5f;
            state.half_z       = size.z * 0.5f;
            state.bottom       = bounds.minimum.y;
            state.top          = bounds.maximum.y;

            for (int ix = 0; ix < count_x; ix++)
            {
                for (int iz = 0; iz < count_z; iz++)
                {
                    const float x = bounds.minimum.x + (ix + 0.5f) * step_x;
                    const float z = bounds.minimum.z + (iz + 0.5f) * step_z;
                    float top     = -PX_MAX_F32;
                    float bottom  = PX_MAX_F32;
                    for (PxU32 i = 0; i < count; i++)
                    {
                        const PxGeometry& geometry = shapes[i]->getGeometry();
                        const PxTransform pose     = shapes[i]->getLocalPose();
                        PxGeomRaycastHit hit;
                        const PxVec3 above(x, bounds.maximum.y + 0.5f, z);
                        if (PxGeometryQuery::raycast(above, PxVec3(0.0f, -1.0f, 0.0f), geometry, pose, reach, PxHitFlag::eDEFAULT, 1, &hit))
                        {
                            top = PxMax(top, above.y - hit.distance);
                        }
                        const PxVec3 below(x, bounds.minimum.y - 0.5f, z);
                        if (PxGeometryQuery::raycast(below, PxVec3(0.0f, 1.0f, 0.0f), geometry, pose, reach, PxHitFlag::eDEFAULT, 1, &hit))
                        {
                            bottom = PxMin(bottom, below.y + hit.distance);
                        }
                    }
                    if (top > bottom + 0.02f)
                    {
                        state.columns.push_back({ x, z, bottom, top });
                        state.hull_volume += (top - bottom) * state.column_area;
                    }
                }
            }
        }

        float bilinear(const float heights[4], float u, float v)
        {
            u = PxClamp(u, 0.0f, 1.0f);
            v = PxClamp(v, 0.0f, 1.0f);
            return (heights[0] * (1.0f - u) + heights[1] * u) * (1.0f - v) + (heights[2] * (1.0f - u) + heights[3] * u) * v;
        }

        void leave_water(car::Simulation& simulation, CarWaterState& state, uint64_t id, float dt)
        {
            if (state.in_water)
            {
                OceanWake::Displace(id, nullptr, 0);
            }
            state.in_water  = false;
            state.submerged = 0.0f;
            state.flood      = PxMax(state.flood - state.flood * drain_rate * dt, 0.0f);
            state.intake_wet = 0.0f;
            simulation.set_intake_flooded(false);
        }
    }

    void car_water::tick(car::Simulation& simulation, CarWaterState& state, uint64_t id, float dt)
    {
        PxRigidDynamic* body = simulation.get_body();
        float sea_level      = 0.0f;
        if (!body || dt <= 0.0f || (body->getRigidBodyFlags() & PxRigidBodyFlag::eKINEMATIC) || !Renderer::GetOceanSeaLevel(sea_level))
        {
            leave_water(simulation, state, id, dt);
            return;
        }

        build_hull(body, state);
        if (state.columns.empty())
        {
            leave_water(simulation, state, id, dt);
            return;
        }

        // physics positions plus the scene origin are map positions, the ocean lives in map space
        const PxTransform pose = body->getGlobalPose();
        const PxVec3 origin    = simulation.get_scene_origin();
        const float radius     = PxVec3(state.half_x, (state.top - state.bottom) * 0.5f, state.half_z).magnitude() + PxAbs(state.center_x) + PxAbs(state.center_z);
        if (pose.p.y + origin.y - radius > sea_level + wave_reach)
        {
            leave_water(simulation, state, id, dt);
            return;
        }

        // the fft and surf surface at the four footprint corners, the wake adds its detail per column below
        float corner_heights[4];
        const float x0 = state.center_x - state.half_x;
        const float z0 = state.center_z - state.half_z;
        for (int i = 0; i < 4; i++)
        {
            const PxVec3 local((i & 1) ? state.center_x + state.half_x : x0, state.bottom, (i >> 1) ? state.center_z + state.half_z : z0);
            const PxVec3 world = pose.transform(local) + origin;
            if (!Renderer::GetOceanHeight(world.x, world.z, corner_heights[i]))
            {
                leave_water(simulation, state, id, dt);
                return;
            }
        }

        const float lowest = pose.p.y + origin.y - radius;
        if (lowest > max(max(corner_heights[0], corner_heights[1]), max(corner_heights[2], corner_heights[3])) + 0.5f)
        {
            leave_water(simulation, state, id, dt);
            return;
        }

        // wake heights at every column and tire in one lock
        const auto& multibody = simulation.get_multibody_state();
        const car::config& cfg = simulation.get_config();
        const size_t column_count = state.columns.size();
        state.wake_xz.resize((column_count + car::wheel_count) * 2);
        state.wake_heights.resize(column_count + car::wheel_count);
        for (size_t i = 0; i < column_count; i++)
        {
            const CarWaterColumn& column = state.columns[i];
            const PxVec3 world           = pose.transform(PxVec3(column.x, column.bottom, column.z)) + origin;
            state.wake_xz[i * 2 + 0]     = world.x;
            state.wake_xz[i * 2 + 1]     = world.z;
        }
        for (int i = 0; i < car::wheel_count; i++)
        {
            const PxRigidDynamic* wheel = multibody.corners[i].wheel_body;
            const PxVec3 world          = (wheel ? wheel->getGlobalPose().p : pose.p) + origin;
            state.wake_xz[(column_count + i) * 2 + 0] = world.x;
            state.wake_xz[(column_count + i) * 2 + 1] = world.z;
        }
        OceanWake::SampleHeights(state.wake_xz.data(), state.wake_heights.data(), static_cast<uint32_t>(column_count + car::wheel_count));

        // the cabin takes water through the door seals and vents, faster under more head
        const float height       = state.top - state.bottom;
        const float inv_width    = 1.0f / PxMax(2.0f * state.half_x, 0.01f);
        const float inv_length   = 1.0f / PxMax(2.0f * state.half_z, 0.01f);
        const float water_center = bilinear(corner_heights, 0.5f, 0.5f);
        const float floor_y      = (pose.transform(PxVec3(state.center_x, state.bottom + 0.15f * height, state.center_z)) + origin).y;
        const float intake_y     = (pose.transform(PxVec3(state.center_x, state.bottom + 0.55f * height, state.center_z)) + origin).y;
        const float head         = water_center - floor_y;
        if (head > 0.0f)
        {
            state.flood = PxMin(state.flood + flood_rate * sqrtf(head) * (1.0f - state.flood) * dt, 1.0f);
        }
        else
        {
            state.flood = PxMax(state.flood - state.flood * drain_rate * dt, 0.0f);
        }
        state.intake_wet = PxClamp(state.intake_wet + (water_center > intake_y ? dt : -dt), 0.0f, intake_hold);
        simulation.set_intake_flooded(state.intake_wet >= intake_hold);
        const float sealed = sealed_dry + (sealed_swamped - sealed_dry) * state.flood;

        // the length of hull the water flows past, the frontal area is the submerged volume over it
        const PxVec3 velocity = body->getLinearVelocity();
        PxVec3 flow_local     = pose.q.rotateInv(PxVec3(velocity.x, 0.0f, velocity.z));
        const float flow      = flow_local.normalize();
        const float run       = flow > 1e-3f ? PxMax(2.0f * (PxAbs(flow_local.x) * state.half_x + PxAbs(flow_local.z) * state.half_z), 0.5f) : 2.0f * state.half_z;

        PxVec3 drag_sum       = PxVec3(0.0f);
        float volume_sum      = 0.0f;
        state.wake.clear();

        struct Push
        {
            PxVec3 point;
            PxVec3 drag;
        };
        thread_local vector<Push> pushes;
        pushes.clear();

        for (size_t i = 0; i < column_count; i++)
        {
            const CarWaterColumn& column = state.columns[i];
            const PxVec3 low  = pose.transform(PxVec3(column.x, column.bottom, column.z));
            const PxVec3 high = pose.transform(PxVec3(column.x, column.top, column.z));
            const float u     = (column.x - x0) * inv_width;
            const float v     = (column.z - z0) * inv_length;
            const float water_base = bilinear(corner_heights, u, v) - origin.y;
            const float water      = water_base + state.wake_heights[i];

            // a column on its side or upside down still spans low to high in y
            const float y_min  = PxMin(low.y, high.y);
            const float y_max  = PxMax(low.y, high.y);
            const float span   = PxMax(y_max - y_min, 1e-3f);

            // the wake source is measured against the sea without the wake, measured against the wake the water
            // the hull lifts would count as more water it holds out and the field would feed itself
            const float wet_base = PxClamp((water_base - y_min) / span, 0.0f, 1.0f);
            if (wet_base > 0.0f)
            {
                const PxVec3 map = pose.transform(PxVec3(column.x, column.bottom, column.z)) + origin;
                state.wake.push_back({ map.x, map.z, wet_base * (column.top - column.bottom) * state.column_area });
            }

            const float wet = PxClamp((water - y_min) / span, 0.0f, 1.0f);
            if (wet <= 0.0f)
            {
                continue;
            }

            const float volume    = wet * (column.top - column.bottom) * state.column_area;
            const PxVec3 dry_end  = low.y <= high.y ? low : high;
            const PxVec3 wet_end  = low.y <= high.y ? high : low;
            const PxVec3 centroid = dry_end + (wet_end - dry_end) * (wet * 0.5f);
            volume_sum           += volume;

            const PxVec3 buoyancy = PxVec3(0.0f, water_density * gravity * volume * sealed, 0.0f);
            PxRigidBodyExt::addForceAtPos(*body, buoyancy, centroid, PxForceMode::eFORCE, true);

            // pushing water aside costs its dynamic pressure, broadside through the frontal area, and the
            // underbody slaps flat onto the surface where the column crosses it
            const PxVec3 point_velocity = PxRigidBodyExt::getVelocityAtPos(*body, centroid);
            const PxVec3 horizontal(point_velocity.x, 0.0f, point_velocity.z);
            const float area_front      = volume / run;
            const float area_flat       = state.column_area * (wet < 1.0f ? 1.0f : 0.3f);
            PxVec3 drag                 = -0.5f * water_density * drag_hull * area_front * horizontal.magnitude() * horizontal;
            drag.y                      = -0.5f * water_density * drag_slam * area_flat * PxAbs(point_velocity.y) * point_velocity.y;
            pushes.push_back({ centroid, drag });
            drag_sum += drag;
        }

        // drag removes momentum, it never reverses the motion within a step
        const float mass  = PxMax(body->getMass(), 1.0f);
        const float speed = velocity.magnitude();
        const float limit = speed * mass / dt;
        const float scale = drag_sum.magnitude() > limit && drag_sum.magnitude() > 0.0f ? limit / drag_sum.magnitude() : 1.0f;
        for (const Push& push : pushes)
        {
            PxRigidBodyExt::addForceAtPos(*body, push.drag * scale, push.point, PxForceMode::eFORCE, true);
        }

        // tires are buoyed by their air and pushed like paddles, the water they part joins the wake
        for (int i = 0; i < car::wheel_count; i++)
        {
            PxRigidDynamic* wheel = multibody.corners[i].wheel_body;
            if (!wheel)
            {
                continue;
            }

            const float radius_wheel = cfg.wheel_radius_for(i);
            const float width        = cfg.wheel_width_for(i);
            const PxTransform wheel_pose = wheel->getGlobalPose();
            const PxVec3 local       = pose.transformInv(wheel_pose.p);
            const float water_base   = bilinear(corner_heights, (local.x - x0) * inv_width, (local.z - z0) * inv_length) - origin.y;
            const float water        = water_base + state.wake_heights[column_count + i];
            const float depth        = PxClamp(water - (wheel_pose.p.y - radius_wheel), 0.0f, 2.0f * radius_wheel);
            const float depth_base   = PxClamp(water_base - (wheel_pose.p.y - radius_wheel), 0.0f, 2.0f * radius_wheel);
            if (depth <= 0.0f && depth_base <= 0.0f)
            {
                continue;
            }

            const float volume = circle_segment_area(radius_wheel, depth) * width;
            wheel->addForce(PxVec3(0.0f, water_density * gravity * volume * tire_air, 0.0f), PxForceMode::eFORCE, true);

            const PxVec3 wheel_velocity = wheel->getLinearVelocity();
            const PxVec3 horizontal(wheel_velocity.x, 0.0f, wheel_velocity.z);
            PxVec3 drag = -0.5f * water_density * drag_tire * width * depth * horizontal.magnitude() * horizontal;
            const float wheel_mass = PxMax(wheel->getMass(), 1.0f);
            if (drag.magnitude() * dt > (horizontal.magnitude() * (wheel_mass + mass * 0.25f)))
            {
                drag *= horizontal.magnitude() * (wheel_mass + mass * 0.25f) / (drag.magnitude() * dt);
            }
            wheel->addForce(drag, PxForceMode::eFORCE, true);

            // the tire splits the water along its heading, three slices over the wetted chord
            PxVec3 forward = wheel_pose.q.rotate(PxVec3(1.0f, 0.0f, 0.0f)).cross(PxVec3(0.0f, 1.0f, 0.0f));
            forward.y      = 0.0f;
            if (forward.normalize() < 1e-3f)
            {
                forward = PxVec3(0.0f, 0.0f, 1.0f);
            }
            const PxVec3 center      = wheel_pose.p + origin;
            const float chord        = 0.55f * radius_wheel;
            const float volume_base  = circle_segment_area(radius_wheel, depth_base) * width;
            for (int slice = -1; slice <= 1; slice++)
            {
                const PxVec3 at = center + forward * (chord * slice);
                state.wake.push_back({ at.x, at.z, volume_base / 3.0f });
            }
            volume_sum += volume;
        }

        state.submerged = state.hull_volume > 0.0f ? PxClamp(volume_sum / state.hull_volume, 0.0f, 1.0f) : 0.0f;
        if (state.wake.empty())
        {
            leave_water(simulation, state, id, dt);
            return;
        }

        OceanWake::Displace(id, state.wake.data(), static_cast<uint32_t>(state.wake.size()));
        state.in_water = true;
    }
}
