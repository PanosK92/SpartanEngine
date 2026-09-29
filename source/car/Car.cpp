/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===============================
#include "pch.h"
#include "Car.h"
#include "AiDriver.h"
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
#include "../world/components/CarReset.h"
#include "../world/components/SpawnPoint.h"
#include "../world/Prefab.h"
#include "../io/pugixml.hpp"
#include <mutex>
//==========================================

namespace spartan
{
    std::vector<Car*> Car::s_cars;
    Car* Car::s_spectated          = nullptr;
    bool Car::s_car_picker_on_foot = false;

    // shared entities use external linkage and resolve lazily during world load
    Entity* default_camera = nullptr;

    namespace
    {
        // car prefabs are created from multiple world loading threads, s_cars is shared
        std::mutex car_list_mutex;
        std::once_flag audio_synthesizers_once;
        constexpr float car_spawn_margin = 1.0f;
        // how far from the car surface the player can be and still get in
        constexpr float car_enter_reach = 2.5f;

        float get_car_lower_extent(const car::car_preset& preset)
        {
            const float wheel_extent = preset.suspension_height + std::max(preset.front_wheel_radius, preset.rear_wheel_radius);
            return std::max(preset.height * 0.5f, wheel_extent);
        }

        float get_body_visual_offset_y(const car::car_definition& definition, Physics* physics)
        {
            // The shared Ferrari mesh keeps its original origin correction. Using the
            // recipient's suspension height here would lower the buggy shell back
            // down to the wheels and cancel its extra chassis clearance.
            constexpr float ferrari_body_offset_y = -(1.116f * 0.5f + 0.3f) + 0.1f;
            return definition.body_is_placeholder
                ? ferrari_body_offset_y
                : physics->GetVehicleSimulation()->get_chassis_visual_offset_y();
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
            Render* render = wheel_entity->GetComponent<Render>();
            if (!render || !std::isfinite(target_radius) || target_radius <= 0.0f)
            {
                return;
            }

            const math::Vector3 extents  = render->GetBoundingBoxMesh().GetExtents();
            const float measured_radius  = std::max({ extents.x, extents.y, extents.z });
            if (!std::isfinite(measured_radius) || measured_radius <= 1e-5f)
            {
                return;
            }

            wheel_entity->SetScaleLocal(math::Vector3(target_radius / measured_radius));
        }

        std::string resolve_car_file(const std::string& file)
        {
            const std::string relative = file.empty() ? "project/cars/ferrari_laferrari.car" : file;

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

        std::string get_material_context(Entity* entity, Render* render)
        {
            std::string context;
            for (Entity* current = entity; current != nullptr; current = current->GetParent())
            {
                context += " ";
                context += to_lower_copy(current->GetObjectName());
            }

            if (render)
            {
                context += " ";
                context += to_lower_copy(render->GetMaterialName());
            }

            return context;
        }

        CarMaterialSlot resolve_car_material_slot(Entity* entity, Render* render)
        {
            const std::string context = get_material_context(entity, render);

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

        using CarMaterialClones =
            std::map<
                std::pair<Material*, std::string>,
                std::shared_ptr<Material>
            >;

        std::shared_ptr<Material> clone_car_material(
            Entity* car_entity,
            Render* render,
            const char* slot_name,
            CarMaterialClones& clones
        )
        {
            Material* source = render ? render->GetMaterial() : nullptr;
            if (!source)
            {
                return nullptr;
            }

            const auto key = std::make_pair(
                source,
                std::string(slot_name)
            );
            const auto existing = clones.find(key);
            if (existing != clones.end())
            {
                render->SetMaterial(existing->second);
                return existing->second;
            }

            const std::string resource_name =
                "car_" +
                std::to_string(car_entity->GetObjectId()) +
                "_" +
                std::to_string(source->GetObjectId()) +
                "_" +
                slot_name +
                std::string(EXTENSION_MATERIAL);
            std::shared_ptr<Material> material = source->Clone(resource_name);
            material->SetPersistent(false);
            render->SetMaterial(material);
            clones.emplace(key, material);
            return material;
        }

    }

    Car* Car::Create(const Config& config)
    {
        SP_PROFILE_CPU();
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
        car->m_show_telemetry = config.show_telemetry;
        car->m_dyno = config.dyno;
        car->m_is_drivable    = config.drivable;
        car->m_paint_preset   = config.paint_preset;
        car->m_paint_color    = config.paint_color;
        car->m_customize_materials = config.customize_materials;

        if (config.drivable)
        {
            // create vehicle entity with physics
            car->m_vehicle_entity = World::CreateEntity();
            car->m_vehicle_entity->SetObjectName("vehicle");
            car->m_vehicle_entity->SetPosition(config.position);
            car->m_vehicle_entity->AddTag("car");

            Physics* physics = car->m_vehicle_entity->AddComponent<Physics>();
            physics->SetStatic(false);
            physics->SetMass(definition->performance.mass > 0.0f ? definition->performance.mass : 1500.0f);
            physics->SetVehiclePreset(definition->performance);
            physics->SetVehicleSimMode(config.vehicle_sim_mode);
            physics->SetBodyType(BodyType::Vehicle);
            physics->SetCar(car);  // car ticks automatically through entity system

            // create car body (without its baked in wheels)
            std::vector<Entity*> excluded_wheel_entities;
            car->m_body_entity = car->CreateBody(&excluded_wheel_entities);
            if (car->m_body_entity)
            {
                car->m_body_entity->SetParent(car->m_vehicle_entity);
                car->m_body_entity->SetPositionLocal(math::Vector3(0.0f, get_body_visual_offset_y(*definition, physics), 0.07f));
                car->m_body_entity->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Right, math::pi * 0.5f));
                car->m_body_entity->SetScaleLocal(1.1f);

                physics->SetChassisEntity(car->m_body_entity, excluded_wheel_entities);
            }

            car->CreateAudioSources(car->m_vehicle_entity);
            car->CreateWheels(car->m_vehicle_entity, physics, excluded_wheel_entities);

            // skid marks are defined in the world as a prefab override on the vehicle, not added here

            // camera follow enters the car when play mode starts
            car->m_camera_follows = config.camera_follows;

        }
        else
        {
            // display cars keep the same visual hierarchy without vehicle physics
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
        config.dyno           = node.attribute("dyno").as_bool(false);
        config.camera_follows = node.attribute("camera_follows").as_bool(false);
        config.customize_materials = node.attribute("customize_materials").as_bool(true);
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
        std::lock_guard<std::mutex> lock(car_list_mutex);
        for (Car* car : s_cars)
        {
            if (car->m_vehicle_entity)
            {
                if (Physics* physics = car->m_vehicle_entity->GetComponent<Physics>())
                {
                    physics->SetCar(nullptr);
                }
            }
            car->m_vehicle_entity = nullptr;
            car->m_body_entity    = nullptr;
            car->m_window_entity  = nullptr;
            delete car;
        }
        s_cars.clear();
        s_spectated = nullptr;

        default_camera = nullptr;

        // stop any vibration
        Input::GamepadStopFeedback();
    }

    std::vector<Car*> Car::GetAll()
    {
        std::lock_guard<std::mutex> lock(car_list_mutex);
        return s_cars;
    }

    void Car::SetVehicleSimMode(VehicleSimMode mode)
    {
        if (!m_vehicle_entity)
        {
            return;
        }
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleSimMode(mode);
        }
    }

