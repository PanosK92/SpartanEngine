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

//= INCLUDES ===============================
#include "pch.h"
#include "Car.h"
#include "CarHud.h"
#include "CarSimulation.h"
#include "CarEngineSoundSynthesis.h"
#include "CarTireSquealSynthesis.h"
#include "../Input/Input.h"
#include "../Core/Window.h"
#include "../FileSystem/FileSystem.h"
#include "../Rendering/Material.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/Physics.h"
#include "../World/Components/Render.h"
#include "../World/Prefab.h"
#include "../IO/pugixml.hpp"
#include <mutex>
//==========================================

namespace spartan
{
    // static member initialization
    std::vector<Car*> Car::s_cars;

    // entities shared with other files (external linkage required)
    // resolved lazily on world load, see the camera discovery in Tick
    Entity* default_camera     = nullptr;
    Entity* default_car        = nullptr;
    Entity* default_car_window = nullptr;

    namespace
    {
        // car prefabs are created from multiple world loading threads, s_cars is shared
        std::mutex car_list_mutex;
        constexpr float car_spawn_margin = 1.0f;

        float get_car_lower_extent(const car::car_preset& preset)
        {
            const float wheel_extent = preset.suspension_height + std::max(preset.front_wheel_radius, preset.rear_wheel_radius);
            return std::max(preset.height * 0.5f, wheel_extent);
        }

        enum class CarMaterialSlot
        {
            Unknown,
            BodyPaint,
            CarbonTrim,
            TireRubber,
            RimMetal,
            HeadlightLens,
            TaillightLens,
            MainGlass,
            MirrorGlass,
            EngineMetal,
            BrakeDisc,
            InteriorLeather,
            BlackTrim,
            EmissiveRedLight,
            EmissiveWhiteLight
        };

