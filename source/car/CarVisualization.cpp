/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#ifdef SP_GAME
#include "CarPhysics.h"
#endif
#include "Car.h"
#include "../profiling/Profiler.h"
#include "../physics/PhysicsWorld.h"
#include "CarHud.h"
#include "CarSimulation.h"
#include "CarDebug.h"
#include "CarEngineSoundSynthesis.h"
#include "CarTireSquealSynthesis.h"
#include "CarSurfaceEffects.h"
#include "../input/Input.h"
#include "../core/Window.h"
#include "../file_system/FileSystem.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include "../resource/ResourceCache.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/AudioSource.h"
#include "../world/components/Camera.h"
#include "../world/components/Light.h"
#include "../world/components/Physics.h"
#include "../world/components/Render.h"
#include "../game/components/CarReset.h"
#include "../world/components/SpawnPoint.h"
#include "../world/Prefab.h"
#include "../io/pugixml.hpp"
#include <mutex>
//==========================================

namespace spartan
{
    namespace
    {
        // structure colors are reserved by nature, load only tints inside the same hue
        const Color skeleton_color_frame       = Color(0.55f, 0.62f, 0.72f, 1.0f);
        const Color skeleton_color_suspension  = Color(0.98f, 0.70f, 0.12f, 1.0f);
        const Color skeleton_color_steering    = Color(0.20f, 0.95f, 0.42f, 1.0f);
        const Color skeleton_color_drivetrain  = Color(0.10f, 0.78f, 1.00f, 1.0f);
        const Color skeleton_color_wheel       = Color(0.78f, 0.82f, 0.88f, 1.0f);
        const Color skeleton_color_collision   = Color(0.72f, 0.28f, 1.00f, 1.0f);
        // telemetry overlays, never used for structure
        const Color skeleton_color_contact     = Color(0.55f, 1.00f, 0.30f, 1.0f);
        const Color skeleton_color_tire_force  = Color(1.00f, 0.30f, 0.68f, 1.0f);
        const Color skeleton_color_long_force  = Color(1.00f, 0.40f, 0.15f, 1.0f);
        const Color skeleton_color_torque      = Color(1.00f, 0.18f, 0.18f, 1.0f);
        const Color skeleton_color_aero        = Color(0.25f, 0.90f, 0.75f, 1.0f);

        auto tint_skeleton_color = [](const Color& base, float load, float lift) -> Color
        {
            const float t = std::clamp(load, 0.0f, 1.0f) * lift;
            return Color(
                std::min(base.r + t * (1.0f - base.r), 1.0f),
                std::min(base.g + t * (1.0f - base.g), 1.0f),
                std::min(base.b + t * (1.0f - base.b), 1.0f),
                base.a);
        };

        math::Vector3 lerp_skeleton(const math::Vector3& a, const math::Vector3& b, float t)
        {
            return a + (b - a) * t;
        }

        void draw_skeleton_joint(const math::Vector3& position, const Color& color)
        {
            Renderer::DrawSphere(position, 0.035f, 6, color);
        }