    VehicleSimMode Car::GetVehicleSimMode() const
    {
        if (!m_vehicle_entity)
        {
            return VehicleSimMode::Full;
        }
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            return physics->GetVehicleSimMode();
        }
        return VehicleSimMode::Full;
    }

    static void mark_entity_tree_transient(Entity* entity)
    {
        if (!entity)
        {
            return;
        }

        entity->SetTransient(true);
        std::vector<Entity*> descendants;
        entity->GetDescendants(&descendants);
        for (Entity* descendant : descendants)
        {
            if (descendant)
            {
                descendant->SetTransient(true);
            }
        }
    }

    void Car::ClearBodyRenderStates(bool restore)
    {
        if (restore)
        {
            for (const BodyRenderState& state : m_body_render_states)
            {
                if (Entity* entity = World::GetEntityById(state.entity_id))
                {
                    entity->SetActive(state.active);
                }
            }
        }

        m_body_render_states.clear();
    }

    void Car::ApplySkeletonBodyVisibility()
    {
        ClearBodyRenderStates(true);

        // skeleton mode always hides the painted 3d mesh, collision hull is a separate debug toggle
        if (
            m_visualization_preset != CarVisualizationPreset::Skeleton ||
            !m_vehicle_entity
        )
        {
            return;
        }

        // deactivate roots, GetActive walks parents so every painted mesh under them is skipped
        auto hide_entity = [this](Entity* entity)
        {
            if (!entity)
            {
                return;
            }

            const uint64_t id = entity->GetObjectId();
            for (const BodyRenderState& state : m_body_render_states)
            {
                if (state.entity_id == id)
                {
                    entity->SetActive(false);
                    return;
                }
            }

            m_body_render_states.push_back({ id, entity->IsActive() });
            entity->SetActive(false);
        };

        hide_entity(m_body_entity);

        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            for (int i = 0; i < 4; i++)
            {
                hide_entity(
                    physics->GetWheelEntity(static_cast<WheelIndex>(i))
                );
            }
        }

        // any other render meshes attached to the vehicle root
        std::vector<Entity*> descendants;
        m_vehicle_entity->GetDescendants(&descendants);
        for (Entity* entity : descendants)
        {
            if (
                entity &&
                entity->IsActive() &&
                entity->GetComponent<Render>()
            )
            {
                hide_entity(entity);
            }
        }
    }

    void Car::SetVisualizationPreset(CarVisualizationPreset preset)
    {
        m_visualization_preset = preset;
        ApplySkeletonBodyVisibility();
    }

    void Car::PrepareForPlayStop()
    {
        if (m_is_occupied) Input::GamepadStopFeedback();
        m_haptic_initialized = false;
        if (m_visualization_preset == CarVisualizationPreset::Skeleton)
        {
            SetVisualizationPreset(CarVisualizationPreset::Full);
        }
        else
        {
            ClearBodyRenderStates(false);
        }

        m_was_playing = false;
    }

    void Car::SetSkeletonShowCollision(bool show)
    {
        m_skeleton_show_collision = show;
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

        // drop cached body pointers before destroying the mesh, restore would uaf
        ClearBodyRenderStates(false);

        const math::Vector3 current_position = m_vehicle_entity->GetPosition();
        const math::Quaternion current_rotation = m_vehicle_entity->GetRotation();
        float ground_height = current_position.y - get_car_lower_extent(m_definition->performance);
        bool has_ground_contact = false;
        for (int i = 0; i < ::car::wheel_count; i++)
        {
            const math::Vector3 contact_point = physics->GetWheelContactPoint(static_cast<WheelIndex>(i));
            if (physics->IsWheelGrounded(static_cast<WheelIndex>(i)) && std::isfinite(contact_point.y))
            {
                ground_height = has_ground_contact ? std::max(ground_height, contact_point.y) : contact_point.y;
                has_ground_contact = true;
            }
        }
        const float target_height = ground_height + get_car_lower_extent(definition->performance) + car_spawn_margin;
        // A secured car cannot settle after the road spawn's one-metre safety lift.
        // Preserve the stand's tire contact height when swapping vehicle geometry.
        const auto wheel_extent = [](const car::car_preset& p)
        {
            return p.suspension_height + std::max(p.front_wheel_radius, p.rear_wheel_radius);
        };
        const float dyno_height = current_position.y - wheel_extent(m_definition->performance) + wheel_extent(definition->performance);
        const math::Vector3 target_position(current_position.x, m_dyno ? dyno_height : std::max(current_position.y, target_height), current_position.z);
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
        physics->SetVehiclePreset(definition->performance);

        std::vector<Entity*> excluded_wheel_entities;
        m_body_entity = CreateBody(&excluded_wheel_entities);
        if (m_body_entity)
        {
            m_body_entity->SetParent(m_vehicle_entity);
            // play stop strips non transient play spawned entities, keep the body with the car
            mark_entity_tree_transient(m_body_entity);
            m_body_entity->SetPositionLocal(math::Vector3(0.0f, get_body_visual_offset_y(*definition, physics), 0.07f));
            m_body_entity->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Right, math::pi * 0.5f));
            m_body_entity->SetScaleLocal(1.1f);
            physics->SetChassisEntity(m_body_entity, excluded_wheel_entities);
        }

        if (m_dyno)
        {
            // Recreate visuals and their mesh-centre offsets for the new wheel asset.
            // Resizing a previous car's wheels retains stale placement/centre data.
            for (int i = 0; i < 4; ++i)
            {
                const auto index = static_cast<WheelIndex>(i);
                if (auto* wheel = physics->GetWheelEntity(index))
                {
                    wheel->SetActive(false);
                    World::RemoveEntity(wheel);
                }
                physics->SetWheelEntity(index, nullptr);
            }
            CreateWheels(m_vehicle_entity, physics, excluded_wheel_entities);
            for (int i = 0; i < 4; ++i)
                if (auto* wheel = physics->GetWheelEntity(static_cast<WheelIndex>(i))) mark_entity_tree_transient(wheel);
            physics->SyncWheelOffsetsFromEntities();
        }

        for (int i = 0; !m_dyno && i < 4; i++)
        {
            const WheelIndex wheel_index = static_cast<WheelIndex>(i);
            if (Entity* wheel_entity = physics->GetWheelEntity(wheel_index))
            {
                const bool is_front = i == 0 || i == 1;
                const float radius = is_front ? definition->performance.front_wheel_radius : definition->performance.rear_wheel_radius;
                const float width  = is_front ? definition->performance.front_wheel_width : definition->performance.rear_wheel_width;
                physics->ScaleWheelEntityToDimensions(wheel_entity, radius, width);
            }
        }

        if (visualization_preset == CarVisualizationPreset::Skeleton)
        {
            SetVisualizationPreset(CarVisualizationPreset::Skeleton);
        }
    }

    Car::~Car() = default;

    void Car::SetAiDriver(std::unique_ptr<AiDriver> driver)
    {
        if (m_ai_driver)
        {
            m_ai_driver->Release();
        }
        m_ai_driver = std::move(driver);
        if (m_ai_driver)
        {
            m_ai_driver->Possess(this);
        }
    }

    void Car::Destroy()
    {
        // the car goes with the driver, nothing to hand back
        m_ai_driver.reset();
        if (m_is_occupied)
        {
            Exit();
        }
        // the camera can hang off this car's body, hand it back before the hierarchy goes
        StopSpectating();

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
            if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
            {
                physics->SetCar(nullptr);
            }
            World::RemoveEntity(m_vehicle_entity);
        }
        else if (m_body_entity)
        {
            World::RemoveEntity(m_body_entity);
        }

        delete this;
    }

    void Car::SetHeadlights(CarHeadlights mode)
    {
        m_headlights = mode;
        m_rear_lamps = mode != CarHeadlights::Off;
    }

    void Car::CycleHeadlights()
    {
        SetHeadlights(static_cast<CarHeadlights>((static_cast<uint8_t>(m_headlights) + 1) % 3));
    }

    bool Car::HasLights() const
    {
        return m_vehicle_entity && (m_vehicle_entity->GetChildByName("headlight_left") || m_vehicle_entity->GetChildByName("headlight_right"));
    }

    bool Car::IsPlayerInRange() const
    {
        Entity* car_reference = m_vehicle_entity ? m_vehicle_entity : m_body_entity;
        if (!default_camera || !car_reference)
        {
            return false;
        }

        const math::Vector3 player_position = default_camera->GetPosition();
        const math::BoundingBox aabb        = GetCarAABB();
        const math::Vector3 aabb_min        = aabb.GetMin();
        const math::Vector3 aabb_max        = aabb.GetMax();

        // no renderable bounds, fall back to the car origin
        if (aabb.IsInfinite() || aabb_min.x > aabb_max.x)
        {
            return (player_position - car_reference->GetPosition()).Length() <= car_enter_reach;
        }

        // distance to the closest point on the car bounds, so a long car is not harder to enter than a short one
        const math::Vector3 closest_point = math::Vector3(
            std::clamp(player_position.x, aabb_min.x, aabb_max.x),
            std::clamp(player_position.y, aabb_min.y, aabb_max.y),
            std::clamp(player_position.z, aabb_min.z, aabb_max.z)
        );

        return (player_position - closest_point).Length() <= car_enter_reach;
    }

    void Car::Enter()
    {
        if (m_is_occupied || !m_is_drivable || m_externally_controlled)
        {
            return;
        }

        if (s_spectated)
        {
            s_spectated->StopSpectating();
        }

        for (Car* car : GetAll())
        {
            if (car && car != this && car->IsOccupied())
            {
                car->Exit();
            }
        }

        m_is_occupied = true;
        m_haptic_initialized = false;
        m_haptic_left = m_haptic_right = m_haptic_shift = 0.0f;
        m_chase_camera = {};

        // disable player physics controller so it doesn't interfere with driving
        if (default_camera)
        {
            if (Physics* controller = default_camera->GetComponent<Physics>())
            {
                controller->SetEnabled(false);
            }
        }

        ConfigureCameraForView();

        // play door sound
        if (Entity* sound_door = m_vehicle_entity ? m_vehicle_entity->GetChildByName("sound_door") : nullptr)
        {
            if (AudioSource* audio = sound_door->GetComponent<AudioSource>())
            {
                audio->PlayClip();
            }
        }
    }

    void Car::Exit(bool position_player)
    {
        if (!m_is_occupied)
        {
            return;
        }

        m_is_occupied = false;
        m_externally_controlled = false;
        m_chase_camera = {};
        // the synth is shared, the next car to be entered has to push its own spec
        m_engine_sound_configured = false;
        m_engine_sound_cranking = false;

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

        Entity* camera = FindCameraEntity();

        if (camera && default_camera)
        {
            if (Camera* component = camera->GetComponent<Camera>())
            {
                component->ResetFpsMotion();
            }
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
        if (default_camera && position_player)
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

        StopSounds();

        // play door sound
        if (
            position_player &&
            m_vehicle_entity
        )
        {
            if (
                Entity* sound_door =
                    m_vehicle_entity->GetChildByName(
                        "sound_door"
                    )
            )
            {
                if (
                    AudioSource* audio =
                        sound_door->GetComponent<AudioSource>()
                )
                {
                    audio->PlayClip();
                }
            }
        }

        // show window when outside, skeleton mode keeps all painted mesh hidden
        if (
            m_window_entity &&
            m_visualization_preset != CarVisualizationPreset::Skeleton
        )
        {
            m_window_entity->SetActive(true);
        }

        // stop vibration
        Input::GamepadStopFeedback();
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

        Entity* owner = m_vehicle_entity->GetParent();
        CarReset* car_reset =
            owner ? owner->GetComponent<CarReset>() : nullptr;
        SpawnPoint* spawn_point =
            car_reset ? car_reset->GetSpawnPoint() : nullptr;

        if (!spawn_point)
        {
            if (!m_spawn_error_logged)
            {
                SP_LOG_ERROR(
                    "car '%s' requires a valid spawn point",
                    owner ?
                        owner->GetObjectName().c_str() :
                        m_vehicle_entity->GetObjectName().c_str()
                );
                m_spawn_error_logged = true;
            }
            return;
        }

        m_haptic_initialized = false;
        m_haptic_left = m_haptic_right = m_haptic_shift = 0.0f;
        if (m_is_occupied) Input::GamepadStopFeedback();
        spawn_point->Place(m_vehicle_entity);
        m_spawn_error_logged = false;
        m_chase_camera = {};
    }

    bool Car::GetSteeringGeometry(float& wheelbase, float& max_steer_angle, float& linearity) const
    {
        Physics* physics = m_vehicle_entity ? m_vehicle_entity->GetComponent<Physics>() : nullptr;
        const ::car::Simulation* simulation = physics ? physics->GetVehicleSimulation() : nullptr;
        if (!simulation)
        {
            return false;
        }

        const ::car::car_preset& spec = simulation->get_spec();
        wheelbase       = spec.wheelbase;
        max_steer_angle = spec.max_steer_angle;
        linearity       = spec.steering_linearity;
        return true;
    }

    void Car::PlaceAt(const math::Vector3& ground_position, const math::Quaternion& rotation)
    {
        if (!m_vehicle_entity)
        {
            return;
        }

        math::Vector3 position = ground_position;
        position.y += (m_definition ? get_car_lower_extent(m_definition->performance) : 0.6f) + 0.05f;

        m_haptic_initialized = false;
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetBodyTransform(position, rotation);
            physics->SetLinearVelocity(math::Vector3::Zero);
            physics->SetAngularVelocity(math::Vector3::Zero);
        }
        m_vehicle_entity->SetPosition(position);
        m_vehicle_entity->SetRotation(rotation);
        m_chase_camera = {};
    }

    void Car::SummonToPlayer()
    {
        if (!m_vehicle_entity || m_is_occupied)
        {
            return;
        }

        Entity* origin_entity = nullptr;
        bool from_eye = false;
        if (Engine::IsFlagSet(EngineMode::Playing) && default_camera)
        {
            origin_entity = default_camera;
        }
        else if (Camera* camera = World::GetCamera())
        {
            origin_entity = camera->GetEntity();
            from_eye = true;
        }
        if (!origin_entity)
        {
            return;
        }

        math::Vector3 forward = origin_entity->GetForward();
        forward.y = 0.0f;
        if (forward.LengthSquared() < 0.001f)
        {
            forward = math::Vector3::Forward;
        }
        forward.Normalize();

        math::Vector3 position = origin_entity->GetPosition() + forward * 4.0f;
        if (from_eye)
        {
            position.y -= 1.6f;
        }
        float lift = 0.6f;
        if (m_definition)
        {
            lift = get_car_lower_extent(m_definition->performance);
        }
        position.y += lift;

        math::Quaternion rotation = math::Quaternion::FromLookRotation(
            forward,
            math::Vector3::Up
        );

        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetBodyTransform(position, rotation);
        }
        m_vehicle_entity->SetPosition(position);
        m_vehicle_entity->SetRotation(rotation);
        m_chase_camera = {};
    }

    void Car::SummonPlayerCar()
    {
        Car* player_car = nullptr;
        for (Car* car : GetAll())
        {
            if (!car || !car->IsDrivable())
            {
                continue;
            }

            Entity* root = car->GetRootEntity();
            Entity* owner = root ? root->GetParent() : nullptr;
            if (owner && owner->GetComponent<CarReset>())
            {
                player_car = car;
                break;
            }
            if (!player_car)
            {
                player_car = car;
            }
        }

        if (player_car)
        {
            player_car->SummonToPlayer();
        }
    }

    void Car::StopSounds()
    {
        // release both shared synth streams before another car can take ownership
        if (Entity* sound_engine = m_vehicle_entity ? m_vehicle_entity->GetChildByName("sound_engine") : nullptr)
        {
            if (AudioSource* audio = sound_engine->GetComponent<AudioSource>())
            {
                audio->StopSynthesis();
            }
        }
        if (Entity* sound_tire = m_vehicle_entity ? m_vehicle_entity->GetChildByName("sound_tire_squeal") : nullptr)
        {
            if (AudioSource* audio = sound_tire->GetComponent<AudioSource>())
            {
                audio->StopSynthesis();
            }
        }
        m_tire_squeal_volume      = 0.0f;
        m_engine_sound_configured = false;
        m_engine_sound_cranking   = false;
    }

    Car* Car::GetViewed()
    {
        if (s_spectated)
        {
            return s_spectated;
        }
        for (Car* car : GetAll())
        {
            if (car && car->IsOccupied())
            {
                return car;
            }
        }
        return nullptr;
    }

    std::string Car::GetDisplayName() const
    {
        std::string label;
        if (Entity* root = GetRootEntity())
        {
            // drivable cars hang a generic "vehicle" entity under the named owner
            label = root->GetObjectName();
            if (label == "vehicle" && root->GetParent())
            {
                label = root->GetParent()->GetObjectName();
            }
        }
        const std::string model = m_definition ? m_definition->name : std::string("car");
        return label.empty() ? model : model + "  /  " + label;
    }

    void Car::Spectate()
    {
        if (IsSpectated() || m_is_occupied || !m_is_drivable || !m_vehicle_entity || !Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        // one view at a time: leave the driven car where it is, or stop watching the previous one
        for (Car* car : GetAll())
        {
            if (car && car->IsOccupied())
            {
                car->Exit();
            }
        }
        if (s_spectated)
        {
            s_spectated->StopSpectating();
        }

        s_spectated    = this;
        m_chase_camera = {};
        StopSounds();

        // the player stays where they stood, the controller must not walk the camera away
        if (default_camera)
        {
            if (Physics* controller = default_camera->GetComponent<Physics>())
            {
                controller->SetEnabled(false);
            }
        }

        ConfigureCameraForView();
    }

    void Car::StopSpectating()
    {
        if (!IsSpectated())
        {
            return;
        }
        Entity* camera = FindCameraEntity();
        s_spectated    = nullptr;
        m_chase_camera = {};
        StopSounds();

        if (m_orbit_mouse_active)
        {
            Input::SetMousePosition(m_orbit_mouse_last_position);
            if (!Window::IsFullScreen())
            {
                Input::SetMouseCursorVisible(true);
            }
            m_orbit_mouse_active = false;
        }

        // back into the player's head
        Physics* controller = default_camera ? default_camera->GetComponent<Physics>() : nullptr;
        if (camera && default_camera)
        {
            if (Camera* component = camera->GetComponent<Camera>())
            {
                component->ResetFpsMotion();
            }
            camera->SetParent(default_camera);
            camera->SetRotationLocal(math::Quaternion::Identity);
            if (controller)
            {
                camera->SetPositionLocal(controller->GetControllerTopLocal());
            }
        }
        if (controller)
        {
            controller->SetEnabled(true);
        }
    }

    bool Car::IsCameraControlled(Entity* camera)
    {
        for (Car* car : s_cars)
        {
            if (car && car->IsViewed() && car->FindCameraEntity() == camera)
            {
                return true;
            }
        }
        return false;
    }

    void Car::CycleView()
    {
        m_current_view = static_cast<CarView>((static_cast<int>(m_current_view) + 1) % 3);
        if (IsViewed())
        {
            ConfigureCameraForView();
        }
    }

    void Car::SetView(CarView view)
    {
        m_current_view = view;
        // a car nobody looks at only remembers the view, taking the camera would steal it from the viewed car
        if (IsViewed())
        {
            ConfigureCameraForView();
        }
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
        // switching the view between cars can leave the camera mounted on the previous car
        if (!camera && IsViewed())
        {
            if (Camera* active = World::GetCamera())
            {
                camera = active->GetEntity();
            }
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

        if (Camera* component = camera->GetComponent<Camera>())
        {
            component->ResetFpsMotion();
        }

        if (m_current_view == CarView::Chase)
        {
            if (default_camera)
            {
                camera->SetParent(default_camera);
            }
            m_chase_camera = {};
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
            if (Render* render = entity->GetComponent<Render>())
            {
                combined.Merge(render->GetBoundingBox());
            }
        }

        return combined;
    }

    Entity* Car::CreateBody(std::vector<Entity*>* out_excluded_entities)
    {
        SP_PROFILE_CPU();
        if (!m_definition)
        {
            return nullptr;
        }
        uint32_t mesh_flags  = Mesh::GetDefaultFlags();
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
        // Retain authored LOD 0, but let distant traffic use generated LODs.

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
            CarMaterialClones material_clones;
            auto clone_material =
                [&](Render* render, const char* slot_name)
                {
                    return clone_car_material(
                        car_entity,
                        render,
                        slot_name,
                        material_clones
                    );
                };
            for (Entity* descendant : descendants)
            {
                Render* render = descendant->GetComponent<Render>();
                if (!render || !render->GetMaterial())
                {
                    continue;
                }

                const std::string context = get_material_context(descendant, render);
                const CarMaterialSlot material_slot = resolve_car_material_slot(descendant, render);
                if (material_slot == CarMaterialSlot::MainGlass && (contains(context, "object_58") || contains(context, "windshield")))
                {
                    m_window_entity = descendant;
                }
                if (!m_customize_materials)
                {
                    continue;
                }

                switch (material_slot)
                {
                    case CarMaterialSlot::BodyPaint:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "body_paint"))
                        {
                            material->ApplyPaintPreset(m_paint_preset, m_paint_color, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::CarbonTrim:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "carbon_trim"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::CarbonFiber, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::TireRubber:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "tire_rubber"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::RubberTire, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::RimMetal:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "rim_metal"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::Chrome, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::HeadlightLens:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "headlight_lens"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::HeadlightLens, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::TaillightLens:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "taillight_lens"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::TaillightLens, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::MainGlass:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "main_glass"))
                        {
                            // smoked engine covers use tinted glass, windshields and side glass stay clear
                            const MaterialSurfacePreset glass_preset = contains(context, "engine")
                                ? MaterialSurfacePreset::GlassTinted
                                : MaterialSurfacePreset::GlassClear;
                            material->ApplySurfacePreset(glass_preset, false);
                        }

                        if (contains(context, "object_58") || contains(context, "windshield"))
                        {
                            m_window_entity = descendant;
                        }
                        break;
                    }
                    case CarMaterialSlot::MirrorGlass:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "mirror_glass"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::Chrome, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::EngineMetal:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "engine_metal"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::PolishedMetal, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::BrakeDisc:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "brake_disc"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::BrakeDisc, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::InteriorLeather:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "interior_leather"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::Leather, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::BlackTrim:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "black_trim"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::BlackPlastic, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::EmissiveRedLight:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "emissive_red_light"))
                        {
                            material->ApplySurfacePreset(MaterialSurfacePreset::EmissiveRedLight, false);
                        }
                        break;
                    }
                    case CarMaterialSlot::EmissiveWhiteLight:
                    {
                        if (std::shared_ptr<Material> material =
                            clone_material(render, "emissive_white_light"))
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
        // Retain authored LOD 0, but let distant traffic use generated LODs.

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

        // A single-root glTF can put the tire directly on the imported root.
        Entity* wheel_source = wheel_root->GetComponent<Render>() ? wheel_root : wheel_root->GetChildByIndex(0);
        if (!wheel_source)
        {
            return nullptr;
        }

        // clone the cached wheel root because each car owns wheel transforms
        Entity* wheel_base = wheel_source->Clone();
        wheel_base->SetParent(nullptr);
        wheel_base->SetActive(true);
        wheel_root->SetActive(false);
        wheel_root->SetTransient(true);

        // Tires and rims stay clean even when their contact patch enters the terrain blend band.
        std::vector<Entity*> wheel_parts = {wheel_base};
        wheel_base->GetDescendants(&wheel_parts);
        for (Entity* part : wheel_parts)
        {
            if (Render* render = part->GetComponent<Render>())
            {
                render->SetFlag(RenderFlags::ExcludeFromTerrainBlend);
                bool rotates = true;
                for (Entity* ancestor = part; ancestor && ancestor != wheel_base; ancestor = ancestor->GetParent())
                {
                    if (ancestor->GetObjectName() == "brake_caliper")
                    {
                        rotates = false;
                        break;
                    }
                }
                if (Material* material = render->GetMaterial())
                    material->SetProperty(MaterialProperty::MotionBlurRadial, rotates ? 1.0f : 0.0f);
            }
        }

        // wheel bounds must be measured at unit scale before absolute dimension scaling
        wheel_base->SetScale(1.0f);

        if (Render* render = wheel_base->GetComponent<Render>())
        {
            Material* material = render->GetMaterial();
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

    void Car::CreateWheels(Entity* vehicle_ent, Physics* physics, const std::vector<Entity*>& baked_wheel_entities)
    {
        SP_PROFILE_CPU();
        Entity* wheel_base = SpawnWheelBase();
        if (!wheel_base)
        {
            return;
        }

        const ::car::car_preset& preset = m_definition->performance;
        const float front_wheel_radius  = preset.front_wheel_radius > 0.0f ? preset.front_wheel_radius : 0.34f;
        const float rear_wheel_radius   = preset.rear_wheel_radius  > 0.0f ? preset.rear_wheel_radius  : 0.35f;
        const float front_wheel_width   = preset.front_wheel_width  > 0.0f ? preset.front_wheel_width  : 0.245f;
        const float rear_wheel_width    = preset.rear_wheel_width   > 0.0f ? preset.rear_wheel_width   : 0.305f;
        Entity* wheel_fl = wheel_base;
        Entity* wheel_fr = wheel_base->Clone();
        Entity* wheel_rl = wheel_base->Clone();
        Entity* wheel_rr = wheel_base->Clone();

        const float suspension_height   = physics->GetSuspensionHeight();
        const float preset_wheelbase    = preset.wheelbase   > 0.0f ? preset.wheelbase   : 2.6f;
        const float preset_track_front  = preset.track_front > 0.0f ? preset.track_front : 1.6f;
        const float preset_track_rear   = preset.track_rear  > 0.0f ? preset.track_rear  : 1.6f;
        const float front_z             = preset_wheelbase   * 0.5f;
        const float rear_z              = -preset_wheelbase  * 0.5f;
        const float half_track_front    = preset_track_front * 0.5f;
        const float half_track_rear     = preset_track_rear  * 0.5f;
        const float wheel_y             = -suspension_height;
        struct WheelPlacement
        {
            math::Vector3 position;
            float radius;
        };
        WheelPlacement placements[4] =
        {
            { math::Vector3(-half_track_front, wheel_y, front_z), front_wheel_radius },
            { math::Vector3(half_track_front, wheel_y, front_z), front_wheel_radius },
            { math::Vector3(-half_track_rear, wheel_y, rear_z), rear_wheel_radius },
            { math::Vector3(half_track_rear, wheel_y, rear_z), rear_wheel_radius }
        };
        std::vector<WheelPlacement> measured_placements;
        const math::Matrix vehicle_inverse = vehicle_ent->GetMatrix().Inverted();
        for (Entity* baked : baked_wheel_entities)
        {
            if (to_lower_copy(baked->GetObjectName()).find("tire") == std::string::npos)
            {
                continue;
            }
            bool nested = false;
            for (Entity* ancestor = baked->GetParent(); ancestor; ancestor = ancestor->GetParent())
            {
                if (to_lower_copy(ancestor->GetObjectName()).find("tire") != std::string::npos)
                {
                    nested = true;
                    break;
                }
            }
            if (nested)
            {
                continue;
            }
            math::BoundingBox bounds = math::BoundingBox::Zero;
            std::vector<Entity*> parts = { baked };
            baked->GetDescendants(&parts);
            for (Entity* part : parts)
            {
                if (Render* render = part->GetComponent<Render>())
                {
                    const math::BoundingBox part_bounds = render->GetBoundingBoxMesh() * part->GetMatrix();
                    if (bounds == math::BoundingBox::Zero)
                    {
                        bounds = part_bounds;
                    }
                    else
                    {
                        bounds.Merge(part_bounds);
                    }
                }
            }
            const math::Vector3 extents = bounds.GetExtents();
            const float radius = std::max({ extents.x, extents.y, extents.z });
            if (bounds != math::BoundingBox::Zero && extents.IsFinite() && std::isfinite(radius) && radius > 0.01f)
            {
                measured_placements.push_back({ vehicle_inverse * bounds.GetCenter(), radius });
            }
        }
        // A placeholder supplies only the shell, not the recipient's axle geometry.
        if (!m_definition->body_is_placeholder && measured_placements.size() == 4)
        {
            float axle_midpoint = 0.0f;
            for (const WheelPlacement& placement : measured_placements)
            {
                axle_midpoint += placement.position.z;
            }
            axle_midpoint *= 0.25f;
            for (const WheelPlacement& placement : measured_placements)
            {
                const int index = placement.position.z >= axle_midpoint ? (placement.position.x < 0.0f ? 0 : 1) : (placement.position.x < 0.0f ? 2 : 3);
                placements[index] = placement;
            }
        }
        // The contact solver and visible tread must use the same unloaded radius.
        // Donor wheels locate the arches; their radius does not override the tire preset.
        physics->ScaleWheelEntityToDimensions(wheel_fl, front_wheel_radius, front_wheel_width);
        physics->ScaleWheelEntityToDimensions(wheel_fr, front_wheel_radius, front_wheel_width);
        physics->ScaleWheelEntityToDimensions(wheel_rl, rear_wheel_radius, rear_wheel_width);
        physics->ScaleWheelEntityToDimensions(wheel_rr, rear_wheel_radius, rear_wheel_width);

        // front left
        wheel_fl->SetObjectName("wheel_front_left");
        wheel_fl->SetParent(vehicle_ent);
        wheel_fl->SetPositionLocal(placements[0].position);
        tag_wheel(wheel_fl, true, true);

        // front right
        wheel_fr->SetObjectName("wheel_front_right");
        wheel_fr->SetParent(vehicle_ent);
        wheel_fr->SetPositionLocal(placements[1].position);
        wheel_fr->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));
        tag_wheel(wheel_fr, true, false);

        // rear left
        wheel_rl->SetObjectName("wheel_rear_left");
        wheel_rl->SetParent(vehicle_ent);
        wheel_rl->SetPositionLocal(placements[2].position);
        tag_wheel(wheel_rl, false, true);

        // rear right
        wheel_rr->SetObjectName("wheel_rear_right");
        wheel_rr->SetParent(vehicle_ent);
        wheel_rr->SetPositionLocal(placements[3].position);
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

            // bounds come from the mesh bbox and entity matrix directly, Render::UpdateAabb falls back to identity on inactive entities
            math::BoundingBox aabb = math::BoundingBox::Zero;
            std::vector<Entity*> parts;
            parts.push_back(baked);
            baked->GetDescendants(&parts);
            for (Entity* part : parts)
            {
                if (Render* render = part->GetComponent<Render>())
                {
                    const math::BoundingBox part_aabb = render->GetBoundingBoxMesh() * part->GetMatrix();
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
        if (m_definition->body_is_placeholder || spots.size() != 4)
        {
            if (!m_definition->body_is_placeholder && !spots.empty())
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
        SP_PROFILE_CPU();

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

        // the source above holds the shared device open, so its native rate is known now
        std::call_once(audio_synthesizers_once, []()
        {
            const int sample_rate = AudioSource::GetDeviceSampleRate();
            engine_sound::initialize(sample_rate);
            tire_squeal_sound::initialize(sample_rate);
        });

        // door open/close
        {
            Entity* sound = World::CreateEntity();
            sound->SetObjectName("sound_door");
            sound->SetParent(parent_entity);

            AudioSource* audio_source = sound->AddComponent<AudioSource>();
            audio_source->SetAudioClip("project/music/cars/car_door.wav");
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

        if (m_dyno)
        {
            Physics* physics = m_vehicle_entity ? m_vehicle_entity->GetComponent<Physics>() : nullptr;
            if (physics && physics->GetVehicleSimulation())
            {
                auto* simulation = physics->GetVehicleSimulation();
                simulation->mount_dyno(Engine::IsFlagSet(EngineMode::Playing));
                car_hud::draw_dyno_window(this, physics);
                if (Engine::IsFlagSet(EngineMode::Playing)) TickSounds();
            }
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
            if (!is_playing && m_ai_driver)
            {
                SetAiDriver(nullptr);
            }
            if (!is_playing && m_is_occupied)
            {
                Exit(false);
            }
            if (!is_playing)
            {
                StopSpectating();
            }

            const bool play_started = is_playing && !m_was_playing;
            Entity* owner =
                m_vehicle_entity ? m_vehicle_entity->GetParent() : nullptr;
            if (
                play_started &&
                owner &&
                owner->GetComponent<CarReset>()
            )
            {
                ResetToSpawn();
            }

            // only take the wheel if the player is standing next to the car, otherwise they stay on foot
            if (
                m_camera_follows &&
                !m_is_occupied &&
                play_started &&
                IsPlayerInRange()
            )
            {
                Enter();
            }
            m_was_playing = is_playing;
        }

        if (m_vehicle_entity && m_is_drivable && !Engine::IsFlagSet(EngineMode::Paused))
        {
            if (!m_surface_effects) m_surface_effects = std::make_shared<CarSurfaceEffects>();
            m_surface_effects->Tick(m_vehicle_entity, static_cast<float>(Timer::GetDeltaTimeSec()),
                Engine::IsFlagSet(EngineMode::Playing));
        }
        if (m_ai_driver && Engine::IsFlagSet(EngineMode::Playing) && !Engine::IsFlagSet(EngineMode::Paused))
        {
            m_ai_driver->Tick(std::clamp(static_cast<float>(Timer::GetDeltaTimeSec()), 0.0f, 0.1f));
        }
        TickInput();
        TickSounds();
        TickChaseCamera();
        TickEnterExit();
        TickViewSwitch();
        TickSummon();
        TickVisualization();

        if (IsViewed() && !m_cinematic)
        {
            Physics* hud_physics = m_vehicle_entity ? m_vehicle_entity->GetComponent<Physics>() : nullptr;
            car_hud::draw_driver_hud(hud_physics, !m_show_telemetry);
            if (m_show_telemetry)
            {
                car_hud::draw_telemetry_hud(this, hud_physics);
            }
        }
        TickCarPicker();

        if (IsSpectated() && !m_cinematic)
        {
            Renderer::DrawString(
                "SPECTATING   key / mouse  >  gamepad\n"
                "Cars\tF3\tTouchpad\n"
                "Light\tL\tDpadUp\n"
                "View\tV\tTri\n"
                "ReCam\tC\tR3\n"
                "Look\tRClk\tRStick",
                math::Vector2(0.006f, 0.03f));
        }

        if (m_is_occupied && !m_cinematic && m_ai_driver)
        {
            Renderer::DrawString(
                "AI DRIVING   key / mouse  >  gamepad\n"
                "Light\tL\tDpadUp\n"
                "View\tV\tTri\n"
                "ReCam\tC\tR3\n"
                "Look\tRClk\tRStick",
                math::Vector2(0.006f, 0.03f));
        }

        // osd controls cheat sheet, top left as tidy rows, each row reads action then keyboard or mouse then gamepad
        if (m_is_occupied && !m_cinematic && !m_ai_driver)
        {
            Renderer::DrawString(
                "CONTROLS   key / mouse  >  gamepad\n"
                "Gas\tUp\tR2\n"
                "Brake\tDown\tL2\n"
                "Steer\tL/R\tLStick\n"
                "Hbrk\tSpace\tCircle\n"
                "Shift\tPgUp/Dn\tR1/L1\n"
                "Auto/Man\t-\tDpadLeft\n"
                "Telem/Cars\tF3\tTouchpad\n"
                "Feedback\t-\tCreate\n"
                "Light\tL\tDpadUp\n"
                "View\tV\tTri\n"
                "ReCam\tC\tR3\n"
                "Look\tRClk\tRStick\n"
                "Reset\tR\tCross\n"
                "Summon\tH\tDpadDn\n"
                "Exit\tE\tSquare",
                math::Vector2(0.006f, 0.03f));
        }
    }

    void Car::TickInput()
    {
        if (!m_vehicle_entity || !IsViewed())
        {
            return;
        }
        // a spectator gets the camera and the hud, never the pedals, gears or reset
        const bool driving = m_is_occupied && !m_externally_controlled;

        Physics* physics = m_vehicle_entity->GetComponent<Physics>();
        if (!physics || !Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        bool is_gamepad_connected = Input::IsGamepadConnected();
        float dt = static_cast<float>(Timer::GetDeltaTimeSec());

        // an external controller owns the pedals when flagged, so keyboard zeros do not overwrite it
        float throttle  = physics->GetVehicleThrottle();
        float brake     = physics->GetVehicleBrake();
        float steering  = physics->GetVehicleSteering();
        float handbrake = physics->GetVehicleHandbrake();
        if (driving)
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
        if (driving && (Input::GetKeyDown(KeyCode::R) || Input::GetKeyDown(KeyCode::Button_South)))
        {
            ResetToSpawn();
        }

        // toggle telemetry hud
        if (Input::GetKeyDown(KeyCode::F3) || Input::GetKeyDown(KeyCode::Touchpad))
        {
            m_show_telemetry = !m_show_telemetry;
        }

        // cycle the headlights off, low beam, high beam, a spectator can switch them too
        if (Input::GetKeyDown(KeyCode::L) || Input::GetKeyDown(KeyCode::DPad_Up))
        {
            CycleHeadlights();
        }

        if (Input::GetKeyDown(KeyCode::Back))
        {
            m_controller_feedback_enabled = !m_controller_feedback_enabled;
        }
        if (driving && Input::GetKeyDown(KeyCode::DPad_Left))
        {
            if (auto* simulation = physics->GetVehicleSimulation())
                simulation->set_manual_transmission(!simulation->get_manual_transmission());
        }

        // manual gear shifting (gran turismo style: L1/pgdn down, R1/pgup up)
        if (driving && (Input::GetKeyDown(KeyCode::Left_Shoulder) || Input::GetKeyDown(KeyCode::Paddle2) || Input::GetKeyDown(KeyCode::Page_Down)))
        {
            physics->ShiftDown();
        }
        if (driving && (Input::GetKeyDown(KeyCode::Right_Shoulder) || Input::GetKeyDown(KeyCode::Paddle1) || Input::GetKeyDown(KeyCode::Page_Up)))
        {
            physics->ShiftUp();
        }

        TickControllerFeedback(physics, dt);
    }

    void Car::TickControllerFeedback(Physics* physics, float dt)
    {
        auto* simulation = physics->GetVehicleSimulation();
        if (!simulation || !Input::IsGamepadConnected() || Input::IsBlockedByUi() ||
            m_externally_controlled || !m_is_occupied || !m_controller_feedback_enabled)
        {
            Input::GamepadStopFeedback();
            m_haptic_initialized = false;
            m_haptic_left = m_haptic_right = m_haptic_shift = 0.0f;
            return;
        }

        // Discontinuities (reset, pause, long frame) must not feel like a landing.
        if (dt <= 0.0f || dt > 0.1f)
        {
            Input::GamepadStopFeedback();
            m_haptic_initialized = false;
            m_haptic_left = m_haptic_right = m_haptic_shift = 0.0f;
            return;
        }
        const float speed = physics->GetLinearVelocity().Length();
        const float motion = std::clamp(speed / 6.0f, 0.0f, 1.0f);
        float slip = 0.0f;
        float drift = 0.0f;
        float road = 0.0f;
        float bump = 0.0f;
        bool grounded = false;
        for (int i = 0; i < 4; ++i)
        {
            const float compression = simulation->get_wheel_compression(i);
            const bool contact = simulation->is_wheel_grounded(i);
            if (contact)
            {
                grounded = true;
                const float load = std::clamp(physics->GetWheelTireLoad(static_cast<WheelIndex>(i)) / 1500.0f, 0.0f, 1.0f);
                const float tread_speed = fabsf(simulation->get_wheel_angular_velocity(i)) * simulation->get_wheel_effective_radius(i);
                const float tire_motion = std::clamp((std::max(speed, tread_speed) - 0.5f) / 3.0f, 0.0f, 1.0f);
                slip = std::max(slip, std::clamp((fabsf(physics->GetWheelSlipRatio(static_cast<WheelIndex>(i))) - 0.12f) * 1.5f, 0.0f, 1.0f) * load * tire_motion);
                drift = std::max(drift, std::clamp((fabsf(physics->GetWheelSlipAngle(static_cast<WheelIndex>(i))) - 0.09f) * 2.0f, 0.0f, 1.0f) * load * motion);
                if (m_haptic_initialized)
                    bump = std::max(bump, std::clamp(fabsf(compression - m_haptic_compression[i]) / dt * 0.25f, 0.0f, 0.65f) * load);
                const auto surface = simulation->get_wheel_surface(i);
                float roughness = 0.015f;
                if (surface == car::surface_gravel) roughness = 0.18f;
                if (surface == car::surface_dirt) roughness = 0.12f;
                if (surface == car::surface_grass) roughness = 0.09f;
                road += roughness * motion * load * 0.25f;
            }
            m_haptic_compression[i] = compression;
        }

        const int gear = simulation->get_current_gear();
        if (m_haptic_initialized && gear != m_haptic_gear)
            m_haptic_shift = 0.4f;
        m_haptic_gear = gear;
        m_haptic_initialized = true;
        m_haptic_shift *= expf(-dt / 0.065f);
        m_haptic_phase = fmodf(m_haptic_phase + dt, 10.0f);

        const float throttle = physics->GetVehicleThrottle();
        const float brake = physics->GetVehicleBrake();
        const float rpm = std::clamp((physics->GetEngineRPM() - physics->GetIdleRPM()) /
            std::max(physics->GetRedlineRPM() - physics->GetIdleRPM(), 1.0f), 0.0f, 1.0f);
        const float load = std::clamp(fabsf(simulation->get_engine_output_torque()) /
            std::max(simulation->get_spec().engine_peak_torque, 1.0f), 0.0f, 1.0f);
        const bool abs = grounded && brake > 0.01f && simulation->is_abs_active_any();
        const bool tc = grounded && throttle > 0.01f && simulation->is_tc_active();
        const bool limiter = simulation->get_rev_limiter_active();
        const float pulse = 0.5f + 0.5f * sinf(m_haptic_phase * 25.0f * math::pi * 2.0f);
        const float engine = simulation->get_engine_running() ? (0.015f + load * 0.055f) * (0.6f + rpm * 0.4f) : 0.0f;
        const float texture = road * (0.55f + 0.45f * sinf(m_haptic_phase * 43.0f * math::pi * 2.0f));
        const float low = std::clamp(engine + bump + texture + drift * 0.2f + m_haptic_shift, 0.0f, 0.85f);
        const float high = std::clamp(slip * 0.3f + drift * 0.15f + texture + bump * 0.35f +
            (abs ? pulse * 0.35f : 0.0f) + (tc ? pulse * 0.2f : 0.0f) + (limiter ? pulse * 0.15f : 0.0f), 0.0f, 0.85f);
        // Fast attack and a short release preserve impacts without frame-to-frame buzzing.
        m_haptic_left = std::max(low, m_haptic_left * expf(-dt / 0.045f));
        m_haptic_right = std::max(high, m_haptic_right * expf(-dt / 0.045f));
        Input::GamepadDrivingFeedback(m_haptic_left, m_haptic_right,
            grounded ? 0.25f + brake * 0.4f : 0.1f,
            0.12f + load * 0.25f, abs, tc, rpm, limiter);
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
        if (IsViewed() && physics && audio_engine)
        {
            float engine_rpm  = physics->GetEngineRPM();
            float throttle    = physics->GetVehicleThrottle();
            float boost       = physics->GetBoostPressure();
            float idle_rpm    = physics->GetIdleRPM();
            float redline_rpm = physics->GetRedlineRPM();
            float rpm_normalized = std::clamp((engine_rpm - idle_rpm) / (redline_rpm - idle_rpm), 0.0f, 1.0f);
            car::Simulation* simulation = physics->GetVehicleSimulation();

            // the synth reads the effective spec so upgrades are heard, reconfigure only when it changes
            {
                const car::car_preset& preset          = simulation->get_spec();
                const car::car_preset& base            = simulation->get_base_spec();
                const car::active_upgrades& upgrades   = simulation->get_upgrades();
                auto stage_fraction = [](int stage, int stage_max)
                {
                    return stage_max > 0 ? std::clamp(static_cast<float>(stage) / static_cast<float>(stage_max), 0.0f, 1.0f) : 0.0f;
                };

                engine_sound::engine_config config;
                config.cylinder_count      = preset.engine_sound_cylinders;
                config.bank_count          = preset.engine_sound_banks;
                config.bank_angle_deg      = preset.engine_bank_angle_deg;
                config.idle_rpm            = preset.engine_idle_rpm;
                config.redline_rpm         = preset.engine_redline_rpm;
                config.max_rpm             = preset.engine_max_rpm;
                config.displacement_l      = preset.engine_displacement_l;
                config.bore_mm             = preset.engine_bore_mm;
                config.stroke_mm           = preset.engine_stroke_mm;
                config.compression_ratio   = preset.engine_compression_ratio;
                config.primary_length_m    = preset.exhaust_primary_length_m;
                config.collector_length_m  = preset.exhaust_collector_length_m;
                config.tailpipe_length_m   = preset.exhaust_tailpipe_length_m;
                config.muffler_level       = preset.exhaust_muffler_level;
                config.intake_runner_length_m = preset.intake_runner_length_m;
                config.intake_valve_duration_deg = preset.intake_valve_duration_deg;
                config.combustion_variation = preset.engine_combustion_variation;
                config.crank_inertia = preset.engine_inertia;
                config.turbo_bypass_valve = preset.turbo_bypass_valve;
                config.turbo_enabled       = preset.turbo_enabled && preset.boost_max_pressure > 0.0f;
                config.boost_max_pressure  = preset.boost_max_pressure;
                config.boost_wastegate_rpm = preset.boost_wastegate_rpm;
                config.engine_stage        = stage_fraction(upgrades.engine, base.engine_stage_max);
                config.exhaust_stage       = stage_fraction(upgrades.exhaust, base.exhaust_stage_max);
                config.intake_stage        = stage_fraction(upgrades.intake, base.intake_stage_max);
                config.turbo_stage         = stage_fraction(upgrades.turbo, base.turbo_stage_max);
                for (int i = 0; i < car::max_engine_cylinders; i++)
                {
                    config.firing_order[i]  = static_cast<int>(preset.engine_firing_order[i]);
                    config.cylinder_bank[i] = static_cast<int>(preset.engine_cylinder_bank[i]);
                    config.firing_intervals_deg[i] = preset.engine_firing_intervals_deg[i];
                }

                if (!m_engine_sound_configured || config != m_engine_sound_config)
                {
                    engine_sound::configure(config);
                    m_engine_sound_config     = config;
                    m_engine_sound_configured = true;
                }

                if (!audio_engine->IsSynthesisMode())
                {
                    audio_engine->SetSynthesisMode(
                        true,
                        [](float* buffer, int num_samples)
                        {
                            engine_sound::generate(buffer, num_samples, true);
                        }
                    );
                }
            }

            const car::car_preset& preset = simulation->get_spec();
            float torque_normalized = std::clamp(
                simulation->get_engine_output_torque() /
                std::max(preset.engine_peak_torque, 1.0f),
                0.0f,
                1.5f
            );
            float load = std::clamp(
                std::max(throttle * 0.15f, torque_normalized),
                0.0f,
                1.0f
            );

            engine_sound::listener_view view = engine_sound::listener_view::chase;
            if (m_current_view == CarView::Hood)
            {
                view = engine_sound::listener_view::hood;
            }
            else if (m_current_view == CarView::Wheel)
            {
                view = engine_sound::listener_view::cabin;
            }

            // closed throttle above idle with the engine being driven by the wheels: the ecu cuts fuel
            const bool overrun = simulation->get_engine_running() && throttle < 0.05f && engine_rpm > idle_rpm + 300.0f && simulation->get_engine_output_torque() <= 0.0f;
            const float gearbox_rpm = simulation->get_gearbox_input_angular_velocity() * 60.0f / (2.0f * math::pi);
            // bank 0 sits on the car's left, it moves to the right of the screen when looking at the nose
            float bank_pan = 1.0f;
            if (Camera* camera = World::GetCamera())
            {
                bank_pan = std::clamp(math::Vector3::Dot(m_vehicle_entity->GetRight(), camera->GetEntity()->GetRight()), -1.0f, 1.0f);
            }

            engine_sound::set_parameters(
                engine_rpm,
                throttle,
                load,
                boost,
                simulation->get_rev_limiter_active(),
                simulation->get_current_gear(),
                simulation->get_is_shifting(),
                view,
                gearbox_rpm,
                overrun,
                bank_pan
            );

            // the synth already breathes with load, this gain only adds the distance and the body
            const float engine_volume_scale = 1.0f;
            const float effort = std::max(throttle, load);
            float volume = (0.8f + rpm_normalized * 0.1f + effort * 0.1f) * engine_volume_scale;
            audio_engine->SetVolume(volume);

            // only a real ignition plays the starter, joining a running engine settles on its live state
            const bool cranking = simulation->get_starter_engaged();
            if (!audio_engine->IsPlaying())
            {
                if (cranking)
                {
                    engine_sound::start();
                }
                else
                {
                    engine_sound::prime();
                }
                audio_engine->StartSynthesis();
            }
            else if (cranking && !m_engine_sound_cranking)
            {
                engine_sound::start();
            }
            m_engine_sound_cranking = cranking;
        }
        else if (!IsViewed() && audio_engine && audio_engine->IsPlaying())
        {
            audio_engine->StopSynthesis();
        }

        // tire squeal
        if (audio_tire && physics && IsViewed())
        {
            float speed_kmh = physics->GetLinearVelocity().Length() * 3.6f;

            float contact_speed = 0.0f;
            float power_squared = 0.0f;
            float weighted_pan = 0.0f;
            car::Simulation* simulation = physics->GetVehicleSimulation();
            Camera* camera = World::GetCamera();

            for (int i = 0; i < 4 && simulation; i++)
            {
                WheelIndex wheel = static_cast<WheelIndex>(i);
                if (!physics->IsWheelGrounded(wheel))
                {
                    continue;
                }

                const car::wheel& state = simulation->get_wheel_state(i);
                float tread_speed = fabsf(physics->GetWheelAngularVelocity(wheel)) * simulation->get_wheel_effective_radius(i);
                contact_speed = std::max(contact_speed, std::max(speed_kmh / 3.6f, tread_speed));

                // rubber only sings on a hard paved surface, loose ground scrubs instead
                float surface = 0.0f;
                if (state.contact_surface == car::surface_asphalt || state.contact_surface == car::surface_concrete)
                {
                    surface = 1.0f;
                }
                else if (state.contact_surface == car::surface_wet_asphalt)
                {
                    surface = 0.35f;
                }

                // onset sits at the force peak: below it the patch still grips, past it the tread slides
                float onset = std::clamp((state.friction_use - 0.8f) / 0.17f, 0.0f, 1.0f);
                onset = onset * onset * (3.0f - 2.0f * onset);

                // friction power against a full grip slide at about 3 m/s, so four sliding tires outsing one
                float reference = std::max(state.tire_load, 500.0f) * 3.0f;
                float power = std::clamp(state.slip_power / reference, 0.0f, 1.5f) * onset * surface;
                power_squared += power * power;

                if (camera)
                {
                    math::Vector3 to_wheel = (physics->GetWheelContactPoint(wheel) - camera->GetEntity()->GetPosition()).Normalized();
                    weighted_pan += power * power * math::Vector3::Dot(to_wheel, camera->GetEntity()->GetRight());
                }
            }
            float target_intensity = std::clamp(sqrtf(power_squared), 0.0f, 1.0f);
            // lean toward the sliding side, exaggerated a little since the wheels sit close together
            float balance = power_squared > 1e-6f ? std::clamp(weighted_pan / power_squared * 2.0f, -1.0f, 1.0f) : 0.0f;

            // Frame-rate-independent release keeps the stream alive through the DSP tail.
            float dt = std::max(0.0f, static_cast<float>(Timer::GetDeltaTimeSec()));
            float fade_rate = 1.0f - expf(-dt / 0.12f);
            m_tire_squeal_volume = std::max(target_intensity, m_tire_squeal_volume * (1.0f - fade_rate));

            // feed parameters into the synthesizer
            float speed_normalized = std::clamp(contact_speed / 55.0f, 0.0f, 1.0f);
            tire_squeal_sound::set_parameters(target_intensity, speed_normalized, balance);

            if (m_tire_squeal_volume > 0.001f)
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
                    tire_squeal_sound::reset();
                    audio_tire->SetVolume(0.18f);
                    audio_tire->StartSynthesis();
                }
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
        else if (audio_tire && audio_tire->IsPlaying())
        {
            audio_tire->StopSynthesis();
            m_tire_squeal_volume = 0.0f;
        }
    }

    void Car::TickChaseCamera()
    {
        if (!IsViewed() || m_current_view != CarView::Chase || !m_vehicle_entity || !default_camera)
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
                m_chase_camera = {};
            }
        }

        if (!camera)
        {
            return;
        }

        Physics* car_physics = m_vehicle_entity->GetComponent<Physics>();
        const float speed = car_physics ? car_physics->GetLinearVelocity().Length() : 0.0f;
        m_chase_camera.Update(m_vehicle_entity->GetPosition(), m_vehicle_entity->GetForward(),
            speed, static_cast<float>(Timer::GetDeltaTimeSec()), m_camera_wind_shake);

        camera->SetPosition(m_chase_camera.position);
        if (Camera* camera_component = camera->GetComponent<Camera>())
            camera_component->SetFovHorizontalDeg(90.0f);
        const math::Vector3 look_direction = (m_chase_camera.look_at - m_chase_camera.position).Normalized();
        camera->SetRotation(math::Quaternion::FromLookRotation(look_direction, math::Vector3::Up));
    }

    void Car::TickEnterExit()
    {
        if (
            !Engine::IsFlagSet(EngineMode::Playing) ||
            !m_is_drivable ||
            m_externally_controlled
        )
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
            else if (IsPlayerInRange())
            {
                Enter();
            }
        }
    }

    void Car::TickViewSwitch()
    {
        if (!IsViewed())
        {
            return;
        }

        // triangle for view change (gran turismo style)
        if (Input::GetKeyDown(KeyCode::V) || Input::GetKeyDown(KeyCode::Button_North))
        {
            CycleView();
        }
    }

    void Car::TickSummon()
    {
        if (m_externally_controlled)
        {
            return;
        }
        if (Input::IsBlockedByUi())
        {
            return;
        }
        if (!Input::GetKeyDown(KeyCode::H) && !Input::GetKeyDown(KeyCode::DPad_Down))
        {
            return;
        }

        Entity* owner = m_vehicle_entity ? m_vehicle_entity->GetParent() : nullptr;
        if (!owner || !owner->GetComponent<CarReset>())
        {
            return;
        }

        SummonToPlayer();
    }

    void Car::TickCarPicker()
    {
        if (!Engine::IsFlagSet(EngineMode::Playing) || !m_is_drivable || m_cinematic)
        {
            return;
        }

        // one car draws the list per frame: the viewed car while its telemetry is up, on foot the first drivable car
        Car* viewed = GetViewed();
        if (viewed)
        {
            s_car_picker_on_foot = false;
            if (viewed != this || !m_show_telemetry)
            {
                return;
            }
        }
        else
        {
            Car* owner = nullptr;
            for (Car* car : GetAll())
            {
                if (car && car->m_is_drivable && car->GetRootEntity())
                {
                    owner = car;
                    break;
                }
            }
            if (owner != this)
            {
                return;
            }
            if (Input::GetKeyDown(KeyCode::F3) || Input::GetKeyDown(KeyCode::Touchpad))
            {
                s_car_picker_on_foot = !s_car_picker_on_foot;
            }
            if (!s_car_picker_on_foot)
            {
                return;
            }
        }

        Car* chosen = car_hud::draw_car_picker(viewed);
        if (!chosen || chosen == viewed)
        {
            return;
        }

        // the list stays open on the new car so switching again is one more click
        chosen->SetShowTelemetry(true);
        if (chosen->IsExternallyControlled())
        {
            chosen->Spectate();
        }
        else
        {
            chosen->Enter();
        }
    }
}