        std::string to_lower_copy(std::string value)
        {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return std::tolower(c); });
            return value;
        }

        bool contains(const std::string& value, const char* token)
        {
            return value.find(token) != std::string::npos;
        }

        MaterialPaintPreset parse_paint_preset(const char* value)
        {
            const std::string preset = to_lower_copy(value ? value : "");

            if (preset == "gloss_solid" || preset == "gloss solid" || preset == "solid")
            {
                return MaterialPaintPreset::GlossSolid;
            }

            if (preset == "metallic")
            {
                return MaterialPaintPreset::Metallic;
            }

            if (preset == "satin")
            {
                return MaterialPaintPreset::Satin;
            }

            if (preset == "matte")
            {
                return MaterialPaintPreset::Matte;
            }

            if (preset == "pearl")
            {
                return MaterialPaintPreset::Pearl;
            }

            if (preset == "chameleon")
            {
                return MaterialPaintPreset::Chameleon;
            }

            return MaterialPaintPreset::Metallic;
        }

        void tag_wheel(Entity* wheel, bool is_front, bool is_left)
        {
            wheel->AddTag("wheel");
            wheel->AddTag(is_front ? "wheel_front" : "wheel_rear");
            wheel->AddTag(is_left ? "wheel_left" : "wheel_right");
        }

        // scales prop wheels without a physics component
        void scale_wheel_to_radius(Entity* wheel_entity, float target_radius)
        {
            Render* renderable = wheel_entity->GetComponent<Render>();
            if (!renderable || !std::isfinite(target_radius) || target_radius <= 0.0f)
            {
                return;
            }

            const math::Vector3 extents  = renderable->GetBoundingBoxMesh().GetExtents();
            const float measured_radius  = std::max({ extents.x, extents.y, extents.z });
            if (!std::isfinite(measured_radius) || measured_radius <= 1e-5f)
            {
                return;
            }

            wheel_entity->SetScaleLocal(math::Vector3(target_radius / measured_radius));
        }

        std::string resolve_car_file(const std::string& file)
        {
            const std::string relative = file.empty() ? "cars/ferrari_laferrari.car" : file;

            // paths in world files are relative to the world file directory
            const std::string& world_path = World::GetFilePath();
            if (!world_path.empty())
            {
                const std::string candidate = FileSystem::GetDirectoryFromFilePath(world_path) + relative;
                if (FileSystem::Exists(candidate))
                {
                    return candidate;
                }
            }

            // fall back to the path as given, relative to the working directory
            return relative;
        }

        std::string get_material_context(Entity* entity, Render* renderable)
        {
            std::string context;
            for (Entity* current = entity; current != nullptr; current = current->GetParent())
            {
                context += " ";
                context += to_lower_copy(current->GetObjectName());
            }

            if (renderable)
            {
                context += " ";
                context += to_lower_copy(renderable->GetMaterialName());
            }

            return context;
        }

        CarMaterialSlot resolve_car_material_slot(Entity* entity, Render* renderable)
        {
            const std::string context = get_material_context(entity, renderable);

            if (contains(context, "car_paint") || contains(context, "body"))
            {
                return CarMaterialSlot::BodyPaint;
            }

            if (contains(context, "carbon"))
            {
                return CarMaterialSlot::CarbonTrim;
            }

            if (contains(context, "object_129") || contains(context, "object_144") || contains(context, "object_159") || contains(context, "object_174"))
            {
                return CarMaterialSlot::BrakeDisc;
            }

            if (contains(context, "object_180") || contains(context, "object_150") || contains(context, "rim"))
            {
                return CarMaterialSlot::RimMetal;
            }

            if (contains(context, "tire"))
            {
                return CarMaterialSlot::TireRubber;
            }

            if (contains(context, "red_light") || contains(context, "tail_lights") || contains(context, "run_lights"))
            {
                return CarMaterialSlot::EmissiveRedLight;
            }

            if ((contains(context, "rear") || contains(context, "tail") || contains(context, "break") || contains(context, "breake")) && contains(context, "headlight"))
            {
                return CarMaterialSlot::EmissiveRedLight;
            }

            if (contains(context, "headlight") && !contains(context, "glass"))
            {
                return CarMaterialSlot::EmissiveWhiteLight;
            }

            if (contains(context, "glass") || contains(context, "glasses"))
            {
                if (contains(context, "break") || contains(context, "breake") || contains(context, "tail") || contains(context, "rear") || contains(context, "red"))
                {
                    return CarMaterialSlot::TaillightLens;
                }

                if (contains(context, "head"))
                {
                    return CarMaterialSlot::HeadlightLens;
                }

                return CarMaterialSlot::MainGlass;
            }

            if (contains(context, "object_58"))
            {
                return CarMaterialSlot::MainGlass;
            }

            if (contains(context, "object_98"))
            {
                return CarMaterialSlot::MirrorGlass;
            }

            if (contains(context, "object_14") || contains(context, "engine") || contains(context, "disk"))
            {
                return CarMaterialSlot::EngineMetal;
            }

            if (contains(context, "object_90") || contains(context, "leather") || contains(context, "interior"))
            {
                return CarMaterialSlot::InteriorLeather;
            }

            if (contains(context, "black") || contains(context, "under"))
            {
                return CarMaterialSlot::BlackTrim;
            }

            return CarMaterialSlot::Unknown;
        }

        std::shared_ptr<Material> clone_car_material(Entity* car_entity, Entity* entity, Render* renderable, const char* slot_name)
        {
            Material* source = renderable ? renderable->GetMaterial() : nullptr;
            if (!source)
            {
                return nullptr;
            }

            const std::string resource_name = "car_" + std::to_string(car_entity->GetObjectId()) + "_" + std::to_string(entity->GetObjectId()) + "_" + slot_name + std::string(EXTENSION_MATERIAL);
            std::shared_ptr<Material> material = source->Clone(resource_name);
            // the prefab rebuilds these clones with fresh entity ids on every load, saving them only litters the world resources with orphans
            material->SetPersistent(false);
            renderable->SetMaterial(material);
            return material;
        }

        const Color skeleton_color_frame       = Color(0.20f, 0.72f, 1.00f, 1.0f);
        const Color skeleton_color_control_arm = Color(0.86f, 0.88f, 0.92f, 1.0f);
        const Color skeleton_color_spring      = Color(1.00f, 0.72f, 0.12f, 1.0f);
        const Color skeleton_color_steering    = Color(0.25f, 1.00f, 0.48f, 1.0f);
        const Color skeleton_color_drivetrain  = Color(0.25f, 0.72f, 1.00f, 1.0f);
        const Color skeleton_color_joint       = Color(1.00f, 1.00f, 1.00f, 1.0f);
        const Color skeleton_color_wheel       = Color(0.38f, 0.78f, 0.96f, 1.0f);
        const Color skeleton_color_contact     = Color(0.20f, 1.00f, 0.42f, 1.0f);
        const Color skeleton_color_tire_force  = Color(1.00f, 0.30f, 0.68f, 1.0f);
        const Color skeleton_color_long_force  = Color(1.00f, 0.62f, 0.12f, 1.0f);
        const Color skeleton_color_torque      = Color(1.00f, 0.18f, 0.18f, 1.0f);
        const Color skeleton_color_aero        = Color(0.15f, 0.92f, 1.00f, 1.0f);
        const Color skeleton_color_collision   = Color(0.72f, 0.28f, 1.00f, 1.0f);

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

        Color get_skeleton_tire_temperature_color(float temperature, float wear)
        {
            const float ambient = ::car::tuning::spec.tire_ambient_temp;
            const float optimal = std::max(::car::tuning::spec.tire_optimal_temp, ambient + 1.0f);
            const float maximum = std::max(::car::tuning::spec.tire_max_temp, optimal + 1.0f);
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

        template<typename Transform>
        void draw_skeleton_chassis_shapes(physx::PxRigidDynamic* body, Transform&& to_render)
        {
            if (!body)
            {
                return;
            }

            const physx::PxU32 shape_count = body->getNbShapes();
            if (shape_count == 0)
            {
                return;
            }
            std::vector<physx::PxShape*> shapes(shape_count);
            body->getShapes(shapes.data(), shape_count);
            for (physx::PxShape* shape : shapes)
            {
                const physx::PxTransform shape_pose = body->getGlobalPose() * shape->getLocalPose();
                const physx::PxGeometry& shape_geometry = shape->getGeometry();
                if (shape_geometry.getType() == physx::PxGeometryType::eCONVEXMESH)
                {
                    const physx::PxConvexMeshGeometry& geometry = static_cast<const physx::PxConvexMeshGeometry&>(shape_geometry);
                    if (!geometry.convexMesh)
                    {
                        continue;
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
                            Renderer::DrawLine(to_render(shape_pose.transform(geometry.scale.transform(vertices[index_a]))), to_render(shape_pose.transform(geometry.scale.transform(vertices[index_b]))), skeleton_color_collision, skeleton_color_collision);
                        }
                    }
                }
                else if (shape_geometry.getType() == physx::PxGeometryType::eBOX)
                {
                    const physx::PxBoxGeometry& geometry = static_cast<const physx::PxBoxGeometry&>(shape_geometry);
                    const physx::PxVec3 h = geometry.halfExtents;
                    const physx::PxVec3 vertices[8] = { {-h.x, -h.y, -h.z}, {h.x, -h.y, -h.z}, {h.x, h.y, -h.z}, {-h.x, h.y, -h.z}, {-h.x, -h.y, h.z}, {h.x, -h.y, h.z}, {h.x, h.y, h.z}, {-h.x, h.y, h.z} };
                    const int edges[12][2] = { {0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7} };
                    for (const auto& edge : edges)
                    {
                        Renderer::DrawLine(to_render(shape_pose.transform(vertices[edge[0]])), to_render(shape_pose.transform(vertices[edge[1]])), skeleton_color_collision, skeleton_color_collision);
                    }
                }
            }
        }
    }

    Car* Car::Create(const Config& config)
    {
        // the .car file is the single source of truth for the car
        const ::car::car_definition* definition = ::car::load_car_file(config.car_file);
        if (!definition)
        {
            SP_LOG_ERROR("failed to load car file: %s", config.car_file.c_str());
            return nullptr;
        }

        // register sibling car files so the hud preset selector can switch between them
        ::car::load_car_directory(FileSystem::GetDirectoryFromFilePath(config.car_file));

        Car* car = new Car();
        car->m_definition     = definition;
        car->m_spawn_position = config.position;
        car->m_show_telemetry = config.show_telemetry;
        car->m_is_drivable    = config.drivable;
        car->m_paint_preset   = config.paint_preset;
        car->m_paint_color    = config.paint_color;

        if (config.drivable)
        {
            // the definition's performance drives the simulation
            ::car::load_car(definition->performance);
            for (int i = 0; i < ::car::preset_count; i++)
            {
                if (::car::preset_registry[i].instance == &definition->performance)
                {
                    ::car::active_preset_index = i;
                    break;
                }
            }

            // create vehicle entity with physics
            car->m_vehicle_entity = World::CreateEntity();
            car->m_vehicle_entity->SetObjectName("vehicle");
            car->m_vehicle_entity->SetPosition(config.position);
            car->m_vehicle_entity->AddTag("car");

            Physics* physics = car->m_vehicle_entity->AddComponent<Physics>();
            physics->SetStatic(false);
            // mass comes from the active car preset, applied inside car::setup
            // we seed a sensible fallback so Physics::GetMass returns something useful before vehicle setup
            physics->SetMass(::car::tuning::spec.mass > 0.0f ? ::car::tuning::spec.mass : 1500.0f);
            physics->SetBodyType(BodyType::Vehicle);
            physics->SetCar(car);  // car ticks automatically through entity system

            // create car body (without its baked in wheels)
            std::vector<Entity*> excluded_wheel_entities;
            car->m_body_entity = car->CreateBody(&excluded_wheel_entities);
            if (car->m_body_entity)
            {
                car->m_body_entity->SetParent(car->m_vehicle_entity);
                if (definition->body_model.empty())
                {
                    car->m_body_entity->SetPositionLocal(math::Vector3::Zero);
                    car->m_body_entity->SetRotationLocal(math::Quaternion::Identity);
                }
                else
                {
                    car->m_body_entity->SetPositionLocal(math::Vector3(0.0f, ::car::get_chassis_visual_offset_y(), 0.07f));
                    car->m_body_entity->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Right, math::pi * 0.5f));
                    car->m_body_entity->SetScaleLocal(1.1f);
                }

                physics->SetChassisEntity(car->m_body_entity, excluded_wheel_entities);
            }

            car->CreateAudioSources(car->m_vehicle_entity);
            car->CreateWheels(car->m_vehicle_entity, physics);

            // skid marks are defined in the world as a prefab override on the vehicle, not added here

            // store camera_follows flag - car will auto-enter when play mode starts
            car->m_camera_follows = config.camera_follows;

            // set globals for backward compatibility
            default_car = car->m_body_entity;
        }
        else
        {
            // non-drivable display car: same deterministic hierarchy as the drivable one,
            // a root with the body bolted on and free spinning wheels, just without physics
            car->m_vehicle_entity = World::CreateEntity();
            car->m_vehicle_entity->SetObjectName("vehicle");
            car->m_vehicle_entity->SetPosition(config.position);
            car->m_vehicle_entity->AddTag("car");

            std::vector<Entity*> baked_wheel_entities;
            car->m_body_entity = car->CreateBody(&baked_wheel_entities);
            if (car->m_body_entity)
            {
                car->m_body_entity->SetParent(car->m_vehicle_entity);
                car->m_body_entity->SetPositionLocal(math::Vector3::Zero);
            }

            // spawn real wheel entities where the baked in tires were
            car->CreatePropWheels(car->m_vehicle_entity, baked_wheel_entities);

            if (config.static_physics && car->m_body_entity)
            {
                std::vector<Entity*> car_parts;
                car->m_body_entity->GetDescendants(&car_parts);
                for (Entity* car_part : car_parts)
                {
                    if (car_part->GetComponent<Render>())
                    {
                        Physics* physics_body = car_part->AddComponent<Physics>();
                        physics_body->SetKinematic(true);
                        physics_body->SetBodyType(BodyType::Mesh);
                    }
                }
            }

            car->CreateAudioSources(car->m_vehicle_entity);
            default_car = car->m_body_entity;
        }

        {
            std::lock_guard<std::mutex> lock(car_list_mutex);
            s_cars.push_back(car);
        }
        return car;
    }

    void Car::RegisterPrefabs()
    {
        Prefab::Register("car", Car::CreatePrefab);
    }

    Entity* Car::CreatePrefab(pugi::xml_node& node, Entity* parent)
    {
        Config config;
        config.position       = parent ? parent->GetPosition() : math::Vector3::Zero;
        config.car_file       = resolve_car_file(node.attribute("file").as_string(""));
        config.drivable       = node.attribute("drivable").as_bool(false);
        config.static_physics = node.attribute("static_physics").as_bool(false);
        config.show_telemetry = node.attribute("telemetry").as_bool(false);
        config.camera_follows = node.attribute("camera_follows").as_bool(false);
        config.paint_preset   = parse_paint_preset(node.attribute("paint_preset").as_string("metallic"));
        config.paint_color.r  = node.attribute("paint_color_r").as_float(config.paint_color.r);
        config.paint_color.g  = node.attribute("paint_color_g").as_float(config.paint_color.g);
        config.paint_color.b  = node.attribute("paint_color_b").as_float(config.paint_color.b);
        config.paint_color.a  = node.attribute("paint_color_a").as_float(config.paint_color.a);

        Car* car = Create(config);
        if (car && parent)
        {
            Entity* root = car->GetRootEntity();
            if (root)
            {
                root->SetParent(parent);
                root->SetPositionLocal(math::Vector3::Zero);
            }
        }
        return car ? car->GetRootEntity() : nullptr;
    }

    void Car::ShutdownAll()
    {
        for (Car* car : s_cars)
        {
            car->m_vehicle_entity = nullptr;
            car->m_body_entity    = nullptr;
            car->m_window_entity  = nullptr;
            delete car;
        }
        s_cars.clear();

        default_camera     = nullptr;
        default_car        = nullptr;
        default_car_window = nullptr;

        // stop any vibration
        Input::GamepadVibrate(0.0f, 0.0f);
    }

    std::vector<Car*>& Car::GetAll()
    {
        return s_cars;
    }

    void Car::SetVisualizationPreset(CarVisualizationPreset preset)
    {
        if (preset == m_visualization_preset)
        {
            return;
        }

        for (const BodyRenderState& state : m_body_render_states)
        {
            if (state.entity)
            {
                state.entity->SetActive(state.active);
            }
        }
        m_body_render_states.clear();

        m_visualization_preset = preset;
        if (preset != CarVisualizationPreset::Skeleton || !m_vehicle_entity)
        {
            return;
        }

        std::vector<Entity*> render_entities;
        render_entities.push_back(m_vehicle_entity);
        m_vehicle_entity->GetDescendants(&render_entities);

        for (Entity* entity : render_entities)
        {
            if (entity && entity->GetComponent<Render>())
            {
                m_body_render_states.push_back({ entity, entity->IsActive() });
            }
        }

        for (const BodyRenderState& state : m_body_render_states)
        {
            state.entity->SetActive(false);
        }
    }

    void Car::LoadDefinition(const car::car_definition* definition)
    {
        if (!definition || definition == m_definition || !m_vehicle_entity)
        {
            return;
        }

        Physics* physics = m_vehicle_entity->GetComponent<Physics>();
        if (!physics)
        {
            return;
        }

        const CarVisualizationPreset visualization_preset = m_visualization_preset;
        if (visualization_preset == CarVisualizationPreset::Skeleton)
        {
            SetVisualizationPreset(CarVisualizationPreset::Full);
        }

        const math::Vector3 current_position = m_vehicle_entity->GetPosition();
        const math::Quaternion current_rotation = m_vehicle_entity->GetRotation();
        float ground_height = current_position.y - get_car_lower_extent(::car::tuning::spec);
        bool has_ground_contact = false;
        for (int i = 0; i < ::car::wheel_count; i++)
        {
            const physx::PxVec3& contact_point = ::car::wheels[i].contact_point;
            if (::car::wheels[i].grounded && std::isfinite(contact_point.y))
            {
                ground_height = has_ground_contact ? std::max(ground_height, contact_point.y) : contact_point.y;
                has_ground_contact = true;
            }
        }
        const float target_height = ground_height + get_car_lower_extent(definition->performance) + car_spawn_margin;
        const math::Vector3 target_position(current_position.x, std::max(current_position.y, target_height), current_position.z);
        const math::Quaternion target_rotation = math::Quaternion::FromEulerAngles(0.0f, current_rotation.Yaw(), 0.0f);
        physics->SetBodyTransform(target_position, target_rotation, false);

        if (m_body_entity)
        {
            m_body_entity->SetActive(false);
            World::RemoveEntity(m_body_entity);
        }

        m_body_entity   = nullptr;
        m_window_entity = nullptr;
        m_definition    = definition;
        ::car::load_car(definition->performance);

        std::vector<Entity*> excluded_wheel_entities;
        m_body_entity = CreateBody(&excluded_wheel_entities);
        if (m_body_entity)
        {
            m_body_entity->SetParent(m_vehicle_entity);
            if (definition->body_model.empty())
            {
                m_body_entity->SetPositionLocal(math::Vector3::Zero);
                m_body_entity->SetRotationLocal(math::Quaternion::Identity);
            }
            else
            {
                m_body_entity->SetPositionLocal(math::Vector3(0.0f, ::car::get_chassis_visual_offset_y(), 0.07f));
                m_body_entity->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Right, math::pi * 0.5f));
                m_body_entity->SetScaleLocal(1.1f);
            }
            physics->SetChassisEntity(m_body_entity, excluded_wheel_entities);
        }

        for (int i = 0; i < 4; i++)
        {
            const WheelIndex wheel_index = static_cast<WheelIndex>(i);
            if (Entity* wheel_entity = physics->GetWheelEntity(wheel_index))
            {
                const float radius = i < 2 ? ::car::cfg.front_wheel_radius : ::car::cfg.rear_wheel_radius;
                const float width  = i < 2 ? ::car::cfg.front_wheel_width : ::car::cfg.rear_wheel_width;
                physics->ScaleWheelEntityToDimensions(wheel_entity, radius, width);
            }
        }

        default_car        = m_body_entity;
        default_car_window = m_window_entity;
        if (visualization_preset == CarVisualizationPreset::Skeleton)
        {
            SetVisualizationPreset(CarVisualizationPreset::Skeleton);
        }
    }

    void Car::TickVisualization()
    {
        if (m_visualization_preset != CarVisualizationPreset::Skeleton || !m_vehicle_entity)
        {
            return;
        }

        Physics* physics = m_vehicle_entity->GetComponent<Physics>();
        if (!physics)
        {
            return;
        }

        Entity* wheel_entities[4] =
        {
            physics->GetWheelEntity(WheelIndex::FrontLeft),
            physics->GetWheelEntity(WheelIndex::FrontRight),
            physics->GetWheelEntity(WheelIndex::RearLeft),
            physics->GetWheelEntity(WheelIndex::RearRight)
        };

        for (Entity* wheel_entity : wheel_entities)
        {
            if (!wheel_entity)
            {
                return;
            }
        }

        const math::Vector3 vehicle_position = m_vehicle_entity->GetPosition();
        const math::Quaternion vehicle_rotation = m_vehicle_entity->GetRotation();
        auto to_world = [&](const math::Vector3& local) { return vehicle_position + vehicle_rotation * local; };
        auto from_px = [](const physx::PxVec3& value) { return math::Vector3(value.x, value.y, value.z); };
        auto to_render = [&](const physx::PxVec3& value) { return physics->TransformVehiclePointToRender(from_px(value)); };
        draw_skeleton_chassis_shapes(::car::body, to_render);

        math::Vector3 wheel_local[4];
        math::Vector3 wheel_world[4];
        math::Vector3 shock_top_world[4];
        math::Vector3 shock_bottom_world[4];
        for (int i = 0; i < 4; i++)
        {
            const ::car::suspension_corner& corner = ::car::multibody.corners[i];
            if (!corner.upright || !corner.wheel_body)
            {
                return;
            }

            const physx::PxTransform wheel_pose = corner.wheel_body->getGlobalPose();
            wheel_world[i] = to_render(wheel_pose.p);
            wheel_local[i] = vehicle_rotation.Conjugate() * (wheel_world[i] - vehicle_position);
            const float wheel_radius = ::car::cfg.wheel_radius_for(i);
            const float wheel_half_width = ::car::cfg.wheel_width_for(i) * 0.5f;
            const physx::PxQuat query_rotation = corner.upright->getGlobalPose().q;
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
            const Color zone_color[3] = { get_skeleton_tire_temperature_color(::car::wheels[i].thermal.surface[0], ::car::wheels[i].wear), get_skeleton_tire_temperature_color(::car::wheels[i].thermal.surface[1], ::car::wheels[i].wear), get_skeleton_tire_temperature_color(::car::wheels[i].thermal.surface[2], ::car::wheels[i].wear) };
            for (int segment = 0; segment <= wheel_segments; segment++)
            {
                const float angle = static_cast<float>(segment) / static_cast<float>(wheel_segments) * math::pi * 2.0f;
                const physx::PxVec3 radial = (wheel_radial_y * cosf(angle) + wheel_radial_z * sinf(angle)) * wheel_radius;
                const physx::PxVec3 effective_point = wheel_pose.p + (wheel_radial_y * cosf(angle) + wheel_radial_z * sinf(angle)) * ::car::wheels[i].effective_radius;
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

            draw_skeleton_cylinder(to_render(wheel_left), to_render(wheel_right), 0.045f, skeleton_color_joint);
            const float brake_radius = wheel_radius * 0.62f;
            const float brake_temperature_range = std::max(::car::tuning::spec.brake_fade_temp - ::car::tuning::spec.brake_ambient_temp, 1.0f);
            const float brake_heat = std::clamp((::car::wheels[i].brake_temp - ::car::tuning::spec.brake_ambient_temp) / brake_temperature_range, 0.0f, 1.0f);
            const float abs_flash = ::car::abs_active[i] ? 0.35f : 0.0f;
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
            Renderer::DrawLine(wheel_world[i], to_render(wheel_pose.p + spin_marker), skeleton_color_joint, skeleton_color_joint);
            const math::Vector3 wheel_axis_render = (to_render(wheel_pose.p + wheel_axis) - wheel_world[i]).Normalized();
            const float wheel_torque_reference = std::max(::car::tuning::spec.handbrake_torque + ::car::tuning::spec.brake_force * wheel_radius, 1.0f);
            draw_skeleton_torque_arc(wheel_world[i], wheel_axis_render, wheel_radius * 0.72f, ::car::wheels[i].net_torque / wheel_torque_reference, skeleton_color_torque);
            Renderer::DrawSphere(wheel_world[i], 0.052f, 7, get_skeleton_tire_temperature_color(::car::wheels[i].thermal.core, ::car::wheels[i].wear));
            const float axle_brake_share = i < 2 ? ::car::tuning::spec.brake_bias_front : 1.0f - ::car::tuning::spec.brake_bias_front;
            const physx::PxVec3 chassis_forward = ::car::body->getGlobalPose().q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f));
            const float forward_speed = fabsf(::car::body->getLinearVelocity().dot(chassis_forward));
            const bool reverse_requested = !::car::tuning::spec.manual_transmission && forward_speed < 0.5f && ::car::input.brake > 0.8f && ::car::input.throttle < ::car::tuning::spec.input_deadzone && ::car::is_in_forward_gear() && !::car::is_shifting;
            const bool service_braking = ::car::input.brake > ::car::tuning::spec.input_deadzone && !::car::is_in_reverse() && !reverse_requested;
            const float wheel_radius_for_brake = ::car::cfg.wheel_radius_for(i);
            float applied_brake_torque = service_braking ? ::car::tuning::spec.brake_force * wheel_radius_for_brake * ::car::input.brake * axle_brake_share * 0.5f * ::car::get_brake_efficiency(::car::wheels[i].brake_temp) * ::car::assisted_actuators.brake_torque_scale[i] : 0.0f;
            if (i >= 2)
            {
                applied_brake_torque += ::car::tuning::spec.handbrake_torque * ::car::input.handbrake;
            }
            const float brake_reference = std::max(::car::tuning::spec.brake_force * wheel_radius_for_brake * axle_brake_share * 0.5f + (i >= 2 ? ::car::tuning::spec.handbrake_torque : 0.0f), 1.0f);
            const float brake_actuation = std::clamp(applied_brake_torque / brake_reference, 0.0f, 1.0f);
            const float caliper_size = 0.050f + brake_actuation * 0.016f;
            Renderer::DrawSphere(to_render(wheel_pose.p + wheel_radial_y * brake_radius * 0.72f + wheel_radial_z * brake_radius * 0.45f), caliper_size, 6, brake_color);

            const physx::PxTransform upright_pose = corner.upright->getGlobalPose();
            const math::Vector3 upright_bottom = to_render(upright_pose.transform(physx::PxVec3(0.0f, -0.18f, 0.0f)));
            const math::Vector3 upright_top = to_render(upright_pose.transform(physx::PxVec3(0.0f, 0.18f, 0.0f)));
            draw_skeleton_cylinder(upright_bottom, upright_top, 0.028f, skeleton_color_control_arm);
            draw_skeleton_joint(upright_bottom, skeleton_color_joint);
            draw_skeleton_joint(upright_top, skeleton_color_joint);

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
                const bool tie_rod = i < 2 && ::car::multibody.rack && member_index == corner.member_count - 1;
                const Color& member_color = tie_rod ? skeleton_color_steering : skeleton_color_control_arm;
                draw_skeleton_cylinder(start, end, tie_rod ? 0.016f : 0.020f, member_color);
                draw_skeleton_joint(start, tie_rod ? skeleton_color_steering : skeleton_color_joint);
                draw_skeleton_joint(end, tie_rod ? skeleton_color_steering : skeleton_color_joint);
            }

            const math::Vector3 shock_top = to_render(::car::body->getGlobalPose().transform(corner.chassis_shock_anchor));
            const math::Vector3 shock_bottom = to_render(upright_pose.transform(corner.upright_shock_anchor));
            shock_top_world[i] = shock_top;
            shock_bottom_world[i] = shock_bottom;
            const math::Vector3 shock_mid = lerp_skeleton(shock_top, shock_bottom, 0.48f);
            const float spring_load = std::clamp(fabsf(::car::spring_force[i]) / std::max(::car::tuning::spec.max_susp_force, 1.0f), 0.0f, 1.0f);
            const float damper_velocity = fabsf(::car::wheels[i].compression_velocity) * ::car::cfg.suspension_travel;
            const float damper_load = std::clamp(damper_velocity / std::max(::car::tuning::spec.max_damper_velocity, 0.1f), 0.0f, 1.0f);
            const Color spring_color = Color(1.0f, 0.72f + spring_load * 0.25f, 0.12f + spring_load * 0.55f, 1.0f);
            const Color damper_color = Color(1.0f, 0.38f + damper_load * 0.45f, 0.12f, 1.0f);
            draw_skeleton_cylinder(shock_top, shock_mid, 0.030f, damper_color);
            Renderer::DrawLine(shock_mid, shock_bottom, skeleton_color_joint, skeleton_color_joint);
            draw_skeleton_spring(shock_top, shock_bottom, vehicle_rotation * math::Vector3::Forward, 0.055f, spring_color);
            draw_skeleton_joint(shock_top, skeleton_color_joint);
            draw_skeleton_joint(shock_bottom, skeleton_color_joint);
            const math::Vector3 shock_axis = (shock_top - shock_bottom).Normalized();
            const math::Vector3 spring_force_vector = shock_axis * (::car::spring_force[i] * 0.00001f);
            Renderer::DrawLine(shock_top, shock_top + spring_force_vector, spring_color, spring_color);
            Renderer::DrawLine(shock_bottom, shock_bottom - spring_force_vector, spring_color, spring_color);
            const float current_compression = corner.shock_rest_length - corner.shock_length;
            if (current_compression > ::car::cfg.suspension_travel * ::car::tuning::spec.bump_stop_threshold)
            {
                Renderer::DrawSphere(shock_bottom, 0.065f, 8, skeleton_color_torque);
            }
            physx::PxVec3 sweep_origin;
            physx::PxVec3 sweep_endpoint;
            bool sweep_hit = false;
            ::car::get_debug_sweep(i, sweep_origin, sweep_endpoint, sweep_hit);
            const Color& sweep_color = sweep_hit ? skeleton_color_contact : skeleton_color_collision;
            Renderer::DrawLine(to_render(sweep_origin), to_render(sweep_endpoint), sweep_color, sweep_color);
            Renderer::DrawSphere(to_render(sweep_endpoint), 0.025f, 6, sweep_color);
            if (::car::wheels[i].grounded)
            {
                const ::car::wheel& wheel = ::car::wheels[i];
                physx::PxVec3 wheel_forward = wheel_axis.cross(wheel.contact_normal);
                if (wheel_forward.normalize() < 0.0001f)
                {
                    wheel_forward = upright_pose.q.rotate(physx::PxVec3(0.0f, 0.0f, 1.0f));
                }
                const physx::PxVec3 wheel_lateral = wheel.contact_normal.cross(wheel_forward).getNormalized();
                const physx::PxVec3 normal_endpoint = wheel.contact_point + wheel.contact_normal * (wheel.tire_load * 0.00002f);
                const physx::PxVec3 longitudinal_endpoint = wheel.contact_point + wheel_forward * wheel.longitudinal_force * 0.00002f;
                const physx::PxVec3 lateral_endpoint = wheel.contact_point + wheel_lateral * wheel.lateral_force * 0.00002f;
                const float rolling_resistance_force = ::car::tuning::spec.rolling_resistance * wheel.tire_load;
                const float rolling_direction = -std::clamp(corner.wheel_body->getLinearVelocity().dot(wheel_forward) / 0.5f, -1.0f, 1.0f);
                const physx::PxVec3 rolling_endpoint = wheel.contact_point + wheel_forward * rolling_direction * rolling_resistance_force * 0.00004f;
                const math::Vector3 contact = to_render(wheel.contact_point);
                Color contact_color = skeleton_color_contact;
                if (wheel.contact_surface == ::car::surface_gravel)
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
                Renderer::DrawLine(contact, to_render(longitudinal_endpoint), skeleton_color_long_force, skeleton_color_long_force);
                Renderer::DrawLine(contact, to_render(lateral_endpoint), skeleton_color_tire_force, skeleton_color_tire_force);
                Renderer::DrawLine(contact, to_render(rolling_endpoint), skeleton_color_aero, skeleton_color_aero);
                const float trail = std::max(::car::tuning::spec.pneumatic_trail_max * (1.0f - fabsf(wheel.slip_angle) / std::max(::car::tuning::spec.pneumatic_trail_peak, 0.01f)), 0.0f);
                const float aligning_torque = -wheel.lateral_force * trail * ::car::tuning::spec.self_align_gain;
                const math::Vector3 contact_normal_render = (to_render(wheel.contact_point + wheel.contact_normal) - contact).Normalized();
                draw_skeleton_torque_arc(wheel_world[i], contact_normal_render, wheel_radius * 0.48f, aligning_torque / 500.0f, skeleton_color_steering);
            }
        }

        const float front_z = (wheel_local[0].z + wheel_local[1].z) * 0.5f;
        const float rear_z  = (wheel_local[2].z + wheel_local[3].z) * 0.5f;
        const float frame_y = 0.04f;
        const float front_frame_half_width = ::car::cfg.track_front * 0.30f;
        const float rear_frame_half_width = ::car::cfg.track_rear * 0.30f;
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

        auto draw_anti_roll_bar = [&](int left, int right, float stiffness)
        {
            if (stiffness <= 0.0f)
            {
                return;
            }
            const ::car::suspension_corner& left_corner = ::car::multibody.corners[left];
            const ::car::suspension_corner& right_corner = ::car::multibody.corners[right];
            const float compression_difference = (left_corner.shock_rest_length - left_corner.shock_length) - (right_corner.shock_rest_length - right_corner.shock_length);
            const float anti_roll_load = std::clamp(fabsf(compression_difference * stiffness) / std::max(::car::tuning::spec.max_susp_force, 1.0f), 0.0f, 1.0f);
            const Color loaded_anti_roll_color = Color(0.82f + anti_roll_load * 0.18f, 0.35f + anti_roll_load * 0.30f, 1.0f, 1.0f);
            const math::Vector3 left_arm_end = lerp_skeleton(shock_top_world[left], shock_bottom_world[left], 0.24f);
            const math::Vector3 right_arm_end = lerp_skeleton(shock_top_world[right], shock_bottom_world[right], 0.24f);
            const float anti_roll_twist = compression_difference / std::max(::car::cfg.suspension_travel, 0.01f) * math::pi;
            draw_skeleton_shaft(shock_top_world[left], shock_top_world[right], 0.020f, 0.0f, anti_roll_twist, loaded_anti_roll_color);
            draw_skeleton_cylinder(shock_top_world[left], left_arm_end, 0.014f, loaded_anti_roll_color);
            draw_skeleton_cylinder(shock_top_world[right], right_arm_end, 0.014f, loaded_anti_roll_color);
            const math::Vector3 anti_roll_force = vehicle_rotation * math::Vector3::Up * (compression_difference * stiffness * 0.00001f);
            Renderer::DrawLine(shock_bottom_world[left], shock_bottom_world[left] - anti_roll_force, loaded_anti_roll_color, loaded_anti_roll_color);
            Renderer::DrawLine(shock_bottom_world[right], shock_bottom_world[right] + anti_roll_force, loaded_anti_roll_color, loaded_anti_roll_color);
        };
        draw_anti_roll_bar(0, 1, ::car::tuning::spec.front_arb_stiffness);
        draw_anti_roll_bar(2, 3, ::car::tuning::spec.rear_arb_stiffness);

        const physx::PxTransform body_pose = ::car::body->getGlobalPose();
        const math::Vector3 center_of_mass = to_render(body_pose.transform(::car::body->getCMassLocalPose().p));
        Renderer::DrawSphere(center_of_mass, 0.075f, 10, skeleton_color_spring);
        if (::car::aero_debug.valid)
        {
            auto draw_aero_force = [&](const physx::PxVec3& position, const physx::PxVec3& force)
            {
                const math::Vector3 start = to_render(position);
                Renderer::DrawLine(start, to_render(position + force * 0.00002f), skeleton_color_aero, skeleton_color_aero);
            };
            draw_aero_force(::car::aero_debug.position, ::car::aero_debug.drag_force);
            draw_aero_force(::car::aero_debug.front_aero_pos, ::car::aero_debug.front_downforce);
            draw_aero_force(::car::aero_debug.rear_aero_pos, ::car::aero_debug.rear_downforce);
            draw_aero_force(::car::aero_debug.side_aero_pos, ::car::aero_debug.side_force);
        }

        if (::car::multibody.rack)
        {
            const physx::PxTransform rack_pose = ::car::multibody.rack->getGlobalPose();
            const float half_width = ::car::cfg.track_front * 0.35f;
            const math::Vector3 rack_left = to_render(rack_pose.transform(physx::PxVec3(-half_width, 0.0f, 0.0f)));
            const math::Vector3 rack_right = to_render(rack_pose.transform(physx::PxVec3(half_width, 0.0f, 0.0f)));
            draw_skeleton_cylinder(rack_left, rack_right, 0.025f, skeleton_color_steering);
            draw_skeleton_joint(rack_left, skeleton_color_steering);
            draw_skeleton_joint(rack_right, skeleton_color_steering);
        }

        const int drivetrain_type = ::car::tuning::spec.drivetrain_type;
        const bool drives_front = drivetrain_type == 1 || drivetrain_type == 2;
        const bool drives_rear  = drivetrain_type == 0 || drivetrain_type == 2;
        const math::Vector3 front_diff = to_world(math::Vector3(0.0f, -0.02f, front_z));
        const math::Vector3 rear_diff  = to_world(math::Vector3(0.0f, -0.02f, rear_z));
        const math::Vector3 gearbox    = to_world(math::Vector3(0.0f, 0.02f, 0.0f));
        const float driveshaft_twist  = ::car::get_driveshaft_twist();
        const float driveshaft_torque = ::car::get_driveshaft_torque();
        const float torque_load = std::clamp(fabsf(driveshaft_torque) / 6000.0f, 0.0f, 1.0f);
        const Color loaded_drivetrain_color = Color(0.25f - torque_load * 0.08f, 0.72f + torque_load * 0.18f, 1.00f, 1.0f);
        const float motor_load = ::car::tuning::spec.electric_enabled ? std::clamp(fabsf(::car::motor_torque) / std::max(::car::tuning::spec.electric_motor_torque, 1.0f), 0.0f, 1.0f) : 0.0f;
        Color power_unit_color = Color(0.25f, 0.72f + motor_load * 0.24f, 1.0f, 1.0f);
        if (::car::rev_limiter_active)
        {
            power_unit_color = skeleton_color_torque;
        }
        else if (::car::tc_active)
        {
            power_unit_color = Color(1.0f, 0.55f + ::car::tc_reduction * 0.35f, 0.10f, 1.0f);
        }
        else if (::car::is_shifting)
        {
            power_unit_color = skeleton_color_spring;
        }
        const float front_pinion_rotation = (::car::get_wheel_rotation(0) + ::car::get_wheel_rotation(1)) * 0.5f * ::car::tuning::spec.final_drive;
        const float rear_pinion_rotation  = (::car::get_wheel_rotation(2) + ::car::get_wheel_rotation(3)) * 0.5f * ::car::tuning::spec.final_drive;
        auto wheel_drivetrain_color = [&](int wheel_index)
        {
            const float wheel_torque_load = std::clamp(fabsf(::car::wheels[wheel_index].drive_torque) / 6000.0f, 0.0f, 1.0f);
            return Color(0.25f - wheel_torque_load * 0.08f, 0.72f + wheel_torque_load * 0.18f, 1.0f, 1.0f);
        };

        const math::Vector3 flywheel_axis = vehicle_rotation * math::Vector3::Right * 0.10f;
        draw_skeleton_shaft(gearbox - flywheel_axis, gearbox + flywheel_axis, 0.075f + ::car::clutch * 0.015f, ::car::engine_rotation, 0.0f, power_unit_color);
        Renderer::DrawSphere(gearbox, 0.10f, 8, power_unit_color);
        if (drives_front)
        {
            Renderer::DrawSphere(front_diff, 0.11f, 8, skeleton_color_drivetrain);
            draw_skeleton_shaft(front_diff, wheel_world[0], 0.04f, ::car::get_wheel_rotation(0), ::car::wheels[0].drive_torque * 0.00005f, wheel_drivetrain_color(0));
            draw_skeleton_shaft(front_diff, wheel_world[1], 0.04f, ::car::get_wheel_rotation(1), ::car::wheels[1].drive_torque * 0.00005f, wheel_drivetrain_color(1));
            const float front_shaft_radius = drivetrain_type == 2 ? 0.035f + ::car::tuning::spec.torque_split_front * 0.04f : 0.055f;
            draw_skeleton_shaft(gearbox, front_diff, front_shaft_radius, front_pinion_rotation, driveshaft_twist, loaded_drivetrain_color);
        }
        if (drives_rear)
        {
            Renderer::DrawSphere(rear_diff, 0.11f, 8, skeleton_color_drivetrain);
            draw_skeleton_shaft(rear_diff, wheel_world[2], 0.04f, ::car::get_wheel_rotation(2), ::car::wheels[2].drive_torque * 0.00005f, wheel_drivetrain_color(2));
            draw_skeleton_shaft(rear_diff, wheel_world[3], 0.04f, ::car::get_wheel_rotation(3), ::car::wheels[3].drive_torque * 0.00005f, wheel_drivetrain_color(3));
            const float rear_shaft_radius = drivetrain_type == 2 ? 0.035f + (1.0f - ::car::tuning::spec.torque_split_front) * 0.04f : 0.055f;
            draw_skeleton_shaft(gearbox, rear_diff, rear_shaft_radius, rear_pinion_rotation, driveshaft_twist, loaded_drivetrain_color);
        }
    }

    void Car::Destroy()
    {
        {
            std::lock_guard<std::mutex> lock(car_list_mutex);
            auto it = std::find(s_cars.begin(), s_cars.end(), this);
            if (it != s_cars.end())
            {
                s_cars.erase(it);
            }
        }

        if (m_vehicle_entity)
        {
            World::RemoveEntity(m_vehicle_entity);
        }
        else if (m_body_entity)
        {
            World::RemoveEntity(m_body_entity);
        }

        delete this;
    }

    void Car::Enter()
    {
        if (m_is_occupied || !m_is_drivable)
        {
            return;
        }

        m_is_occupied = true;
        m_chase_camera.initialized = false;

        // disable player physics controller so it doesn't interfere with driving
        if (default_camera)
        {
            if (Physics* controller = default_camera->GetComponent<Physics>())
            {
                controller->SetEnabled(false);
            }
        }

        ConfigureCameraForView();

        // play engine start sound
        if (Entity* sound_start = m_vehicle_entity ? m_vehicle_entity->GetChildByName("sound_start") : nullptr)
        {
            if (AudioSource* audio = sound_start->GetComponent<AudioSource>())
            {
                audio->PlayClip();
            }
        }

        // play door sound
        if (Entity* sound_door = m_vehicle_entity ? m_vehicle_entity->GetChildByName("sound_door") : nullptr)
        {
            if (AudioSource* audio = sound_door->GetComponent<AudioSource>())
            {
                audio->PlayClip();
            }
        }
    }

    void Car::Exit()
    {
        if (!m_is_occupied)
        {
            return;
        }

        m_is_occupied = false;
        m_mcp_controlled = false;
        m_chase_camera.initialized = false;

        // restore mouse cursor if orbit was active
        if (m_orbit_mouse_active)
        {
            Input::SetMousePosition(m_orbit_mouse_last_position);
            if (!Window::IsFullScreen())
            {
                Input::SetMouseCursorVisible(true);
            }
            m_orbit_mouse_active = false;
        }

        // stop the car: clear all inputs and apply handbrake
        if (m_vehicle_entity)
        {
            if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
            {
                physics->SetVehicleThrottle(0.0f);
                physics->SetVehicleBrake(0.0f);
                physics->SetVehicleSteering(0.0f);
                physics->SetVehicleHandbrake(1.0f);
            }
        }

        Entity* camera = m_body_entity ? m_body_entity->GetChildByName("component_camera") : nullptr;
        if (!camera && default_camera)
        {
            camera = default_camera->GetChildByName("component_camera");
        }

        if (camera && default_camera)
        {
            camera->SetParent(default_camera);
            camera->SetRotationLocal(math::Quaternion::Identity);
        }

        // re-enable player physics controller
        if (default_camera)
        {
            if (Physics* controller = default_camera->GetComponent<Physics>())
            {
                controller->SetEnabled(true);
            }
        }

        // position player at the driver's door (left side of car) as if they were riding all along
        if (default_camera)
        {
            // use vehicle entity for position, fall back to body entity
            Entity* car_ref = m_vehicle_entity ? m_vehicle_entity : m_body_entity;
            if (car_ref)
            {
                // get car's world transform for proper orientation
                math::Vector3 car_position = car_ref->GetPosition();
                math::Vector3 car_left     = car_ref->GetLeft();
                math::Vector3 car_forward  = car_ref->GetForward();

                // offset to driver's door: slightly left and forward (door is roughly at front half of car)
                const float door_side_offset    = 1.8f; // distance from car center to door
                const float door_forward_offset = 0.3f; // slightly forward toward front seat area
                const float ground_offset       = 0.1f; // small offset above ground

                math::Vector3 exit_position = car_position 
                                            + car_left * door_side_offset 
                                            + car_forward * door_forward_offset
                                            + math::Vector3::Up * ground_offset;

                // teleport the physics body first (this is the authoritative position)
                Physics* controller = default_camera->GetComponent<Physics>();
                if (controller)
                {
                    controller->SetBodyTransform(exit_position, math::Quaternion::Identity);
                }

                // also set entity position directly as fallback
                default_camera->SetPosition(exit_position);

                // reset camera local position to top of controller
                if (camera && controller)
                {
                    camera->SetPositionLocal(controller->GetControllerTopLocal());
                }
            }
        }

        // stop engine sound
        if (Entity* sound_engine = m_vehicle_entity ? m_vehicle_entity->GetChildByName("sound_engine") : nullptr)
        {
            if (AudioSource* audio = sound_engine->GetComponent<AudioSource>())
            {
                audio->StopSynthesis();
            }
        }

        // play door sound
        if (Entity* sound_door = m_vehicle_entity ? m_vehicle_entity->GetChildByName("sound_door") : nullptr)
        {
            if (AudioSource* audio = sound_door->GetComponent<AudioSource>())
            {
                audio->PlayClip();
            }
        }

        // show window when outside
        if (m_window_entity)
        {
            m_window_entity->SetActive(true);
        }

        // stop vibration
        Input::GamepadVibrate(0.0f, 0.0f);
    }

    void Car::SetThrottle(float value)
    {
        if (!m_vehicle_entity)
        {
            return;
        }
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleThrottle(value);
        }
    }

    void Car::SetBrake(float value)
    {
        if (!m_vehicle_entity)
        {
            return;
        }
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleBrake(value);
        }
    }

    void Car::SetSteering(float value)
    {
        if (!m_vehicle_entity)
        {
            return;
        }
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleSteering(value);
        }
    }

    void Car::SetHandbrake(float value)
    {
        if (!m_vehicle_entity)
        {
            return;
        }
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleHandbrake(value);
        }
    }

    void Car::ResetToSpawn()
    {
        if (!m_vehicle_entity)
        {
            return;
        }
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            math::Vector3 reset_position = m_spawn_position + math::Vector3(0.0f, get_car_lower_extent(::car::tuning::spec) + car_spawn_margin, 0.0f);
            physics->SetBodyTransform(reset_position, math::Quaternion::Identity);
            m_chase_camera.initialized = false;
        }
    }

    void Car::CycleView()
    {
        m_current_view = static_cast<CarView>((static_cast<int>(m_current_view) + 1) % 3);
        ConfigureCameraForView();
    }

    void Car::SetView(CarView view)
    {
        m_current_view = view;
        ConfigureCameraForView();
    }

    Entity* Car::FindCameraEntity() const
    {
        const char* name = "component_camera";

        Entity* camera = nullptr;
        if (m_body_entity)
        {
            camera = m_body_entity->GetChildByName(name);
        }
        if (!camera && m_vehicle_entity)
        {
            camera = m_vehicle_entity->GetChildByName(name);
        }
        if (!camera && default_camera)
        {
            camera = default_camera->GetChildByName(name);
        }

        return camera;
    }

    void Car::ConfigureCameraForView()
    {
        Entity* camera = FindCameraEntity();
        if (!camera)
        {
            return;
        }

        if (m_current_view == CarView::Chase)
        {
            if (default_camera)
            {
                camera->SetParent(default_camera);
            }
            m_chase_camera.initialized = false;
        }
        else if (m_current_view == CarView::Hood)
        {
            camera->SetParent(m_body_entity);
            // hood position
            math::Quaternion camera_correction = m_body_entity->GetRotationLocal().Inverse();
            camera->SetPositionLocal(math::Vector3(0.0f, 0.8f, -1.0f));
            camera->SetRotationLocal(camera_correction);
        }
        else
        {
            ConfigureWheelCamera(camera);
        }
    }

    void Car::ConfigureWheelCamera(Entity* camera)
    {
        if (!camera || !m_vehicle_entity)
        {
            return;
        }

        // mount on the chassis so the wheel visibly spins, steers and travels with the suspension
        camera->SetParent(m_vehicle_entity);

        // base the mount on the front left wheel rest position in vehicle local space
        math::Vector3 wheel_local = math::Vector3(-0.8f, -0.3f, 1.3f);
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            if (Entity* wheel = physics->GetWheelEntity(WheelIndex::FrontLeft))
            {
                wheel_local = wheel->GetPositionLocal();
            }
        }

        // mount outboard, above and slightly behind the wheel, left side is toward negative x
        const float side_offset = 0.55f;
        const float up_offset   = 0.20f;
        const float back_offset = 0.80f;
        math::Vector3 camera_local = wheel_local + math::Vector3(-side_offset, up_offset, -back_offset);

        // look forward and slightly down so the wheel stays in the lower part of the frame
        math::Vector3 look_target = wheel_local + math::Vector3(0.0f, -0.1f, 2.5f);
        math::Vector3 look_dir    = (look_target - camera_local).Normalized();

        camera->SetPositionLocal(camera_local);
        camera->SetRotationLocal(math::Quaternion::FromLookRotation(look_dir, math::Vector3::Up));
    }

    void Car::AddCameraOrbitYaw(float delta)
    {
        m_chase_camera.yaw_bias += delta;
        // wrap to keep float precision after many rotations, sin and cos give identical results
        const float two_pi = 2.0f * math::pi;
        if (m_chase_camera.yaw_bias >  math::pi)
        {
            m_chase_camera.yaw_bias -= two_pi;
        }
        if (m_chase_camera.yaw_bias < -math::pi)
        {
            m_chase_camera.yaw_bias += two_pi;
        }
    }

    void Car::AddCameraOrbitPitch(float delta)
    {
        m_chase_camera.pitch_bias += delta;
        m_chase_camera.pitch_bias = std::clamp(m_chase_camera.pitch_bias, -pitch_bias_max, pitch_bias_max);
    }

    // private helpers

    math::Vector3 Car::SmoothDamp(const math::Vector3& current, const math::Vector3& target, 
                                   math::Vector3& velocity, float smooth_time, float dt)
    {
        float omega = 2.0f / std::max(smooth_time, 0.0001f);
        float x = omega * dt;
        float exp_factor = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
        math::Vector3 delta = current - target;
        math::Vector3 temp = (velocity + omega * delta) * dt;
        velocity = (velocity - omega * temp) * exp_factor;
        return target + (delta + temp) * exp_factor;
    }

    float Car::LerpAngle(float a, float b, float t)
    {
        float diff = fmodf(b - a + math::pi * 3.0f, math::pi * 2.0f) - math::pi;
        return a + diff * t;
    }

    math::BoundingBox Car::GetCarAABB() const
    {
        if (!m_body_entity)
        {
            return math::BoundingBox::Unit;
        }

        math::BoundingBox combined(math::Vector3::Infinity, math::Vector3::InfinityNeg);
        std::vector<Entity*> descendants;
        m_body_entity->GetDescendants(&descendants);
        descendants.push_back(m_body_entity);

        for (Entity* entity : descendants)
        {
            if (Render* renderable = entity->GetComponent<Render>())
            {
                combined.Merge(renderable->GetBoundingBox());
            }
        }

        return combined;
    }

    Entity* Car::CreateBody(std::vector<Entity*>* out_excluded_entities)
    {
        if (!m_definition)
        {
            return nullptr;
        }
        if (m_definition->body_model.empty())
        {
            Entity* car_entity = World::CreateEntity();
            car_entity->SetObjectName(FileSystem::GetFileNameWithoutExtensionFromFilePath(m_definition->file_path));
            car_entity->AddTag("body");
            auto create_part = [&](const char* name, const math::Vector3& position, const math::Vector3& scale)
            {
                Entity* part = World::CreateEntity();
                part->SetObjectName(name);
                part->SetParent(car_entity);
                part->SetPositionLocal(position);
                part->SetScaleLocal(scale);
                Render* renderable = part->AddComponent<Render>();
                renderable->SetMesh(MeshType::Cube);
                renderable->SetDefaultMaterial();
            };
            create_part("generic_lower_body", math::Vector3(0.0f, -::car::cfg.height * 0.18f, 0.0f), math::Vector3(::car::cfg.width * 0.92f, ::car::cfg.height * 0.38f, ::car::cfg.length * 0.84f));
            create_part("generic_cabin", math::Vector3(0.0f, ::car::cfg.height * 0.22f, -::car::cfg.length * 0.08f), math::Vector3(::car::cfg.width * 0.68f, ::car::cfg.height * 0.42f, ::car::cfg.length * 0.48f));
            create_part("generic_roof", math::Vector3(0.0f, ::car::cfg.height * 0.48f, -::car::cfg.length * 0.08f), math::Vector3(::car::cfg.width * 0.72f, ::car::cfg.height * 0.05f, ::car::cfg.length * 0.52f));
            return car_entity;
        }

        uint32_t mesh_flags  = Mesh::GetDefaultFlags();
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);

        std::shared_ptr<Mesh> mesh_car = ResourceCache::Load<Mesh>(m_definition->body_model, mesh_flags);
        if (!mesh_car)
        {
            return nullptr;
        }

        Entity* mesh_root = mesh_car->GetRootEntity();
        if (!mesh_root)
        {
            return nullptr;
        }

        // mesh root is shared via the resource cache, clone so every car instance gets its own hierarchy
        // the root itself is transient so it never leaks into the world file on save
        Entity* car_entity = mesh_root->Clone();
        car_entity->SetActive(true);
        mesh_root->SetActive(false);
        mesh_root->SetTransient(true);

        car_entity->SetObjectName(FileSystem::GetFileNameWithoutExtensionFromFilePath(m_definition->file_path));
        car_entity->SetScale(m_definition->body_scale);
        car_entity->AddTag("body");

        // deactivate the baked in parts the definition hides, the spawned wheel entities replace them
        {
            std::vector<Entity*> descendants;
            car_entity->GetDescendants(&descendants);

            for (Entity* descendant : descendants)
            {
                std::string entity_name = to_lower_copy(descendant->GetObjectName());

                bool is_excluded_part = false;
                for (const std::string& part : m_definition->body_hide_parts)
                {
                    if (entity_name.find(to_lower_copy(part)) != std::string::npos)
                    {
                        is_excluded_part = true;
                        break;
                    }
                }

                if (is_excluded_part)
                {
                    descendant->SetActive(false);

                    if (out_excluded_entities)
                    {
                        out_excluded_entities->push_back(descendant);
                    }
                }
            }
        }

        // material presets
        {
            std::vector<Entity*> descendants;
            car_entity->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                Render* renderable = descendant->GetComponent<Render>();
                if (!renderable || !renderable->GetMaterial())
                {
                    continue;
                }

                const std::string context = get_material_context(descendant, renderable);

                switch (resolve_car_material_slot(descendant, renderable))
                {
                    case CarMaterialSlot::BodyPaint:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "body_paint"))
                        {
                            material->ApplyPaintPreset(m_paint_preset, m_paint_color, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::CarbonTrim:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "carbon_trim"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::CarbonFiber, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::TireRubber:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "tire_rubber"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::RubberTire, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::RimMetal:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "rim_metal"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::Chrome, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::HeadlightLens:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "headlight_lens"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::HeadlightLens, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::TaillightLens:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "taillight_lens"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::TaillightLens, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::MainGlass:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "main_glass"))
                        {
                            // smoked engine covers use tinted glass, windshields and side glass stay clear
                            const MaterialSurfacePreset glass_preset = contains(context, "engine")
                                ? MaterialSurfacePreset::GlassTinted
                                : MaterialSurfacePreset::GlassClear;
                            material->ApplySurfacePreset(glass_preset, false);
                        }

                        if (contains(context, "object_58") || contains(context, "windshield"))
                        {
                            m_window_entity      = descendant;
                            default_car_window   = descendant;
                        }
                        break;
                    }
                    case CarMaterialSlot::MirrorGlass:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "mirror_glass"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::Chrome, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::EngineMetal:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "engine_metal"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::PolishedMetal, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::BrakeDisc:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "brake_disc"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::BrakeDisc, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::InteriorLeather:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "interior_leather"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::Leather, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::BlackTrim:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "black_trim"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::BlackPlastic, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::EmissiveRedLight:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "emissive_red_light"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::EmissiveRedLight, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::EmissiveWhiteLight:
                    {
                        if (std::shared_ptr<Material> material = clone_car_material(car_entity, descendant, renderable, "emissive_white_light"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::EmissiveWhiteLight, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::Unknown:
                    default:
                    {
                        break;
                    }
                }
            }
        }

        return car_entity;
    }

    Entity* Car::SpawnWheelBase()
    {
        if (!m_definition || m_definition->wheel_model.empty())
        {
            return nullptr;
        }

        uint32_t mesh_flags  = Mesh::GetDefaultFlags();
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);

        std::shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>(m_definition->wheel_model, mesh_flags);
        if (!mesh)
        {
            return nullptr;
        }

        Entity* wheel_root = mesh->GetRootEntity();
        if (!wheel_root)
        {
            return nullptr;
        }

        Entity* wheel_source = wheel_root->GetChildByIndex(0);
        if (!wheel_source)
        {
            return nullptr;
        }

        // the mesh root is shared via the resource cache, clone so every car gets its own wheels
        // the root itself is transient so it never leaks into the world file on save
        Entity* wheel_base = wheel_source->Clone();
        wheel_base->SetParent(nullptr);
        wheel_base->SetActive(true);
        wheel_root->SetActive(false);
        wheel_root->SetTransient(true);

        // measure wheel dimensions from the natural mesh scale
        wheel_base->SetScale(1.0f);

        if (Render* renderable = wheel_base->GetComponent<Render>())
        {
            Material* material = renderable->GetMaterial();
            if (!m_definition->wheel_albedo.empty())
            {
                material->SetTexture(MaterialTextureType::Color, m_definition->wheel_albedo);
            }
            if (!m_definition->wheel_metalness.empty())
            {
                material->SetTexture(MaterialTextureType::Metalness, m_definition->wheel_metalness);
            }
            if (!m_definition->wheel_normal.empty())
            {
                material->SetTexture(MaterialTextureType::Normal, m_definition->wheel_normal);
            }
            if (!m_definition->wheel_roughness.empty())
            {
                material->SetTexture(MaterialTextureType::Roughness, m_definition->wheel_roughness);
            }
            material->SetProperty(MaterialProperty::MotionBlurRadial, 1.0f);
        }

        return wheel_base;
    }

    void Car::CreateWheels(Entity* vehicle_ent, Physics* physics)
    {
        Entity* wheel_base = SpawnWheelBase();
        if (!wheel_base)
        {
            return;
        }

        const ::car::car_preset& preset = ::car::tuning::spec;
        const float front_wheel_radius  = preset.front_wheel_radius > 0.0f ? preset.front_wheel_radius : 0.34f;
        const float rear_wheel_radius   = preset.rear_wheel_radius  > 0.0f ? preset.rear_wheel_radius  : 0.35f;
        const float front_wheel_width   = preset.front_wheel_width  > 0.0f ? preset.front_wheel_width  : 0.245f;
        const float rear_wheel_width    = preset.rear_wheel_width   > 0.0f ? preset.rear_wheel_width   : 0.305f;
        Entity* wheel_fl = wheel_base;
        Entity* wheel_fr = wheel_base->Clone();
        Entity* wheel_rl = wheel_base->Clone();
        Entity* wheel_rr = wheel_base->Clone();
        physics->ScaleWheelEntityToDimensions(wheel_fl, front_wheel_radius, front_wheel_width);
        physics->ScaleWheelEntityToDimensions(wheel_fr, front_wheel_radius, front_wheel_width);
        physics->ScaleWheelEntityToDimensions(wheel_rl, rear_wheel_radius, rear_wheel_width);
        physics->ScaleWheelEntityToDimensions(wheel_rr, rear_wheel_radius, rear_wheel_width);

        const float suspension_height   = physics->GetSuspensionHeight();
        const float preset_wheelbase    = preset.wheelbase   > 0.0f ? preset.wheelbase   : 2.6f;
        const float preset_track_front  = preset.track_front > 0.0f ? preset.track_front : 1.6f;
        const float preset_track_rear   = preset.track_rear  > 0.0f ? preset.track_rear  : 1.6f;
        const float front_z             = preset_wheelbase   * 0.5f;
        const float rear_z              = -preset_wheelbase  * 0.5f;
        const float half_track_front    = preset_track_front * 0.5f;
        const float half_track_rear     = preset_track_rear  * 0.5f;
        const float wheel_y             = -suspension_height;

        // front left
        wheel_fl->SetObjectName("wheel_front_left");
        wheel_fl->SetParent(vehicle_ent);
        wheel_fl->SetPositionLocal(math::Vector3(-half_track_front, wheel_y, front_z));
        tag_wheel(wheel_fl, true, true);

        // front right
        wheel_fr->SetObjectName("wheel_front_right");
        wheel_fr->SetParent(vehicle_ent);
        wheel_fr->SetPositionLocal(math::Vector3(half_track_front, wheel_y, front_z));
        wheel_fr->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));
        tag_wheel(wheel_fr, true, false);

        // rear left
        wheel_rl->SetObjectName("wheel_rear_left");
        wheel_rl->SetParent(vehicle_ent);
        wheel_rl->SetPositionLocal(math::Vector3(-half_track_rear, wheel_y, rear_z));
        tag_wheel(wheel_rl, false, true);

        // rear right
        wheel_rr->SetObjectName("wheel_rear_right");
        wheel_rr->SetParent(vehicle_ent);
        wheel_rr->SetPositionLocal(math::Vector3(half_track_rear, wheel_y, rear_z));
        wheel_rr->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));
        tag_wheel(wheel_rr, false, false);

        physics->SetWheelEntity(WheelIndex::FrontLeft,  wheel_fl);
        physics->SetWheelEntity(WheelIndex::FrontRight, wheel_fr);
        physics->SetWheelEntity(WheelIndex::RearLeft,   wheel_rl);
        physics->SetWheelEntity(WheelIndex::RearRight,  wheel_rr);
    }

    void Car::CreatePropWheels(Entity* root, const std::vector<Entity*>& baked_wheel_entities)
    {
        if (!m_definition)
        {
            return;
        }

        // where the nose of the body model points along its local z axis
        const float forward_z = m_definition->body_forward_z < 0.0f ? -1.0f : 1.0f;

        // measure the baked in tires so the spawned wheels land exactly in the wheel arches
        struct WheelSpot
        {
            math::Vector3 position_local;
            float radius   = 0.0f;
            bool  is_front = false;
        };
        std::vector<WheelSpot> spots;

        const math::Matrix root_inverse = root->GetMatrix().Inverted();
        for (Entity* baked : baked_wheel_entities)
        {
            if (to_lower_copy(baked->GetObjectName()).find("tire") == std::string::npos)
            {
                continue;
            }

            // the hide filter also catches the children of a tire group (tread, rim, disc),
            // measure only the top level group so each wheel yields exactly one spot
            bool is_nested_tire_part = false;
            for (Entity* ancestor = baked->GetParent(); ancestor; ancestor = ancestor->GetParent())
            {
                if (to_lower_copy(ancestor->GetObjectName()).find("tire") != std::string::npos)
                {
                    is_nested_tire_part = true;
                    break;
                }
            }
            if (is_nested_tire_part)
            {
                continue;
            }

            // the renderable can live on a child node, merge every render bound under the part.
            // world bounds are built from the mesh bbox and the entity matrix directly because
            // Render::UpdateAabb falls back to an identity transform on inactive entities and
            // these tires were just deactivated by CreateBody
            math::BoundingBox aabb = math::BoundingBox::Zero;
            std::vector<Entity*> parts;
            parts.push_back(baked);
            baked->GetDescendants(&parts);
            for (Entity* part : parts)
            {
                if (Render* renderable = part->GetComponent<Render>())
                {
                    const math::BoundingBox part_aabb = renderable->GetBoundingBoxMesh() * part->GetMatrix();
                    if (aabb == math::BoundingBox::Zero)
                    {
                        aabb = part_aabb;
                    }
                    else
                    {
                        aabb.Merge(part_aabb);
                    }
                }
            }

            const math::Vector3 extents = aabb.GetExtents();
            if (aabb == math::BoundingBox::Zero || !extents.IsFinite() || extents.y <= 0.01f)
            {
                continue;
            }

            WheelSpot spot;
            spot.position_local = root_inverse * aabb.GetCenter();
            spot.radius         = extents.y; // half the tire height is its radius
            spots.push_back(spot);
        }

        // wheels only car or unexpected model, fall back to the performance geometry
        if (spots.size() != 4)
        {
            if (!spots.empty())
            {
                SP_LOG_WARNING("expected 4 tire groups but measured %zu, using preset geometry for the wheels", spots.size());
            }
            spots.clear();
            const ::car::car_preset& performance = m_definition->performance;
            const float wheelbase   = performance.wheelbase   > 0.0f ? performance.wheelbase   : 2.6f;
            const float track_front = performance.track_front > 0.0f ? performance.track_front : 1.6f;
            const float track_rear  = performance.track_rear  > 0.0f ? performance.track_rear  : 1.6f;
            const float front_z     = wheelbase * 0.5f * forward_z;
            const float rear_z      = -front_z;
            spots.push_back({ math::Vector3(-track_front * 0.5f, 0.0f, front_z), 0.34f });
            spots.push_back({ math::Vector3( track_front * 0.5f, 0.0f, front_z), 0.34f });
            spots.push_back({ math::Vector3(-track_rear  * 0.5f, 0.0f, rear_z),  0.34f });
            spots.push_back({ math::Vector3( track_rear  * 0.5f, 0.0f, rear_z),  0.34f });
        }

        // the pair on the nose side of the axle midpoint is the front axle
        float mid_z = 0.0f;
        for (const WheelSpot& spot : spots)
        {
            mid_z += spot.position_local.z;
        }
        mid_z /= static_cast<float>(spots.size());
        for (WheelSpot& spot : spots)
        {
            spot.is_front = (spot.position_local.z - mid_z) * forward_z > 0.0f;
        }

        Entity* wheel_base = SpawnWheelBase();
        if (!wheel_base)
        {
            return;
        }

        for (size_t i = 0; i < spots.size(); i++)
        {
            const WheelSpot& spot = spots[i];
            Entity* wheel         = (i == spots.size() - 1) ? wheel_base : wheel_base->Clone();

            scale_wheel_to_radius(wheel, spot.radius);
            wheel->SetParent(root);
            wheel->SetPositionLocal(spot.position_local);

            // rims face outward, the outboard side follows the x sign
            const bool is_left = (spot.position_local.x * forward_z) < 0.0f;
            if (spot.position_local.x > 0.0f)
            {
                wheel->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));
            }

            std::string name = "wheel_";
            name += spot.is_front ? "front_" : "rear_";
            name += is_left ? "left" : "right";
            wheel->SetObjectName(name);
            tag_wheel(wheel, spot.is_front, is_left);
        }
    }

    void Car::CreateAudioSources(Entity* parent_entity)
    {
        // initialize sound synthesizers
        engine_sound::initialize(48000);
        tire_squeal_sound::initialize(48000);

        // engine start (still uses a sample for the starter motor sound)
        {
            Entity* sound = World::CreateEntity();
            sound->SetObjectName("sound_start");
            sound->SetParent(parent_entity);

            AudioSource* audio_source = sound->AddComponent<AudioSource>();
            audio_source->SetAudioClip("project\\music\\car_start.wav");
            audio_source->SetLoop(false);
            audio_source->SetPlayOnStart(false);
        }

        // engine sound (synthesized)
        {
            Entity* sound = World::CreateEntity();
            sound->SetObjectName("sound_engine");
            sound->SetParent(parent_entity);

            AudioSource* audio_source = sound->AddComponent<AudioSource>();
            audio_source->SetLoop(true);
            audio_source->SetPlayOnStart(false);
            audio_source->SetVolume(0.8f);
        }

        // door open/close
        {
            Entity* sound = World::CreateEntity();
            sound->SetObjectName("sound_door");
            sound->SetParent(parent_entity);

            AudioSource* audio_source = sound->AddComponent<AudioSource>();
            audio_source->SetAudioClip("project\\music\\car_door.wav");
            audio_source->SetLoop(false);
            audio_source->SetPlayOnStart(false);
        }

        // tire squeal (synthesized)
        {
            Entity* sound = World::CreateEntity();
            sound->SetObjectName("sound_tire_squeal");
            sound->SetParent(parent_entity);

            AudioSource* audio_source = sound->AddComponent<AudioSource>();
            audio_source->SetLoop(true);
            audio_source->SetPlayOnStart(false);
            audio_source->SetVolume(0.0f);
        }
    }

    void Car::Tick()
    {
        if (!m_body_entity)
        {
            return;
        }

        // lazy camera finding - needed because parallel entity loading means camera might not exist during prefab creation
        // prefer the player flycam (camera under a controller body) over cinematic sequence cameras
        if (m_camera_follows && !default_camera)
        {
            std::vector<Entity*> root_entities;
            World::GetRootEntities(root_entities);

            Entity* fallback = nullptr;
            for (Entity* root_entity : root_entities)
            {
                std::vector<Entity*> descendants;
                root_entity->GetDescendants(&descendants);
                descendants.push_back(root_entity);

                for (Entity* entity : descendants)
                {
                    if (!entity->GetComponent<Camera>())
                    {
                        continue;
                    }

                    Entity* parent = entity->GetParent() ? entity->GetParent() : entity;
                    if (Physics* physics = parent->GetComponent<Physics>())
                    {
                        if (physics->GetBodyType() == BodyType::Controller)
                        {
                            default_camera = parent;
                            break;
                        }
                    }

                    if (!fallback)
                    {
                        fallback = parent;
                    }
                }

                if (default_camera)
                {
                    break;
                }
            }

            if (!default_camera)
            {
                default_camera = fallback;
            }
        }

        // auto-enter car when play mode starts if camera_follows is enabled
        {
            bool is_playing = Engine::IsFlagSet(EngineMode::Playing);
            if (m_camera_follows && !m_is_occupied && is_playing && !m_was_playing)
            {
                Enter();
            }
            m_was_playing = is_playing;
        }

        TickInput();
        TickSounds();
        TickChaseCamera();
        TickEnterExit();
        TickViewSwitch();
        TickVisualization();

        if (m_is_occupied)
        {
            Physics* hud_physics = m_vehicle_entity ? m_vehicle_entity->GetComponent<Physics>() : nullptr;
            car_hud::draw_driver_hud(hud_physics);
            if (m_show_telemetry)
            {
                car_hud::draw_telemetry_window(this, hud_physics, &m_show_telemetry);
            }
        }

        // osd controls cheat sheet, top left as tidy rows, each row reads action then keyboard or mouse then gamepad
        if (m_is_occupied)
        {
            Renderer::DrawString(
                "CONTROLS   key / mouse  >  gamepad\n"
                "Gas\tUp\tR2\n"
                "Brake\tDown\tL2\n"
                "Steer\tL/R\tLStick\n"
                "Hbrk\tSpace\tCircle\n"
                "Shift\tPgUp/Dn\tL1/R1\n"
                "Light\tL\tDpadUp\n"
                "View\tV\tTri\n"
                "ReCam\tC\tR3\n"
                "Look\tRClk\tRStick\n"
                "Reset\tR\tCross\n"
                "Exit\tE\tSquare",
                math::Vector2(0.006f, 0.03f));
        }
    }

    void Car::TickInput()
    {
        if (!m_vehicle_entity || !m_is_occupied)
        {
            return;
        }

        Physics* physics = m_vehicle_entity->GetComponent<Physics>();
        if (!physics || !Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        bool is_gamepad_connected = Input::IsGamepadConnected();
        float dt = static_cast<float>(Timer::GetDeltaTimeSec());

        // mcp owns pedals when flagged so keyboard zeros do not overwrite agent input
        float throttle  = physics->GetVehicleThrottle();
        float brake     = physics->GetVehicleBrake();
        float steering  = physics->GetVehicleSteering();
        float handbrake = physics->GetVehicleHandbrake();
        if (!m_mcp_controlled)
        {
            throttle = 0.0f;
            if (is_gamepad_connected)
            {
                throttle = Input::GetGamepadTriggerRight();
            }
            if (Input::GetKey(KeyCode::Arrow_Up))
            {
                throttle = 1.0f;
            }

            brake = 0.0f;
            if (is_gamepad_connected)
            {
                brake = Input::GetGamepadTriggerLeft();
            }
            if (Input::GetKey(KeyCode::Arrow_Down))
            {
                brake = 1.0f;
            }

            steering = 0.0f;
            if (is_gamepad_connected)
            {
                steering = Input::GetGamepadThumbStickLeft().x;
            }
            if (Input::GetKey(KeyCode::Arrow_Left))
            {
                steering = -1.0f;
            }
            if (Input::GetKey(KeyCode::Arrow_Right))
            {
                steering = 1.0f;
            }

            handbrake = (Input::GetKey(KeyCode::Space) || Input::GetKey(KeyCode::Button_East)) ? 1.0f : 0.0f;

            physics->SetVehicleThrottle(throttle);
            physics->SetVehicleBrake(brake);
            physics->SetVehicleSteering(steering);
            physics->SetVehicleHandbrake(handbrake);
        }

        // camera orbit (mouse right_click drag and or gamepad right thumb stick)
        if (m_current_view == CarView::Chase)
        {
            // mouse right_click drag
            {
                bool rmb_down      = Input::GetKeyDown(KeyCode::Click_Right);
                bool rmb_held      = Input::GetKey(KeyCode::Click_Right);
                bool mouse_in_view = Input::GetMouseIsInViewport();

                if (rmb_down && mouse_in_view && !m_orbit_mouse_active)
                {
                    m_orbit_mouse_active        = true;
                    m_orbit_mouse_last_position = Input::GetMousePosition();
                    if (!Window::IsFullScreen())
                    {
                        Input::SetMouseCursorVisible(false);
                    }
                }
                else if (m_orbit_mouse_active && !rmb_held)
                {
                    Input::SetMousePosition(m_orbit_mouse_last_position);
                    if (!Window::IsFullScreen())
                    {
                        Input::SetMouseCursorVisible(true);
                    }
                    m_orbit_mouse_active = false;
                }

                if (m_orbit_mouse_active)
                {
                    math::Vector2 mouse_delta = Input::GetMouseDelta();
                    AddCameraOrbitYaw(mouse_delta.x * mouse_orbit_sensitivity_yaw);
                    AddCameraOrbitPitch(mouse_delta.y * mouse_orbit_sensitivity_pitch);
                }
            }

            // gamepad right thumb stick
            if (is_gamepad_connected)
            {
                math::Vector2 right_stick = Input::GetGamepadThumbStickRight();

                if (fabsf(right_stick.x) > 0.3f)
                {
                    AddCameraOrbitYaw(right_stick.x * orbit_bias_speed * dt);
                }

                if (fabsf(right_stick.y) > 0.3f)
                {
                    AddCameraOrbitPitch(right_stick.y * orbit_bias_speed * dt);
                }
            }

            // manual recenter, keyboard c or right stick click
            if (Input::GetKeyDown(KeyCode::C) || Input::GetKeyDown(KeyCode::Right_Stick))
            {
                m_chase_camera.yaw_bias   = 0.0f;
                m_chase_camera.pitch_bias = 0.0f;
            }
        }

        // reset to spawn
        if (!m_mcp_controlled && (Input::GetKeyDown(KeyCode::R) || Input::GetKeyDown(KeyCode::Button_South)))
        {
            ResetToSpawn();
        }

        // toggle telemetry window
        if (Input::GetKeyDown(KeyCode::F3))
        {
            m_show_telemetry = !m_show_telemetry;
        }

        // manual gear shifting (gran turismo style: L1/pgdn down, R1/pgup up)
        if (!m_mcp_controlled && (Input::GetKeyDown(KeyCode::Left_Shoulder) || Input::GetKeyDown(KeyCode::Page_Down)))
        {
            physics->ShiftDown();
        }
        if (!m_mcp_controlled && (Input::GetKeyDown(KeyCode::Right_Shoulder) || Input::GetKeyDown(KeyCode::Page_Up)))
        {
            physics->ShiftUp();
        }

        // haptic feedback
        if (is_gamepad_connected)
        {
            float left_motor  = 0.0f;
            float right_motor = 0.0f;

            float max_slip_ratio = 0.0f;
            float max_slip_angle = 0.0f;
            for (int i = 0; i < 4; i++)
            {
                WheelIndex wheel = static_cast<WheelIndex>(i);
                max_slip_ratio = std::max(max_slip_ratio, fabsf(physics->GetWheelSlipRatio(wheel)));
                max_slip_angle = std::max(max_slip_angle, fabsf(physics->GetWheelSlipAngle(wheel)));
            }

            if (max_slip_ratio > 0.15f)
            {
                float slip_intensity = std::clamp((max_slip_ratio - 0.15f) * 1.5f, 0.0f, 1.0f);
                left_motor += slip_intensity * 0.5f;
            }

            if (max_slip_angle > 0.15f)
            {
                float drift_intensity = std::clamp((max_slip_angle - 0.15f) * 2.0f, 0.0f, 1.0f);
                left_motor  += drift_intensity * 0.3f;
                right_motor += drift_intensity * 0.2f;
            }

            if (physics->IsAbsActiveAny())
            {
                static float abs_pulse = 0.0f;
                abs_pulse += dt * 25.0f;
                float pulse_value = (sinf(abs_pulse * math::pi * 2.0f) + 1.0f) * 0.5f;
                right_motor += pulse_value * 0.6f;
                left_motor  += pulse_value * 0.3f;
            }

            if (brake > 0.8f && !physics->IsAbsActiveAny())
            {
                right_motor += (brake - 0.8f) * 0.4f;
            }

            left_motor  = std::clamp(left_motor, 0.0f, 1.0f);
            right_motor = std::clamp(right_motor, 0.0f, 1.0f);
            Input::GamepadVibrate(left_motor, right_motor);
        }
    }

    void Car::TickSounds()
    {
        if (!m_vehicle_entity)
        {
            return;
        }

        Entity* sound_engine_entity = m_vehicle_entity->GetChildByName("sound_engine");
        Entity* sound_tire_entity   = m_vehicle_entity->GetChildByName("sound_tire_squeal");
        AudioSource* audio_engine   = sound_engine_entity ? sound_engine_entity->GetComponent<AudioSource>() : nullptr;
        AudioSource* audio_tire     = sound_tire_entity ? sound_tire_entity->GetComponent<AudioSource>() : nullptr;
        Physics* physics            = m_vehicle_entity->GetComponent<Physics>();

        // engine sound
        if (m_is_occupied && physics && audio_engine)
        {
            float engine_rpm  = physics->GetEngineRPM();
            float throttle    = physics->GetVehicleThrottle();
            float boost       = physics->GetBoostPressure();
            float idle_rpm    = physics->GetIdleRPM();
            float redline_rpm = physics->GetRedlineRPM();
            float rpm_normalized = std::clamp((engine_rpm - idle_rpm) / (redline_rpm - idle_rpm), 0.0f, 1.0f);

            if (!audio_engine->IsSynthesisMode())
            {
                audio_engine->SetSynthesisMode(true, [](float* buffer, int num_samples)
                {
                    engine_sound::generate(buffer, num_samples, true);
                });
            }

            if (!audio_engine->IsPlaying())
            {
                audio_engine->StartSynthesis();
            }

            // update synthesizer parameters
            float load = throttle * (0.5f + rpm_normalized * 0.5f);
            engine_sound::set_parameters(engine_rpm, throttle, load, boost);

            // engine was overpowering at full level, keep the rpm and throttle response but scale it way down
            const float engine_volume_scale = 0.18f;
            float volume = (0.6f + rpm_normalized * 0.3f + throttle * 0.1f) * engine_volume_scale;
            audio_engine->SetVolume(volume);
        }
        else if (!m_is_occupied && audio_engine && audio_engine->IsPlaying())
        {
            audio_engine->StopSynthesis();
        }

        // tire squeal
        if (audio_tire && physics)
        {
            float speed_kmh = physics->GetLinearVelocity().Length() * 3.6f;

            float max_slip_angle = 0.0f;
            float max_slip_ratio = 0.0f;
            int grounded_count   = 0;

            for (int i = 0; i < 4; i++)
            {
                WheelIndex wheel = static_cast<WheelIndex>(i);
                if (physics->IsWheelGrounded(wheel))
                {
                    grounded_count++;
                    max_slip_angle = std::max(max_slip_angle, fabsf(physics->GetWheelSlipAngle(wheel)));
                    max_slip_ratio = std::max(max_slip_ratio, fabsf(physics->GetWheelSlipRatio(wheel)));
                }
            }

            const float slip_angle_threshold = 0.35f;
            const float slip_ratio_threshold = 0.28f;
            const float min_speed_for_squeal = 20.0f;

            float target_intensity = 0.0f;
            if (speed_kmh > min_speed_for_squeal && grounded_count > 0)
            {
                float slip_angle_excess = max_slip_angle - slip_angle_threshold;
                float slip_ratio_excess = max_slip_ratio - slip_ratio_threshold;

                if (slip_angle_excess > 0.0f || slip_ratio_excess > 0.0f)
                {
                    float slip_angle_intensity = std::clamp(slip_angle_excess * 1.5f, 0.0f, 1.0f);
                    float slip_ratio_intensity = std::clamp(slip_ratio_excess * 1.8f, 0.0f, 1.0f);
                    target_intensity = std::max(slip_angle_intensity, slip_ratio_intensity);
                }
            }

            // smooth the intensity to avoid abrupt changes
            float fade_rate = (target_intensity > m_tire_squeal_volume) ? 0.04f : 0.025f;
            m_tire_squeal_volume += (target_intensity - m_tire_squeal_volume) * fade_rate;

            // feed parameters into the synthesizer
            float speed_normalized = std::clamp(speed_kmh / 200.0f, 0.0f, 1.0f);
            tire_squeal_sound::set_parameters(m_tire_squeal_volume, speed_normalized);

            if (m_tire_squeal_volume > 0.02f)
            {
                if (!audio_tire->IsSynthesisMode())
                {
                    audio_tire->SetSynthesisMode(true, [](float* buffer, int num_samples)
                    {
                        tire_squeal_sound::generate(buffer, num_samples, true);
                    });
                }

                if (!audio_tire->IsPlaying())
                {
                    audio_tire->StartSynthesis();
                }

                const float max_volume = 0.25f;
                audio_tire->SetVolume(m_tire_squeal_volume * max_volume);
            }
            else
            {
                m_tire_squeal_volume = 0.0f;
                if (audio_tire->IsPlaying())
                {
                    audio_tire->StopSynthesis();
                }
            }
        }
    }

    void Car::TickChaseCamera()
    {
        if (!m_is_occupied || m_current_view != CarView::Chase || !m_vehicle_entity || !default_camera)
        {
            return;
        }

        Entity* camera = default_camera->GetChildByName("component_camera");
        if (!camera)
        {
            camera = m_vehicle_entity->GetChildByName("component_camera");
            if (!camera)
            {
                camera = m_body_entity->GetChildByName("component_camera");
            }
            if (camera)
            {
                camera->SetParent(default_camera);
                m_chase_camera.initialized = false;
            }
        }

        if (!camera)
        {
            return;
        }

        Physics* car_physics = m_vehicle_entity->GetComponent<Physics>();
        float dt = static_cast<float>(Timer::GetDeltaTimeSec());

        math::Vector3 car_position = m_vehicle_entity->GetPosition();
        math::Vector3 car_forward  = m_vehicle_entity->GetForward();
        math::Vector3 car_right    = m_vehicle_entity->GetRight();
        math::Vector3 car_velocity = car_physics ? car_physics->GetLinearVelocity() : math::Vector3::Zero;
        float car_speed = car_velocity.Length();

        float target_yaw = atan2f(car_forward.x, car_forward.z);

        float target_speed_factor = std::clamp(car_speed / chase_speed_reference, 0.0f, 1.0f);
        m_chase_camera.speed_factor += (target_speed_factor - m_chase_camera.speed_factor) * 
            std::min(1.0f, chase_speed_smoothing * dt);

        float dynamic_distance = chase_distance_base - 
            (chase_distance_base - chase_distance_min) * m_chase_camera.speed_factor;
        float dynamic_height = chase_height_base - 
            (chase_height_base - chase_height_min) * m_chase_camera.speed_factor;

        if (!m_chase_camera.initialized)
        {
            m_chase_camera.yaw          = target_yaw;
            m_chase_camera.yaw_bias     = 0.0f;
            m_chase_camera.pitch_bias   = 0.0f;
            m_chase_camera.speed_factor = target_speed_factor;
            m_chase_camera.position     = car_position - math::Vector3(sinf(target_yaw), 0.0f, cosf(target_yaw)) * dynamic_distance
                                        + math::Vector3::Up * dynamic_height;
            m_chase_camera.velocity     = math::Vector3::Zero;
            m_chase_camera.initialized  = true;
        }

        float rotation_speed = chase_rotation_smoothing * (1.0f + m_chase_camera.speed_factor * 0.5f);
        m_chase_camera.yaw = LerpAngle(m_chase_camera.yaw, target_yaw, 1.0f - expf(-rotation_speed * dt));

        float effective_yaw   = m_chase_camera.yaw + m_chase_camera.yaw_bias;
        float effective_pitch = m_chase_camera.pitch_bias;

        float horizontal_scale = cosf(effective_pitch);
        float vertical_offset  = sinf(effective_pitch) * dynamic_distance;

        math::Vector3 offset_direction = math::Vector3(sinf(effective_yaw), 0.0f, cosf(effective_yaw));
        math::Vector3 target_position  = car_position 
                                       - offset_direction * dynamic_distance * horizontal_scale
                                       + math::Vector3::Up * (dynamic_height + vertical_offset);

        float slip_intensity = 0.0f;
        if (car_physics)
        {
            for (uint32_t i = 0; i < 4; i++)
            {
                WheelIndex wheel = static_cast<WheelIndex>(i);
                float slip_angle = fabsf(car_physics->GetWheelSlipAngle(wheel));
                float slip_ratio = fabsf(car_physics->GetWheelSlipRatio(wheel));
                slip_intensity = std::max(slip_intensity, std::max(slip_angle * 0.9f, slip_ratio * 0.7f));
            }
            slip_intensity = std::clamp(slip_intensity, 0.0f, 1.0f);
        }

        float shake_phase = static_cast<float>(Timer::GetTimeSec()) * (16.0f + m_chase_camera.speed_factor * 16.0f);
        float shake_strength = slip_intensity * 0.055f + m_chase_camera.speed_factor * 0.01f;
        target_position += car_right * sinf(shake_phase) * shake_strength;
        target_position += math::Vector3::Up * cosf(shake_phase * 1.37f) * shake_strength * 0.55f;

        float position_smooth = chase_position_smoothing * (1.0f - m_chase_camera.speed_factor * 0.3f);
        m_chase_camera.position = SmoothDamp(m_chase_camera.position, target_position, 
                                             m_chase_camera.velocity, position_smooth, dt);

        math::Vector3 velocity_xz = math::Vector3(car_velocity.x, 0.0f, car_velocity.z);
        float velocity_xz_len = velocity_xz.Length();
        math::Vector3 look_ahead = math::Vector3::Zero;
        if (velocity_xz_len > 2.0f)
        {
            look_ahead = (velocity_xz / velocity_xz_len) * chase_look_ahead_amount * m_chase_camera.speed_factor;
        }
        math::Vector3 look_at = car_position + math::Vector3::Up * chase_look_offset_up + look_ahead;

        camera->SetPosition(m_chase_camera.position);
        if (Camera* camera_component = camera->GetComponent<Camera>())
        {
            float fov = 90.0f + m_chase_camera.speed_factor * 5.0f + slip_intensity * 1.5f;
            camera_component->SetFovHorizontalDeg(fov);
        }

        math::Vector3 look_direction = (look_at - m_chase_camera.position).Normalized();
        camera->SetRotation(math::Quaternion::FromLookRotation(look_direction, math::Vector3::Up));
    }

    void Car::TickEnterExit()
    {
        if (!m_is_drivable)
        {
            return;
        }

        // keyboard: E, gamepad: west button (X on xbox, square on playstation)
        if (Input::GetKeyDown(KeyCode::E) || Input::GetKeyDown(KeyCode::Button_West))
        {
            if (m_is_occupied)
            {
                // don't allow exit if car is moving too fast
                const float max_exit_speed_kmh = 5.0f;
                if (m_vehicle_entity)
                {
                    if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
                    {
                        float speed_kmh = physics->GetLinearVelocity().Length() * 3.6f;
                        if (speed_kmh > max_exit_speed_kmh)
                        {
                            return;
                        }
                    }
                }

                Exit();
            }
            else
            {
                Enter();
            }
        }
    }

    void Car::TickViewSwitch()
    {
        if (!m_is_occupied)
        {
            return;
        }

        // triangle for view change (gran turismo style)
        if (Input::GetKeyDown(KeyCode::V) || Input::GetKeyDown(KeyCode::Button_North))
        {
            CycleView();
        }
    }

}