        void draw_skeleton_cylinder(const math::Vector3& start, const math::Vector3& end, float radius, const Color& color)
        {
            const math::Vector3 axis = end - start;
            const float length = axis.Length();
            if (length <= 0.001f)
            {
                return;
            }

            const math::Vector3 direction = axis / length;
            const math::Vector3 reference = fabsf(direction.y) < 0.9f ? math::Vector3::Up : math::Vector3::Right;
            const math::Vector3 tangent = math::Vector3::Cross(direction, reference).Normalized();
            const math::Vector3 bitangent = math::Vector3::Cross(direction, tangent).Normalized();
            const int segments = 10;
            math::Vector3 previous_start;
            math::Vector3 previous_end;
            for (int i = 0; i <= segments; i++)
            {
                const float angle = static_cast<float>(i) * math::pi * 2.0f / static_cast<float>(segments);
                const math::Vector3 radial = (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                const math::Vector3 start_point = start + radial;
                const math::Vector3 end_point = end + radial;
                if (i > 0)
                {
                    Renderer::DrawLine(previous_start, start_point, color, color);
                    Renderer::DrawLine(previous_end, end_point, color, color);
                }
                if (i % 2 == 0)
                {
                    Renderer::DrawLine(start_point, end_point, color, color);
                }
                previous_start = start_point;
                previous_end = end_point;
            }
        }

        void draw_skeleton_spring(const math::Vector3& start, const math::Vector3& end, const math::Vector3& reference, float radius, const Color& color)
        {
            const math::Vector3 axis = end - start;
            const float length = axis.Length();
            if (length <= 0.001f)
            {
                return;
            }

            const math::Vector3 direction = axis / length;
            math::Vector3 tangent = reference - direction * math::Vector3::Dot(reference, direction);
            if (tangent.LengthSquared() <= 0.0001f)
            {
                tangent = math::Vector3::Right - direction * math::Vector3::Dot(math::Vector3::Right, direction);
            }
            tangent.Normalize();
            const math::Vector3 bitangent = math::Vector3::Cross(direction, tangent).Normalized();
            const int segments = 36;
            const float turns  = 6.0f;
            math::Vector3 previous = start;

            for (int i = 0; i <= segments; i++)
            {
                const float t     = static_cast<float>(i) / static_cast<float>(segments);
                const float angle = t * turns * math::pi * 2.0f;
                const float envelope = sinf(t * math::pi);
                const math::Vector3 point = lerp_skeleton(start, end, t) + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius * envelope;
                if (i > 0)
                {
                    Renderer::DrawLine(previous, point, color, color);
                }
                previous = point;
            }
        }

        void draw_skeleton_shaft(const math::Vector3& start, const math::Vector3& end, float radius, float rotation, float twist, const Color& color)
        {
            const math::Vector3 axis = end - start;
            const float length = axis.Length();
            if (length <= 0.001f)
            {
                return;
            }

            const math::Vector3 direction = axis / length;
            const math::Vector3 reference = fabsf(direction.y) < 0.9f ? math::Vector3::Up : math::Vector3::Right;
            const math::Vector3 tangent   = math::Vector3::Cross(direction, reference).Normalized();
            const math::Vector3 bitangent = math::Vector3::Cross(direction, tangent).Normalized();
            const int radial_segments = 12;
            const int length_segments = 8;

            for (int length_index = 0; length_index <= length_segments; length_index++)
            {
                const float t = static_cast<float>(length_index) / static_cast<float>(length_segments);
                const math::Vector3 center = lerp_skeleton(start, end, t);
                math::Vector3 previous;
                for (int radial_index = 0; radial_index <= radial_segments; radial_index++)
                {
                    const float angle = rotation + twist * t + static_cast<float>(radial_index) * math::pi * 2.0f / static_cast<float>(radial_segments);
                    const math::Vector3 point = center + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                    if (radial_index > 0)
                    {
                        Renderer::DrawLine(previous, point, color, color);
                    }
                    previous = point;
                }
            }

            for (int stripe_index = 0; stripe_index < 4; stripe_index++)
            {
                math::Vector3 previous;
                for (int length_index = 0; length_index <= length_segments; length_index++)
                {
                    const float t = static_cast<float>(length_index) / static_cast<float>(length_segments);
                    const float angle = rotation + twist * t + static_cast<float>(stripe_index) * math::pi * 0.5f;
                    const math::Vector3 point = lerp_skeleton(start, end, t) + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                    if (length_index > 0)
                    {
                        Renderer::DrawLine(previous, point, color, color);
                    }
                    previous = point;
                }
            }

            draw_skeleton_joint(start, color);
            draw_skeleton_joint(end, color);
            Renderer::DrawLine(start - tangent * radius * 1.7f, start + tangent * radius * 1.7f, color, color);
            Renderer::DrawLine(start - bitangent * radius * 1.7f, start + bitangent * radius * 1.7f, color, color);
            Renderer::DrawLine(end - tangent * radius * 1.7f, end + tangent * radius * 1.7f, color, color);
            Renderer::DrawLine(end - bitangent * radius * 1.7f, end + bitangent * radius * 1.7f, color, color);
        }

        Color get_skeleton_tire_temperature_color(float temperature, float wear, const ::car::car_preset& preset)
        {
            const float ambient = preset.tire_ambient_temp;
            const float optimal = std::max(preset.tire_optimal_temp, ambient + 1.0f);
            const float maximum = std::max(preset.tire_max_temp, optimal + 1.0f);
            const float brightness = 1.0f - std::clamp(wear, 0.0f, 1.0f) * 0.55f;
            if (temperature <= optimal)
            {
                const float t = std::clamp((temperature - ambient) / (optimal - ambient), 0.0f, 1.0f);
                return Color((0.12f + t * 0.18f) * brightness, (0.42f + t * 0.58f) * brightness, (1.0f - t * 0.65f) * brightness, 1.0f);
            }
            const float t = std::clamp((temperature - optimal) / (maximum - optimal), 0.0f, 1.0f);
            return Color((0.30f + t * 0.70f) * brightness, (1.0f - t * 0.82f) * brightness, (0.35f - t * 0.25f) * brightness, 1.0f);
        }

        void draw_skeleton_torque_arc(const math::Vector3& center, const math::Vector3& axis_input, float radius, float normalized_torque, const Color& color)
        {
            if (fabsf(normalized_torque) < 0.001f || axis_input.LengthSquared() < 0.0001f)
            {
                return;
            }
            const math::Vector3 axis = axis_input.Normalized();
            const math::Vector3 reference = fabsf(axis.y) < 0.9f ? math::Vector3::Up : math::Vector3::Right;
            const math::Vector3 tangent = math::Vector3::Cross(axis, reference).Normalized();
            const math::Vector3 bitangent = math::Vector3::Cross(axis, tangent).Normalized();
            const float direction = normalized_torque > 0.0f ? 1.0f : -1.0f;
            const float span = std::clamp(fabsf(normalized_torque), 0.0f, 1.0f) * math::pi * 1.5f;
            const int segments = 14;
            math::Vector3 previous = center + tangent * radius;
            for (int segment = 1; segment <= segments; segment++)
            {
                const float angle = direction * span * static_cast<float>(segment) / static_cast<float>(segments);
                const math::Vector3 point = center + (tangent * cosf(angle) + bitangent * sinf(angle)) * radius;
                Renderer::DrawLine(previous, point, color, color);
                previous = point;
            }
            const float end_angle = direction * span;
            const math::Vector3 radial = tangent * cosf(end_angle) + bitangent * sinf(end_angle);
            const math::Vector3 direction_at_end = (-tangent * sinf(end_angle) + bitangent * cosf(end_angle)) * direction;
            Renderer::DrawLine(previous, previous - direction_at_end * radius * 0.28f + radial * radius * 0.16f, color, color);
            Renderer::DrawLine(previous, previous - direction_at_end * radius * 0.28f - radial * radius * 0.16f, color, color);
        }

        // draws one collision shape exactly as physx holds it, every simulated body goes through
        // here so the skeleton can never drift from the geometry the solver is actually using
        template<typename Transform>
        void draw_skeleton_shape(
            const physx::PxTransform& shape_pose,
            const physx::PxGeometry& shape_geometry,
            const Color& color,
            Transform&& to_render
        )
        {
            auto line = [&](const physx::PxVec3& a, const physx::PxVec3& b)
            {
                Renderer::DrawLine(to_render(a), to_render(b), color, color);
            };

            auto ring = [&](
                const physx::PxVec3& center,
                const physx::PxVec3& axis_a,
                const physx::PxVec3& axis_b,
                float radius
            )
            {
                const int segments = 16;
                physx::PxVec3 previous = center + axis_a * radius;
                for (int segment = 1; segment <= segments; segment++)
                {
                    const float angle = static_cast<float>(segment) / static_cast<float>(segments) * math::pi * 2.0f;
                    const physx::PxVec3 point = center + (axis_a * cosf(angle) + axis_b * sinf(angle)) * radius;
                    line(previous, point);
                    previous = point;
                }
            };

            // half of a ring, bulging towards axis_b, which is how a capsule end cap is shaped
            auto arc = [&](
                const physx::PxVec3& center,
                const physx::PxVec3& axis_a,
                const physx::PxVec3& axis_b,
                float radius
            )
            {
                const int segments = 8;
                physx::PxVec3 previous = center + axis_a * radius;
                for (int segment = 1; segment <= segments; segment++)
                {
                    const float angle = static_cast<float>(segment) / static_cast<float>(segments) * math::pi;
                    const physx::PxVec3 point = center + (axis_a * cosf(angle) + axis_b * sinf(angle)) * radius;
                    line(previous, point);
                    previous = point;
                }
            };

            const physx::PxVec3 local_x = shape_pose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f));
            const physx::PxVec3 local_y = shape_pose.q.rotate(physx::PxVec3(0.0f, 1.0f, 0.0f));
            const physx::PxVec3 local_z = shape_pose.q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f));

            switch (shape_geometry.getType())
            {
                case physx::PxGeometryType::eCONVEXMESH:
                {
                    const physx::PxConvexMeshGeometry& geometry = static_cast<const physx::PxConvexMeshGeometry&>(shape_geometry);
                    if (!geometry.convexMesh)
                    {
                        break;
                    }
                    const physx::PxVec3* vertices = geometry.convexMesh->getVertices();
                    const physx::PxU8* indices = geometry.convexMesh->getIndexBuffer();
                    for (physx::PxU32 polygon_index = 0; polygon_index < geometry.convexMesh->getNbPolygons(); polygon_index++)
                    {
                        physx::PxHullPolygon polygon;
                        if (!geometry.convexMesh->getPolygonData(polygon_index, polygon))
                        {
                            continue;
                        }
                        for (physx::PxU32 edge_index = 0; edge_index < polygon.mNbVerts; edge_index++)
                        {
                            const physx::PxU8 index_a = indices[polygon.mIndexBase + edge_index];
                            const physx::PxU8 index_b = indices[polygon.mIndexBase + (edge_index + 1) % polygon.mNbVerts];
                            line(
                                shape_pose.transform(geometry.scale.transform(vertices[index_a])),
                                shape_pose.transform(geometry.scale.transform(vertices[index_b]))
                            );
                        }
                    }
                    break;
                }
                case physx::PxGeometryType::eBOX:
                {
                    const physx::PxBoxGeometry& geometry = static_cast<const physx::PxBoxGeometry&>(shape_geometry);
                    const physx::PxVec3 h = geometry.halfExtents;
                    const physx::PxVec3 vertices[8] = { {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z}, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z} };
                    const int edges[12][2] = { {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7} };
                    for (const auto& edge : edges)
                    {
                        line(shape_pose.transform(vertices[edge[0]]), shape_pose.transform(vertices[edge[1]]));
                    }
                    break;
                }
                case physx::PxGeometryType::eCAPSULE:
                {
                    // a physx capsule runs along its local x, the caps sit at plus and minus the half height
                    const physx::PxCapsuleGeometry& geometry = static_cast<const physx::PxCapsuleGeometry&>(shape_geometry);
                    const physx::PxVec3 cap_a = shape_pose.p - local_x * geometry.halfHeight;
                    const physx::PxVec3 cap_b = shape_pose.p + local_x * geometry.halfHeight;
                    ring(cap_a, local_y, local_z, geometry.radius);
                    ring(cap_b, local_y, local_z, geometry.radius);
                    line(cap_a + local_y * geometry.radius, cap_b + local_y * geometry.radius);
                    line(cap_a - local_y * geometry.radius, cap_b - local_y * geometry.radius);
                    line(cap_a + local_z * geometry.radius, cap_b + local_z * geometry.radius);
                    line(cap_a - local_z * geometry.radius, cap_b - local_z * geometry.radius);
                    arc(cap_a, local_y, -local_x, geometry.radius);
                    arc(cap_a, local_z, -local_x, geometry.radius);
                    arc(cap_b, local_y, local_x, geometry.radius);
                    arc(cap_b, local_z, local_x, geometry.radius);
                    break;
                }
                case physx::PxGeometryType::eSPHERE:
                {
                    const physx::PxSphereGeometry& geometry = static_cast<const physx::PxSphereGeometry&>(shape_geometry);
                    ring(shape_pose.p, local_x, local_y, geometry.radius);
                    ring(shape_pose.p, local_y, local_z, geometry.radius);
                    ring(shape_pose.p, local_z, local_x, geometry.radius);
                    break;
                }
                default:
                {
                    break;
                }
            }
        }

        // a rubber bush is not a ball joint, so it gets a barrel across the load path and a colour that
        // tracks how much of its travel it is using. the deflection is under a millimetre either way
        void draw_skeleton_bushing(physx::PxJoint* joint, const math::Vector3& pivot, const math::Vector3& outboard, float max_deflection)
        {
            float load = 0.0f;
            if (physx::PxD6Joint* bush = joint ? joint->is<physx::PxD6Joint>() : nullptr)
            {
                const physx::PxVec3 offset = bush->getRelativeTransform().p;
                if (std::isfinite(offset.x) && std::isfinite(offset.y) && std::isfinite(offset.z))
                {
                    load = std::clamp(offset.magnitude() / std::max(max_deflection, 0.0001f), 0.0f, 1.0f);
                }
            }

            const Color bush_color = tint_skeleton_color(skeleton_color_suspension, load, 0.40f);
            math::Vector3 along = outboard - pivot;
            if (along.LengthSquared() < 0.000001f)
            {
                draw_skeleton_joint(pivot, bush_color);
                return;
            }
            along.Normalize();
            draw_skeleton_cylinder(pivot - along * 0.022f, pivot + along * 0.022f, 0.030f + load * 0.008f, bush_color);
            Renderer::DrawSphere(pivot, 0.014f, 5, bush_color);
        }

        template<typename Transform>
        void draw_skeleton_actor_shapes(physx::PxRigidActor* actor, const Color& color, Transform&& to_render)
        {
            if (!actor)
            {
                return;
            }

            const physx::PxU32 shape_count = actor->getNbShapes();
            if (shape_count == 0)
            {
                return;
            }

            std::vector<physx::PxShape*> shapes(shape_count);
            actor->getShapes(shapes.data(), shape_count);
            for (physx::PxShape* shape : shapes)
            {
                draw_skeleton_shape(
                    actor->getGlobalPose() * shape->getLocalPose(),
                    shape->getGeometry(),
                    color,
                    to_render
                );
            }
        }

        // capsule endpoints in world space, used so shaft stripes follow the real actor not a guess
        bool get_capsule_endpoints(physx::PxRigidActor* actor, physx::PxVec3& start, physx::PxVec3& end, float& radius)
        {
            if (!actor || actor->getNbShapes() == 0)
            {
                return false;
            }
            physx::PxShape* shape = nullptr;
            actor->getShapes(&shape, 1);
            if (!shape || shape->getGeometry().getType() != physx::PxGeometryType::eCAPSULE)
            {
                return false;
            }
            const physx::PxCapsuleGeometry& geometry =
                static_cast<const physx::PxCapsuleGeometry&>(shape->getGeometry());
            const physx::PxTransform pose = actor->getGlobalPose() * shape->getLocalPose();
            const physx::PxVec3 axis = pose.q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f));
            start = pose.p - axis * geometry.halfHeight;
            end = pose.p + axis * geometry.halfHeight;
            radius = geometry.radius;
            return true;
        }
    }

    void Car::TickVisualization()
    {
        if (
            m_visualization_preset != CarVisualizationPreset::Skeleton ||
            !m_vehicle_entity
        )
        {
            return;
        }

        // keep the painted mesh off every frame, something else can re-enable it after preset change
        if (m_body_render_states.empty())
        {
            ApplySkeletonBodyVisibility();
        }
        else
        {
            bool has_dead = false;
            for (const BodyRenderState& state : m_body_render_states)
            {
                Entity* entity = World::GetEntityById(state.entity_id);
                if (!entity)
                {
                    has_dead = true;
                    continue;
                }

                if (entity->IsActive())
                {
                    entity->SetActive(false);
                }
            }

            // play stop can delete play spawned body meshes while the car object survives
            if (has_dead)
            {
                ClearBodyRenderStates(false);
                ApplySkeletonBodyVisibility();
            }
        }

        Physics* physics = m_vehicle_entity->GetComponent<Physics>();
        if (!physics)
        {
            return;
        }

        ::car::Simulation* simulation = CarPhysics::Get(*physics).GetVehicleSimulation();
        if (!simulation)
        {
            return;
        }
        physx::PxRigidDynamic* body = simulation->get_body();
        if (!body)
        {
            return;
        }

        Entity* wheel_entities[4] =
        {
            CarPhysics::Get(*physics).GetWheelEntity(WheelIndex::FrontLeft),
            CarPhysics::Get(*physics).GetWheelEntity(WheelIndex::FrontRight),
            CarPhysics::Get(*physics).GetWheelEntity(WheelIndex::RearLeft),
            CarPhysics::Get(*physics).GetWheelEntity(WheelIndex::RearRight)
        };

        for (Entity* wheel_entity : wheel_entities)
        {
            if (!wheel_entity)
            {
                return;
            }
        }

        auto from_px = [](const physx::PxVec3& value) { return math::Vector3(value.x, value.y, value.z); };
        auto to_render = [&](const physx::PxVec3& value) { return CarPhysics::Get(*physics).TransformVehiclePointToRender(PhysicsWorld::ToWorldPosition(from_px(value))); };
        if (m_skeleton_show_collision)
        {
            draw_skeleton_actor_shapes(body, skeleton_color_collision, to_render);
        }

        // cheap mode only draws chassis hull plus four wheels
        if (CarPhysics::Get(*physics).GetVehicleSimMode() == VehicleSimMode::Cheap)
        {
            const ::car::config& config = simulation->get_config();
            for (int i = 0; i < 4; i++)
            {
                const float wheel_radius = config.wheel_radius_for(i);
                const float wheel_half_width = config.wheel_width_for(i) * 0.5f;
                const math::Vector3 wheel_center = wheel_entities[i]->GetPosition();
                const math::Quaternion wheel_rotation = wheel_entities[i]->GetRotation();
                const math::Vector3 wheel_axis = wheel_rotation * math::Vector3::Right;
                const math::Vector3 wheel_radial_y = wheel_rotation * math::Vector3::Up;
                const math::Vector3 wheel_radial_z = wheel_rotation * math::Vector3::Forward;
                const math::Vector3 wheel_left = wheel_center - wheel_axis * wheel_half_width;
                const math::Vector3 wheel_right = wheel_center + wheel_axis * wheel_half_width;
                const int wheel_segments = 20;
                math::Vector3 previous_left;
                math::Vector3 previous_right;
                for (int segment = 0; segment <= wheel_segments; segment++)
                {
                    const float angle =
                        static_cast<float>(segment) /
                        static_cast<float>(wheel_segments) *
                        math::pi * 2.0f;
                    const math::Vector3 radial =
                        (wheel_radial_y * cosf(angle) + wheel_radial_z * sinf(angle)) *
                        wheel_radius;
                    const math::Vector3 left = wheel_left + radial;
                    const math::Vector3 right = wheel_right + radial;
                    if (segment > 0)
                    {
                        Renderer::DrawLine(previous_left, left, skeleton_color_wheel, skeleton_color_wheel);
                        Renderer::DrawLine(previous_right, right, skeleton_color_wheel, skeleton_color_wheel);
                        Renderer::DrawLine(left, right, skeleton_color_wheel, skeleton_color_wheel);
                    }
                    previous_left = left;
                    previous_right = right;
                }
            }
            return;
        }

        const ::car::config& config = simulation->get_config();
        const ::car::car_preset& preset = simulation->get_spec();
        const ::car::multibody_state& multibody = simulation->get_multibody_state();
        if (!simulation->has_multibody())
        {
            return;
        }

        const math::Vector3 vehicle_position = m_vehicle_entity->GetPosition();
        const math::Quaternion vehicle_rotation = m_vehicle_entity->GetRotation();
        auto to_world = [&](const math::Vector3& local) { return vehicle_position + vehicle_rotation * local; };

        math::Vector3 wheel_local[4];
        math::Vector3 wheel_world[4];
        math::Vector3 shock_top_world[4];
        math::Vector3 shock_bottom_world[4];
        for (int i = 0; i < 4; i++)
        {
            const ::car::suspension_corner& corner = multibody.corners[i];
            const ::car::wheel& wheel = simulation->get_wheel_state(i);
            if (!corner.upright || !corner.wheel_body)
            {
                return;
            }

            const physx::PxTransform wheel_pose = corner.wheel_body->getGlobalPose();
            wheel_world[i] = to_render(wheel_pose.p);
            wheel_local[i] = vehicle_rotation.Conjugate() * (wheel_world[i] - vehicle_position);
            const float wheel_radius = config.wheel_radius_for(i);
            const float wheel_half_width = config.wheel_width_for(i) * 0.5f;
            // Use the wheel actor, so bearing-axis error is visible instead of
            // replacing the wheel plane with the upright's expected plane.
            const physx::PxQuat query_rotation = wheel_pose.q;
            const physx::PxVec3 wheel_axis = query_rotation.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f));
            const physx::PxVec3 wheel_radial_y = query_rotation.rotate(physx::PxVec3(0.0f, 1.0f, 0.0f));
            const physx::PxVec3 wheel_radial_z = query_rotation.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f));
            const physx::PxVec3 wheel_left = wheel_pose.p - wheel_axis * wheel_half_width;
            const physx::PxVec3 wheel_right = wheel_pose.p + wheel_axis * wheel_half_width;
            const int wheel_segments = 20;
            physx::PxVec3 previous_left;
            physx::PxVec3 previous_right;
            physx::PxVec3 previous_effective;
            physx::PxVec3 previous_temperature[3];
            const float inside_direction = i == 0 || i == 2 ? 1.0f : -1.0f;
            const float zone_offset[3] = { inside_direction * wheel_half_width * 0.66f, 0.0f, -inside_direction * wheel_half_width * 0.66f };
            const Color zone_color[3] = { get_skeleton_tire_temperature_color(wheel.thermal.surface[0], wheel.wear, preset), get_skeleton_tire_temperature_color(wheel.thermal.surface[1], wheel.wear, preset), get_skeleton_tire_temperature_color(wheel.thermal.surface[2], wheel.wear, preset) };
            for (int segment = 0; segment <= wheel_segments; segment++)
            {
                const float angle = static_cast<float>(segment) / static_cast<float>(wheel_segments) * math::pi * 2.0f;
                const physx::PxVec3 radial = (wheel_radial_y * cosf(angle) + wheel_radial_z * sinf(angle)) * wheel_radius;
                const physx::PxVec3 effective_point = wheel_pose.p + (wheel_radial_y * cosf(angle) + wheel_radial_z * sinf(angle)) * wheel.effective_radius;
                const physx::PxVec3 left = wheel_left + radial;
                const physx::PxVec3 right = wheel_right + radial;
                const physx::PxVec3 temperature_point[3] = { wheel_pose.p + wheel_axis * zone_offset[0] + radial, wheel_pose.p + wheel_axis * zone_offset[1] + radial, wheel_pose.p + wheel_axis * zone_offset[2] + radial };
                if (segment > 0)
                {
                    Renderer::DrawLine(to_render(previous_left), to_render(left), skeleton_color_wheel, skeleton_color_wheel);
                    Renderer::DrawLine(to_render(previous_right), to_render(right), skeleton_color_wheel, skeleton_color_wheel);
                    Renderer::DrawLine(to_render(previous_effective), to_render(effective_point), skeleton_color_contact, skeleton_color_contact);
                    for (int zone = 0; zone < 3; zone++)
                    {
                        Renderer::DrawLine(to_render(previous_temperature[zone]), to_render(temperature_point[zone]), zone_color[zone], zone_color[zone]);
                    }
                }
                if (segment % 4 == 0)
                {
                    Renderer::DrawLine(to_render(left), to_render(right), skeleton_color_wheel, skeleton_color_wheel);
                }
                previous_left = left;
                previous_right = right;
                previous_effective = effective_point;
                for (int zone = 0; zone < 3; zone++)
                {
                    previous_temperature[zone] = temperature_point[zone];
                }
            }

            draw_skeleton_cylinder(to_render(wheel_left), to_render(wheel_right), 0.045f, skeleton_color_wheel);
            const float brake_radius = wheel_radius * 0.62f;
            const float brake_temperature_range = std::max(preset.brake_fade_temp - preset.brake_ambient_temp, 1.0f);
            const float brake_heat = std::clamp((wheel.brake_temp - preset.brake_ambient_temp) / brake_temperature_range, 0.0f, 1.0f);
            const float abs_flash = simulation->is_abs_active(i) ? 0.35f : 0.0f;
            const Color brake_color = Color(1.0f, std::min(0.16f + brake_heat * 0.62f + abs_flash, 1.0f), 0.10f + abs_flash, 1.0f);
            physx::PxVec3 previous_brake;
            for (int segment = 0; segment <= 16; segment++)
            {
                const float angle = static_cast<float>(segment) / 16.0f * math::pi * 2.0f;
                const physx::PxVec3 brake_point = wheel_pose.p + (wheel_radial_y * cosf(angle) + wheel_radial_z * sinf(angle)) * brake_radius;
                if (segment > 0)
                {
                    Renderer::DrawLine(to_render(previous_brake), to_render(brake_point), brake_color, brake_color);
                }
                previous_brake = brake_point;
            }
            const physx::PxVec3 spin_marker = wheel_pose.q.rotate(physx::PxVec3(0.0f, wheel_radius * 0.9f, 0.0f));
            Renderer::DrawLine(wheel_world[i], to_render(wheel_pose.p + spin_marker), skeleton_color_wheel, skeleton_color_wheel);
            const math::Vector3 wheel_axis_render = (to_render(wheel_pose.p + wheel_axis) - wheel_world[i]).Normalized();
            const float wheel_torque_reference = std::max(preset.handbrake_torque + preset.brake_force * wheel_radius, 1.0f);
            draw_skeleton_torque_arc(wheel_world[i], wheel_axis_render, wheel_radius * 0.72f, wheel.net_torque / wheel_torque_reference, skeleton_color_torque);
            Renderer::DrawSphere(wheel_world[i], 0.052f, 7, get_skeleton_tire_temperature_color(wheel.thermal.core, wheel.wear, preset));
            const bool is_front_wheel = i < 2;
            const float axle_brake_share = is_front_wheel ? preset.brake_bias_front : 1.0f - preset.brake_bias_front;
            const float wheel_radius_for_brake = config.wheel_radius_for(i);
            const float applied_brake_torque = fabsf(wheel.force_debug.brake_torque);
            const float brake_reference = std::max(preset.brake_force * wheel_radius_for_brake * axle_brake_share * 0.5f + (!is_front_wheel ? preset.handbrake_torque : 0.0f), 1.0f);
            const float brake_actuation = std::clamp(applied_brake_torque / brake_reference, 0.0f, 1.0f);
            const float caliper_size = 0.050f + brake_actuation * 0.016f;
            const physx::PxTransform upright_pose = corner.upright->getGlobalPose();
            Renderer::DrawSphere(to_render(upright_pose.transform(physx::PxVec3(0, brake_radius * 0.72f, brake_radius * 0.45f))), caliper_size, 6, brake_color);

            // the upright box is sized from the ball joint spread, drawing a fixed length rod here
            // hid every geometry change the preset made to it
            draw_skeleton_actor_shapes(corner.upright, skeleton_color_suspension, to_render);
            draw_skeleton_joint(to_render(upright_pose.transform(corner.upright_shock_anchor)), skeleton_color_suspension);

            // the wheel carries a sphere for its rotational inertia, it is not the tyre outline and
            // seeing the two apart is the only way to tell the collision proxy from the visual radius
            draw_skeleton_actor_shapes(corner.wheel_body, skeleton_color_wheel, to_render);

            physx::PxRigidDynamic* drawn_members[::car::max_suspension_members] = {};
            int drawn_member_count = 0;
            for (int member_index = 0; member_index < corner.member_count; member_index++)
            {
                const ::car::suspension_member& member = corner.members[member_index];
                if (!member.actor)
                {
                    continue;
                }
                const physx::PxTransform member_pose = member.actor->getGlobalPose();
                const math::Vector3 start = to_render(member_pose.transform(member.local_start));
                const math::Vector3 end = to_render(member_pose.transform(member.local_end));
                const bool tie_rod = is_front_wheel && multibody.rack && member_index == corner.member_count - 1;
                const Color& member_color = tie_rod ? skeleton_color_steering : skeleton_color_suspension;

                // a wishbone registers its single arm under two members, one per inner pivot, so
                // the shape has to be drawn once while both pivots still get their own marker
                bool already_drawn = false;
                for (int drawn = 0; drawn < drawn_member_count; drawn++)
                {
                    if (drawn_members[drawn] == member.actor)
                    {
                        already_drawn = true;
                        break;
                    }
                }

                if (!already_drawn)
                {
                    drawn_members[drawn_member_count++] = member.actor;
                    // a wishbone is a box and a link is a capsule, both were drawn as one thin rod
                    draw_skeleton_actor_shapes(member.actor, member_color, to_render);
                }

                // outboard is always a bearing, inboard is a rubber bush unless the preset disabled it
                if (member.pivot_is_bushing && member.pivot_joint)
                {
                    physx::PxTransform fixed, moving;
                    if (::car::joint_world_frames(member.pivot_joint, fixed, moving))
                    {
                        draw_skeleton_bushing(member.pivot_joint, to_render(fixed.p), end, preset.bushing_max_deflection);
                        Renderer::DrawLine(to_render(fixed.p), to_render(moving.p), skeleton_color_torque, skeleton_color_torque);
                        draw_skeleton_joint(to_render(moving.p), member_color);
                    }
                }
                else
                {
                    draw_skeleton_joint(start, tie_rod ? skeleton_color_steering : skeleton_color_suspension);
                }
                draw_skeleton_joint(end, tie_rod ? skeleton_color_steering : skeleton_color_suspension);
            }

            const math::Vector3 shock_top = to_render(body->getGlobalPose().transform(corner.chassis_shock_anchor));
            const math::Vector3 shock_bottom = to_render(upright_pose.transform(corner.upright_shock_anchor));
            shock_top_world[i] = shock_top;
            shock_bottom_world[i] = shock_bottom;
            const math::Vector3 shock_mid = lerp_skeleton(shock_top, shock_bottom, 0.48f);
            const float suspension_force = simulation->get_wheel_suspension_force(i);
            const float spring_load = std::clamp(fabsf(suspension_force) / std::max(preset.max_susp_force, 1.0f), 0.0f, 1.0f);
            const float damper_velocity = fabsf(corner.shock_velocity);
            const float damper_load = std::clamp(damper_velocity / std::max(preset.max_damper_velocity, 0.1f), 0.0f, 1.0f);
            const Color spring_color = tint_skeleton_color(skeleton_color_suspension, spring_load, 0.45f);
            const Color damper_color = tint_skeleton_color(skeleton_color_suspension, damper_load, 0.25f);
            const ::car::coilover& coilover_unit = corner.coilover_unit;
            if (coilover_unit.tube && coilover_unit.rod)
            {
                draw_skeleton_actor_shapes(coilover_unit.tube, damper_color, to_render);
                draw_skeleton_actor_shapes(coilover_unit.rod, skeleton_color_suspension, to_render);
                draw_skeleton_spring(shock_top, shock_bottom, vehicle_rotation * math::Vector3::Forward, 0.055f, spring_color);
                draw_skeleton_joint(shock_top, skeleton_color_suspension);
                draw_skeleton_joint(shock_bottom, skeleton_color_suspension);
            }
            else
            {
                draw_skeleton_cylinder(shock_top, shock_mid, 0.030f, damper_color);
                Renderer::DrawLine(shock_mid, shock_bottom, skeleton_color_suspension, skeleton_color_suspension);
                draw_skeleton_spring(shock_top, shock_bottom, vehicle_rotation * math::Vector3::Forward, 0.055f, spring_color);
                draw_skeleton_joint(shock_top, skeleton_color_suspension);
                draw_skeleton_joint(shock_bottom, skeleton_color_suspension);
            }
            const math::Vector3 shock_axis = (shock_top - shock_bottom).Normalized();
            const math::Vector3 spring_force_vector = shock_axis * (suspension_force * 0.00001f);
            Renderer::DrawLine(shock_top, shock_top + spring_force_vector, spring_color, spring_color);
            Renderer::DrawLine(shock_bottom, shock_bottom - spring_force_vector, spring_color, spring_color);
            // the shock carries three separate stages and only the first was ever drawn, so a car sitting
            // on its packers looked identical to one riding on its springs
            const float current_compression = corner.shock_design_length - corner.shock_length;
            const float shock_travel = config.suspension_travel * corner.design_motion_ratio;
            if (current_compression > shock_travel * preset.bump_stop_threshold)
            {
                Renderer::DrawSphere(shock_bottom, 0.065f, 8, skeleton_color_torque);
            }
            if (current_compression > shock_travel * preset.packer_threshold)
            {
                Renderer::DrawSphere(shock_bottom, 0.088f, 9, skeleton_color_long_force);
            }

            // the distance joint is a hard backstop on droop and bump that no spring force reveals
            if (corner.travel_joint)
            {
                const float travel_length = corner.travel_joint->getDistance();
                const float minimum = corner.travel_joint->getMinDistance();
                const float maximum = corner.travel_joint->getMaxDistance();
                const float band = std::max((maximum - minimum) * 0.06f, 0.002f);
                const bool at_droop = travel_length > maximum - band;
                const bool at_bump  = travel_length < minimum + band;
                const Color travel_color = at_droop || at_bump ? skeleton_color_torque : skeleton_color_suspension;
                // the two ends of the allowed band, drawn along the shock so the remaining travel is visible
                const math::Vector3 droop_mark = shock_top + shock_axis * -maximum;
                const math::Vector3 bump_mark  = shock_top + shock_axis * -minimum;
                Renderer::DrawLine(droop_mark, bump_mark, travel_color, travel_color);
                Renderer::DrawSphere(droop_mark, at_droop ? 0.034f : 0.018f, 5, travel_color);
                Renderer::DrawSphere(bump_mark, at_bump ? 0.034f : 0.018f, 5, travel_color);
            }

            // the steering lock, a twist limit on the upright about the chassis vertical. running out of
            // angle produced no visual at all before, the wheel simply stopped turning
            if (corner.steering_stop && corner.steering_limit > 0.0f)
            {
                const float twist = corner.steering_stop->getTwistAngle();
                const float lock = std::clamp(fabsf(twist) / corner.steering_limit, 0.0f, 1.0f);
                const Color lock_color = lock > 0.98f ? skeleton_color_torque : skeleton_color_steering;
                const math::Vector3 steering_axis = vehicle_rotation * math::Vector3::Up;
                draw_skeleton_torque_arc(wheel_world[i], steering_axis, wheel_radius * 0.86f, twist / corner.steering_limit, lock_color);
                if (lock > 0.98f)
                {
                    Renderer::DrawSphere(wheel_world[i], wheel_radius * 0.30f, 8, lock_color);
                }
            }
            physx::PxVec3 sweep_origin;
            physx::PxVec3 sweep_endpoint;
            bool sweep_hit = false;
            simulation->get_debug_sweep(i, sweep_origin, sweep_endpoint, sweep_hit);
            const Color& sweep_color = sweep_hit ? skeleton_color_contact : skeleton_color_collision;
            Renderer::DrawLine(to_render(sweep_origin), to_render(sweep_endpoint), sweep_color, sweep_color);
            Renderer::DrawSphere(to_render(sweep_endpoint), 0.025f, 6, sweep_color);

            // the tread rows the contact model actually loaded, the stalk length is that row's share of
            // the load so an uneven patch from camber or a kerb edge is visible rather than inferred
            const int contact_rows = simulation->get_debug_contact_rows(i);
            physx::PxVec3 normal_force(0);
            for (int row = 0; row < contact_rows; row++)
            {
                physx::PxVec3 row_point;
                physx::PxVec3 row_normal;
                float row_load = 0.0f;
                simulation->get_debug_contact_row(i, row, row_point, row_normal, row_load);
                normal_force += row_normal * row_load;
                const math::Vector3 row_base = to_render(row_point);
                const math::Vector3 row_tip  = to_render(row_point + row_normal * (row_load * 0.00006f));
                Renderer::DrawLine(row_base, row_tip, skeleton_color_contact, skeleton_color_contact);
                Renderer::DrawSphere(row_base, 0.012f, 5, skeleton_color_contact);
            }
            if (wheel.grounded)
            {
                physx::PxVec3 wheel_forward = wheel_axis.cross(wheel.contact_normal);
                if (wheel_forward.normalize() < 0.0001f)
                {
                    wheel_forward = upright_pose.q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f));
                }
                const physx::PxVec3 normal_endpoint = wheel.contact_point + normal_force * 0.00002f;
                const auto& forces = wheel.force_debug;
                const physx::PxVec3 longitudinal_endpoint = forces.tire_point + forces.longitudinal * 0.00002f;
                const physx::PxVec3 lateral_endpoint = forces.tire_point + forces.lateral * 0.00002f;
                const physx::PxVec3 rolling_endpoint = forces.rolling_point + forces.rolling * 0.00004f;
                const math::Vector3 contact = to_render(wheel.contact_point);
                Color contact_color = skeleton_color_contact;
                if (wheel.contact_surface == ::car::surface_gravel || wheel.contact_surface == ::car::surface_dirt)
                {
                    contact_color = Color(0.76f, 0.56f, 0.28f, 1.0f);
                }
                else if (wheel.contact_surface == ::car::surface_grass)
                {
                    contact_color = Color(0.18f, 0.72f, 0.18f, 1.0f);
                }
                else if (wheel.contact_surface == ::car::surface_ice)
                {
                    contact_color = Color(0.65f, 0.90f, 1.00f, 1.0f);
                }
                else if (wheel.contact_surface == ::car::surface_wet_asphalt)
                {
                    contact_color = Color(0.25f, 0.48f, 1.00f, 1.0f);
                }
                if (wheel.contact_actor && wheel.contact_actor->is<physx::PxRigidDynamic>())
                {
                    contact_color = Color(1.00f, 0.90f, 0.20f, 1.0f);
                }
                Renderer::DrawSphere(contact, 0.045f, 8, contact_color);
                Renderer::DrawLine(contact, to_render(normal_endpoint), skeleton_color_contact, skeleton_color_contact);
                Renderer::DrawLine(to_render(forces.tire_point), to_render(longitudinal_endpoint), skeleton_color_long_force, skeleton_color_long_force);
                Renderer::DrawLine(to_render(forces.tire_point), to_render(lateral_endpoint), skeleton_color_tire_force, skeleton_color_tire_force);
                Renderer::DrawLine(to_render(forces.rolling_point), to_render(rolling_endpoint), skeleton_color_aero, skeleton_color_aero);
                // the trail comes from the simulation, recomputing it here drew the curve fit result
                // even when the brush model was the one steering the car
                const float trail = simulation->get_wheel_pneumatic_trail(i);
                const float aligning_torque = simulation->get_wheel_self_aligning_torque(i);
                const math::Vector3 contact_normal_render = (to_render(wheel.contact_point + wheel.contact_normal) - contact).Normalized();
                draw_skeleton_torque_arc(wheel_world[i], contact_normal_render, wheel_radius * 0.48f, aligning_torque / 500.0f, skeleton_color_steering);

                // the patch the brush model derived from carcass deflection, and the point inside it where
                // the lateral force actually acts. both are outputs of the tire model that nothing showed
                const float patch_half = wheel.contact_patch_length * 0.5f;
                if (patch_half > 0.0f)
                {
                    Renderer::DrawLine(to_render(wheel.contact_point - wheel_forward * patch_half), to_render(wheel.contact_point + wheel_forward * patch_half), skeleton_color_contact, skeleton_color_contact);
                }
                Renderer::DrawSphere(to_render(wheel.contact_point - wheel_forward * trail), 0.018f, 5, skeleton_color_steering);
            }
        }

        // Draw both solved anchors of joints that should coincide. Separate
        // markers and an error colour expose loose bearings and disconnected links.
        for (int i = 0; i < multibody.joint_count; ++i)
        {
            const physx::PxJoint* joint = multibody.joints[i];
            if (!joint || (!joint->is<physx::PxSphericalJoint>() && !joint->is<physx::PxRevoluteJoint>() && !joint->is<physx::PxFixedJoint>())) continue;
            physx::PxTransform a, b;
            if (!::car::joint_world_frames(joint, a, b)) continue;
            const bool separated = (a.p - b.p).magnitude() > 0.001f;
            const Color& color = separated ? skeleton_color_torque : skeleton_color_suspension;
            Renderer::DrawLine(to_render(a.p), to_render(b.p), color, color);
            Renderer::DrawSphere(to_render(a.p), separated ? 0.024f : 0.012f, 5, color);
            Renderer::DrawSphere(to_render(b.p), 0.012f, 5, skeleton_color_steering);
            if (joint->is<physx::PxRevoluteJoint>())
            {
                Renderer::DrawLine(to_render(a.p), to_render(a.transform(physx::PxVec3(0.12f, 0, 0))), color, color);
                Renderer::DrawLine(to_render(b.p), to_render(b.transform(physx::PxVec3(0.12f, 0, 0))), skeleton_color_wheel, skeleton_color_wheel);
            }
        }

        const float front_z = (wheel_local[0].z + wheel_local[1].z) * 0.5f;
        const float rear_z  = (wheel_local[2].z + wheel_local[3].z) * 0.5f;
        const float frame_y = 0.04f;
        const float front_frame_half_width = config.track_front * 0.30f;
        const float rear_frame_half_width = config.track_rear * 0.30f;
        const math::Vector3 frame_front_left = to_world(math::Vector3(-front_frame_half_width, frame_y, front_z));
        const math::Vector3 frame_front_right = to_world(math::Vector3(front_frame_half_width, frame_y, front_z));
        const math::Vector3 frame_rear_left = to_world(math::Vector3(-rear_frame_half_width, frame_y, rear_z));
        const math::Vector3 frame_rear_right = to_world(math::Vector3(rear_frame_half_width, frame_y, rear_z));
        const math::Vector3 frame_center_left = lerp_skeleton(frame_front_left, frame_rear_left, 0.5f);
        const math::Vector3 frame_center_right = lerp_skeleton(frame_front_right, frame_rear_right, 0.5f);
        draw_skeleton_cylinder(frame_front_left, frame_rear_left, 0.025f, skeleton_color_frame);
        draw_skeleton_cylinder(frame_front_right, frame_rear_right, 0.025f, skeleton_color_frame);
        draw_skeleton_cylinder(frame_front_left, frame_front_right, 0.025f, skeleton_color_frame);
        draw_skeleton_cylinder(frame_center_left, frame_center_right, 0.025f, skeleton_color_frame);
        draw_skeleton_cylinder(frame_rear_left, frame_rear_right, 0.025f, skeleton_color_frame);
        Renderer::DrawLine(frame_front_left, frame_rear_right, skeleton_color_frame, skeleton_color_frame);
        Renderer::DrawLine(frame_front_right, frame_rear_left, skeleton_color_frame, skeleton_color_frame);
        draw_skeleton_cylinder(frame_front_left, shock_top_world[0], 0.018f, skeleton_color_frame);
        draw_skeleton_cylinder(frame_front_right, shock_top_world[1], 0.018f, skeleton_color_frame);
        draw_skeleton_cylinder(frame_rear_left, shock_top_world[2], 0.018f, skeleton_color_frame);
        draw_skeleton_cylinder(frame_rear_right, shock_top_world[3], 0.018f, skeleton_color_frame);

        auto draw_physical_anti_roll = [&](const ::car::anti_roll_bar& arb, int left, int right, float stiffness)
        {
            if (!arb.left_half || !arb.right_half)
            {
                // no physx bar, fall back to the old travel-difference sketch
                if (stiffness <= 0.0f)
                {
                    return;
                }
                const ::car::suspension_corner& left_corner = multibody.corners[left];
                const ::car::suspension_corner& right_corner = multibody.corners[right];
                const float compression_difference =
                    (left_corner.shock_rest_length - left_corner.shock_length)
                    - (right_corner.shock_rest_length - right_corner.shock_length);
                const float anti_roll_load = std::clamp(
                    fabsf(compression_difference * stiffness) / std::max(preset.max_susp_force, 1.0f),
                    0.0f,
                    1.0f);
                const Color loaded = tint_skeleton_color(skeleton_color_suspension, anti_roll_load, 0.40f);
                const math::Vector3 left_arm_end = lerp_skeleton(shock_top_world[left], shock_bottom_world[left], 0.24f);
                const math::Vector3 right_arm_end = lerp_skeleton(shock_top_world[right], shock_bottom_world[right], 0.24f);
                const float anti_roll_twist =
                    compression_difference / std::max(config.suspension_travel, 0.01f) * math::pi;
                draw_skeleton_shaft(shock_top_world[left], shock_top_world[right], 0.020f, 0.0f, anti_roll_twist, loaded);
                draw_skeleton_cylinder(shock_top_world[left], left_arm_end, 0.014f, loaded);
                draw_skeleton_cylinder(shock_top_world[right], right_arm_end, 0.014f, loaded);
                return;
            }

            const ::car::suspension_corner& left_corner = multibody.corners[left];
            const ::car::suspension_corner& right_corner = multibody.corners[right];
            const float compression_difference =
                (left_corner.shock_rest_length - left_corner.shock_length)
                - (right_corner.shock_rest_length - right_corner.shock_length);
            float twist = 0.0f;
            if (arb.torsion_joint)
            {
                const physx::PxQuat relative = arb.torsion_joint->getRelativeTransform().q;
                twist = 2.0f * atanf(relative.x / std::max(relative.w, 1e-6f));
                if (!std::isfinite(twist))
                {
                    twist = 0.0f;
                }
            }
            else
            {
                twist = compression_difference / std::max(arb.arm_length, 0.05f);
            }
            const float anti_roll_load = std::clamp(
                fabsf(compression_difference * stiffness) / std::max(preset.max_susp_force, 1.0f),
                0.0f,
                1.0f);
            const Color loaded = tint_skeleton_color(skeleton_color_suspension, anti_roll_load, 0.40f);

            physx::PxVec3 left_start, left_end, right_start, right_end;
            float left_radius = 0.016f;
            float right_radius = 0.016f;
            const bool left_caps = get_capsule_endpoints(arb.left_half, left_start, left_end, left_radius);
            const bool right_caps = get_capsule_endpoints(arb.right_half, right_start, right_end, right_radius);
            draw_skeleton_actor_shapes(arb.left_half, loaded, to_render);
            draw_skeleton_actor_shapes(arb.right_half, loaded, to_render);

            if (left_caps && right_caps)
            {
                // inboard is the end with smaller chassis local |x|
                const physx::PxTransform pose = body->getGlobalPose();
                auto local_abs_x = [&](const physx::PxVec3& world) -> float
                {
                    return fabsf(pose.transformInv(world).x);
                };
                const physx::PxVec3 left_inboard =
                    local_abs_x(left_start) < local_abs_x(left_end) ? left_start : left_end;
                const physx::PxVec3 left_outboard = left_inboard == left_start ? left_end : left_start;
                const physx::PxVec3 right_inboard =
                    local_abs_x(right_start) < local_abs_x(right_end) ? right_start : right_end;
                const physx::PxVec3 right_outboard = right_inboard == right_start ? right_end : right_start;
                draw_skeleton_shaft(
                    to_render(left_inboard),
                    to_render(right_inboard),
                    std::max(left_radius, right_radius) * 0.85f,
                    0.0f,
                    twist,
                    loaded);
                draw_skeleton_joint(to_render(left_inboard), skeleton_color_suspension);
                draw_skeleton_joint(to_render(right_inboard), skeleton_color_suspension);
                draw_skeleton_joint(to_render(left_outboard), loaded);
                draw_skeleton_joint(to_render(right_outboard), loaded);
            }

            auto draw_drop = [&](physx::PxRigidDynamic* drop, int corner_index)
            {
                if (!drop)
                {
                    return;
                }
                draw_skeleton_actor_shapes(drop, loaded, to_render);
                physx::PxVec3 drop_start, drop_end;
                float drop_radius = 0.014f;
                if (get_capsule_endpoints(drop, drop_start, drop_end, drop_radius))
                {
                    draw_skeleton_joint(to_render(drop_start), loaded);
                    draw_skeleton_joint(to_render(drop_end), loaded);
                    // load stalk along the link, same units as the old force sketch
                    const math::Vector3 a = to_render(drop_start);
                    const math::Vector3 b = to_render(drop_end);
                    math::Vector3 along = b - a;
                    if (along.LengthSquared() > 1e-6f)
                    {
                        along.Normalize();
                        const float force_scale = anti_roll_load * 0.12f;
                        Renderer::DrawLine(a, a - along * force_scale, loaded, loaded);
                        Renderer::DrawLine(b, b + along * force_scale, loaded, loaded);
                    }
                }
                else
                {
                    draw_skeleton_joint(to_render(drop->getGlobalPose().p), loaded);
                    draw_skeleton_joint(shock_bottom_world[corner_index], loaded);
                }
            };
            draw_drop(arb.left_drop, left);
            draw_drop(arb.right_drop, right);
        };
        draw_physical_anti_roll(multibody.front_arb, 0, 1, preset.front_arb_stiffness);
        draw_physical_anti_roll(multibody.rear_arb, 2, 3, preset.rear_arb_stiffness);

        const physx::PxTransform body_pose = body->getGlobalPose();
        const math::Vector3 center_of_mass = to_render(body_pose.transform(body->getCMassLocalPose().p));
        Renderer::DrawSphere(center_of_mass, 0.075f, 10, skeleton_color_frame);

        // principal inertia axes at the com, length scales with sqrt(i) so yaw vs roll is readable
        {
            const physx::PxVec3 inertia = body->getMassSpaceInertiaTensor();
            const float i_scale = 0.012f / std::max(sqrtf(std::max(std::max(inertia.x, inertia.y), inertia.z)), 1.0f);
            const physx::PxTransform com_pose = body_pose * body->getCMassLocalPose();
            auto draw_inertia_axis = [&](const physx::PxVec3& local_axis, float inertia_value, const Color& color)
            {
                const float half_length = sqrtf(std::max(inertia_value, 1.0f)) * i_scale * 40.0f;
                const physx::PxVec3 world_axis = com_pose.q.rotate(local_axis) * half_length;
                Renderer::DrawLine(
                    to_render(com_pose.p - world_axis),
                    to_render(com_pose.p + world_axis),
                    color,
                    color);
            };
            draw_inertia_axis(physx::PxVec3(1.0f, 0.0f, 0.0f), inertia.x, Color(1.0f, 0.35f, 0.35f, 1.0f));
            draw_inertia_axis(physx::PxVec3(0.0f, 1.0f, 0.0f), inertia.y, Color(0.35f, 1.0f, 0.45f, 1.0f));
            draw_inertia_axis(physx::PxVec3(0.0f, 0.0f, 1.0f), inertia.z, Color(0.35f, 0.65f, 1.0f, 1.0f));
        }
        const ::car::aero_debug_data& aero_debug = simulation->get_aero_debug();
        if (aero_debug.valid)
        {
            auto draw_aero_force = [&](const physx::PxVec3& position, const physx::PxVec3& force)
            {
                const math::Vector3 start = to_render(position);
                Renderer::DrawLine(start, to_render(position + force * 0.00002f), skeleton_color_aero, skeleton_color_aero);
            };
            draw_aero_force(aero_debug.position, aero_debug.drag_force);
            draw_aero_force(aero_debug.front_aero_pos, aero_debug.front_downforce);
            draw_aero_force(aero_debug.rear_aero_pos, aero_debug.rear_downforce);
            draw_aero_force(aero_debug.side_aero_pos, aero_debug.side_force);
        }

        if (multibody.rack)
        {
            // the tie rod ends ride on the bar itself, so the marker span has to come from the
            // shape rather than from a track fraction recomputed here
            const physx::PxTransform rack_pose = multibody.rack->getGlobalPose();
            draw_skeleton_actor_shapes(multibody.rack, skeleton_color_steering, to_render);

            physx::PxShape* rack_shape = nullptr;
            if (multibody.rack->getNbShapes() > 0)
            {
                multibody.rack->getShapes(&rack_shape, 1);
            }

            if (rack_shape && rack_shape->getGeometry().getType() == physx::PxGeometryType::eBOX)
            {
                const float half_width =
                    static_cast<const physx::PxBoxGeometry&>(rack_shape->getGeometry()).halfExtents.x;
                draw_skeleton_joint(to_render(rack_pose.transform(physx::PxVec3(-half_width, 0.0f, 0.0f))), skeleton_color_steering);
                draw_skeleton_joint(to_render(rack_pose.transform(physx::PxVec3(half_width, 0.0f, 0.0f))), skeleton_color_steering);
            }
        }

        const int drivetrain_type = preset.drivetrain_type;
        const bool drives_front = drivetrain_type == 1 || drivetrain_type == 2;
        const bool drives_rear  = drivetrain_type == 0 || drivetrain_type == 2;
        const float axle_y = (wheel_local[0].y + wheel_local[2].y) * 0.5f;
        float gearbox_z = preset.center_of_mass_z;
        if (drives_front && !drives_rear)
        {
            gearbox_z = front_z;
        }
        else if (drives_rear && !drives_front)
        {
            gearbox_z = rear_z;
        }
        {
            const float driven_axle_z = drives_rear ? rear_z : front_z;
            if (fabsf(gearbox_z - driven_axle_z) < 0.08f)
            {
                const float toward_center = (driven_axle_z >= 0.0f) ? -1.0f : 1.0f;
                gearbox_z = driven_axle_z + toward_center * 0.10f;
            }
        }
        const math::Vector3 gearbox = to_world(math::Vector3(0.0f, axle_y, gearbox_z));
        const float driveshaft_torque = simulation->get_driveshaft_torque();
        const float torque_load = std::clamp(fabsf(driveshaft_torque) / 6000.0f, 0.0f, 1.0f);
        const Color loaded_drivetrain_color = tint_skeleton_color(skeleton_color_drivetrain, torque_load, 0.45f);
        const float motor_load = preset.electric_enabled ? std::clamp(fabsf(simulation->get_motor_torque()) / std::max(preset.electric_motor_torque, 1.0f), 0.0f, 1.0f) : 0.0f;
        Color power_unit_color = tint_skeleton_color(skeleton_color_drivetrain, motor_load, 0.35f);
        if (simulation->get_rev_limiter_active())
        {
            power_unit_color = skeleton_color_torque;
        }
        else if (simulation->is_tc_active())
        {
            power_unit_color = skeleton_color_long_force;
        }
        else if (simulation->get_is_shifting())
        {
            power_unit_color = tint_skeleton_color(skeleton_color_drivetrain, 1.0f, 0.55f);
        }

        const math::Vector3 flywheel_axis = vehicle_rotation * math::Vector3::Right * 0.10f;
        draw_skeleton_shaft(gearbox - flywheel_axis, gearbox + flywheel_axis, 0.075f + simulation->get_clutch() * 0.015f, simulation->get_engine_rotation(), 0.0f, power_unit_color);
        Renderer::DrawSphere(gearbox, 0.10f, 8, power_unit_color);

        const ::car::driveline_assembly& driveline = multibody.driveline;
        auto is_driveline_actor = [&](physx::PxRigidDynamic* actor) -> bool
        {
            if (!actor)
            {
                return false;
            }
            if (actor == driveline.gearbox_output || actor == driveline.axle_input)
            {
                return true;
            }
            for (int i = 0; i < driveline.propshaft_count; i++)
            {
                if (actor == driveline.propshaft[i])
                {
                    return true;
                }
            }
            for (int i = 0; i < driveline.differential_count; i++)
            {
                if (actor == driveline.differential[i])
                {
                    return true;
                }
            }
            for (int i = 0; i < 4; i++)
            {
                if (actor == driveline.halfshaft[i])
                {
                    return true;
                }
            }
            return false;
        };
        auto is_arb_actor = [&](physx::PxRigidDynamic* actor) -> bool
        {
            auto match = [&](const ::car::anti_roll_bar& arb)
            {
                return actor == arb.left_half || actor == arb.right_half || actor == arb.left_drop || actor == arb.right_drop;
            };
            return match(multibody.front_arb) || match(multibody.rear_arb);
        };

        const float driveshaft_twist = simulation->get_driveshaft_twist();
        auto wheel_drivetrain_color = [&](int wheel_index)
        {
            const float wheel_torque_load = std::clamp(
                fabsf(simulation->get_wheel_state(wheel_index).drive_torque) / 6000.0f,
                0.0f,
                1.0f);
            return tint_skeleton_color(skeleton_color_drivetrain, wheel_torque_load, 0.45f);
        };
        auto draw_spinning_capsule = [&](
            physx::PxRigidDynamic* actor,
            const Color& color
        )
        {
            if (!actor)
            {
                return;
            }
            physx::PxVec3 start, end;
            float radius = 0.04f;
            draw_skeleton_actor_shapes(actor, color, to_render);
            if (get_capsule_endpoints(actor, start, end, radius))
            {
                physx::PxShape* shape = nullptr;
                actor->getShapes(&shape, 1);
                const physx::PxTransform pose = actor->getGlobalPose() * shape->getLocalPose();
                const physx::PxVec3 radial = pose.q.rotate(physx::PxVec3(0, radius, 0));
                // A material stripe follows the actual actor frame. No invented
                // wheel-speed rotation, torque twist, or enlarged collision radius.
                Renderer::DrawLine(to_render(start + radial), to_render(end + radial), skeleton_color_wheel, skeleton_color_wheel);
            }
        };

        if (driveline.initialized)
        {
            if (driveline.gearbox_output)
            {
                draw_skeleton_actor_shapes(driveline.gearbox_output, power_unit_color, to_render);
                const physx::PxVec3 flange_axis =
                    driveline.gearbox_output->getGlobalPose().q.rotate(physx::PxVec3(1.0f, 0.0f, 0.0f));
                const math::Vector3 flange_center = to_render(driveline.gearbox_output->getGlobalPose().p);
                const math::Vector3 flange_axis_render =
                    (to_render(driveline.gearbox_output->getGlobalPose().p + flange_axis) - flange_center).Normalized();
                draw_skeleton_torque_arc(
                    flange_center,
                    flange_axis_render,
                    0.11f,
                    driveshaft_torque / 6000.0f,
                    loaded_drivetrain_color);
                Renderer::DrawSphere(flange_center, 0.055f, 7, power_unit_color);
            }
            if (driveline.axle_input)
            {
                draw_skeleton_actor_shapes(driveline.axle_input, loaded_drivetrain_color, to_render);
                Renderer::DrawSphere(
                    to_render(driveline.axle_input->getGlobalPose().p),
                    0.045f,
                    7,
                    loaded_drivetrain_color);
            }

            for (int i = 0; i < driveline.propshaft_count; i++)
            {
                physx::PxRigidDynamic* prop = driveline.propshaft[i];
                if (!prop)
                {
                    continue;
                }
                draw_spinning_capsule(prop, loaded_drivetrain_color);
            }

            for (int i = 0; i < driveline.differential_count; i++)
            {
                physx::PxRigidDynamic* differential = driveline.differential[i];
                if (!differential)
                {
                    continue;
                }
                draw_skeleton_actor_shapes(differential, skeleton_color_drivetrain, to_render);
                Renderer::DrawSphere(
                    to_render(differential->getGlobalPose().p),
                    0.09f,
                    8,
                    skeleton_color_drivetrain);
            }

            for (int wheel_index = 0; wheel_index < 4; wheel_index++)
            {
                physx::PxRigidDynamic* shaft = driveline.halfshaft[wheel_index];
                if (!shaft)
                {
                    continue;
                }
                const Color shaft_color = wheel_drivetrain_color(wheel_index);
                draw_spinning_capsule(shaft, shaft_color);

                physx::PxVec3 shaft_start, shaft_end;
                float shaft_radius = 0.04f;
                if (get_capsule_endpoints(shaft, shaft_start, shaft_end, shaft_radius))
                {
                    Renderer::DrawSphere(to_render(shaft_start), 0.028f, 6, skeleton_color_drivetrain);
                    Renderer::DrawSphere(to_render(shaft_end), 0.028f, 6, skeleton_color_drivetrain);
                }
            }

            if (driveline.gearbox_output && driveline.axle_input)
            {
                // Reduced-model power flow between mounted flanges, not a
                // physical shaft with the engine's scalar torsional deflection.
                Renderer::DrawLine(
                    to_render(driveline.gearbox_output->getGlobalPose().p),
                    to_render(driveline.axle_input->getGlobalPose().p),
                    loaded_drivetrain_color,
                    loaded_drivetrain_color);
            }
        }
        else
        {
            const math::Vector3 front_diff = to_world(math::Vector3(0.0f, axle_y, front_z));
            const math::Vector3 rear_diff  = to_world(math::Vector3(0.0f, axle_y, rear_z));
            const float front_pinion_rotation =
                (simulation->get_wheel_rotation(0) + simulation->get_wheel_rotation(1)) * 0.5f * preset.final_drive;
            const float rear_pinion_rotation =
                (simulation->get_wheel_rotation(2) + simulation->get_wheel_rotation(3)) * 0.5f * preset.final_drive;
            if (drives_front)
            {
                Renderer::DrawSphere(front_diff, 0.11f, 8, skeleton_color_drivetrain);
                draw_skeleton_shaft(
                    front_diff,
                    wheel_world[0],
                    0.04f,
                    simulation->get_wheel_rotation(0),
                    simulation->get_wheel_state(0).drive_torque * 0.00005f,
                    wheel_drivetrain_color(0));
                draw_skeleton_shaft(
                    front_diff,
                    wheel_world[1],
                    0.04f,
                    simulation->get_wheel_rotation(1),
                    simulation->get_wheel_state(1).drive_torque * 0.00005f,
                    wheel_drivetrain_color(1));
                const float front_shaft_radius =
                    drivetrain_type == 2 ? 0.035f + preset.torque_split_front * 0.04f : 0.055f;
                draw_skeleton_shaft(
                    gearbox,
                    front_diff,
                    front_shaft_radius,
                    front_pinion_rotation,
                    driveshaft_twist,
                    loaded_drivetrain_color);
            }
            if (drives_rear)
            {
                Renderer::DrawSphere(rear_diff, 0.11f, 8, skeleton_color_drivetrain);
                draw_skeleton_shaft(
                    rear_diff,
                    wheel_world[2],
                    0.04f,
                    simulation->get_wheel_rotation(2),
                    simulation->get_wheel_state(2).drive_torque * 0.00005f,
                    wheel_drivetrain_color(2));
                draw_skeleton_shaft(
                    rear_diff,
                    wheel_world[3],
                    0.04f,
                    simulation->get_wheel_rotation(3),
                    simulation->get_wheel_state(3).drive_torque * 0.00005f,
                    wheel_drivetrain_color(3));
                const float rear_shaft_radius =
                    drivetrain_type == 2 ? 0.035f + (1.0f - preset.torque_split_front) * 0.04f : 0.055f;
                draw_skeleton_shaft(
                    gearbox,
                    rear_diff,
                    rear_shaft_radius,
                    rear_pinion_rotation,
                    driveshaft_twist,
                    loaded_drivetrain_color);
            }
        }

        // everything above reaches bodies by name, which silently misses any actor a future mechanism
        // adds. walking the solver's own list means an unhandled body shows up wrong rather than not at all
        for (int actor_index = 0; actor_index < multibody.actor_count; actor_index++)
        {
            physx::PxRigidDynamic* actor = multibody.actors[actor_index];
            if (!actor || actor == multibody.rack || is_driveline_actor(actor) || is_arb_actor(actor))
            {
                continue;
            }

            bool covered = false;
            for (int i = 0; i < 4 && !covered; i++)
            {
                const ::car::suspension_corner& corner = multibody.corners[i];
                covered = actor == corner.upright
                    || actor == corner.wheel_body
                    || actor == corner.coilover_unit.tube
                    || actor == corner.coilover_unit.rod;
                for (int member_index = 0; member_index < corner.member_count && !covered; member_index++)
                {
                    covered = actor == corner.members[member_index].actor;
                }
            }

            if (!covered)
            {
                draw_skeleton_actor_shapes(actor, skeleton_color_collision, to_render);
                Renderer::DrawSphere(to_render(actor->getGlobalPose().p), 0.06f, 7, skeleton_color_torque);
            }
        }
    }
}
