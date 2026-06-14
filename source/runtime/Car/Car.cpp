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
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/Physics.h"
#include "../World/Prefab.h"
#include "../IO/pugixml.hpp"
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

    Car* Car::Create(const Config& config)
    {
        Car* car = new Car();
        car->m_spawn_position = config.position;
        car->m_show_telemetry = config.show_telemetry;
        car->m_is_drivable    = config.drivable;

        if (config.drivable)
        {
            // create vehicle entity with physics
            car->m_vehicle_entity = World::CreateEntity();
            car->m_vehicle_entity->SetObjectName("vehicle");
            car->m_vehicle_entity->SetPosition(config.position);

            Physics* physics = car->m_vehicle_entity->AddComponent<Physics>();
            physics->SetStatic(false);
            // mass comes from the active car preset, applied inside car::setup
            // we seed a sensible fallback so Physics::GetMass returns something useful before vehicle setup
            physics->SetMass(::car::tuning::spec.mass > 0.0f ? ::car::tuning::spec.mass : 1500.0f);
            physics->SetBodyType(BodyType::Vehicle);
            physics->SetCar(car);  // car ticks automatically through entity system

            // create car body (without its original wheels)
            std::vector<Entity*> excluded_wheel_entities;
            car->m_body_entity = car->CreateBody(true, &excluded_wheel_entities);
            if (car->m_body_entity)
            {
                car->m_body_entity->SetParent(car->m_vehicle_entity);
                car->m_body_entity->SetPositionLocal(math::Vector3(0.0f, ::car::get_chassis_visual_offset_y(), 0.07f));
                car->m_body_entity->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Right, math::pi * 0.5f));
                car->m_body_entity->SetScaleLocal(1.1f);

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
            // non-drivable display car
            car->m_body_entity = car->CreateBody(false);
            if (car->m_body_entity)
            {
                car->m_body_entity->SetPosition(config.position);

                if (config.static_physics)
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
            }

            car->CreateAudioSources(car->m_body_entity);
            default_car = car->m_body_entity;
        }

        s_cars.push_back(car);
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
        config.drivable       = node.attribute("drivable").as_bool(false);
        config.static_physics = node.attribute("static_physics").as_bool(false);
        config.show_telemetry = node.attribute("telemetry").as_bool(false);
        config.camera_follows = node.attribute("camera_follows").as_bool(false);

        // note: camera finding is now deferred to Tick() to support parallel entity loading

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

        // stop any vibration
        Input::GamepadVibrate(0.0f, 0.0f);
    }

    std::vector<Car*>& Car::GetAll()
    {
        return s_cars;
    }

    void Car::Destroy()
    {
        auto it = std::find(s_cars.begin(), s_cars.end(), this);
        if (it != s_cars.end())
        {
            s_cars.erase(it);
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

        // hide window when inside
        if (m_window_entity)
        {
            m_window_entity->SetActive(false);
        }
    }

    void Car::Exit()
    {
        if (!m_is_occupied)
        {
            return;
        }

        m_is_occupied = false;
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
            // lift the car above ground to prevent collision issues on reset
            math::Vector3 reset_position = m_spawn_position + math::Vector3(0.0f, 0.5f, 0.0f);
            physics->SetBodyTransform(reset_position, math::Quaternion::Identity);
            m_chase_camera.initialized = false;
        }
    }

    void Car::CycleView()
    {
        m_current_view = static_cast<CarView>((static_cast<int>(m_current_view) + 1) % 3);
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

    Entity* Car::CreateBody(bool remove_wheels, std::vector<Entity*>* out_excluded_entities)
    {
        uint32_t mesh_flags  = Mesh::GetDefaultFlags();
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);

        std::shared_ptr<Mesh> mesh_car = ResourceCache::Load<Mesh>("project\\models\\ferrari_laferrari\\scene.gltf", mesh_flags);
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
        Entity* car_entity = mesh_root->Clone();
        car_entity->SetActive(true);
        mesh_root->SetActive(false);

        car_entity->SetObjectName("ferrari_laferrari");
        car_entity->SetScale(2.0f);

        auto to_lower = [](std::string s)
        {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
            return s;
        };

        // sync tire and brake disc active state with remove_wheels so cloned bodies match the requested config
        {
            std::vector<Entity*> descendants;
            car_entity->GetDescendants(&descendants);

            for (Entity* descendant : descendants)
            {
                std::string entity_name = to_lower(descendant->GetObjectName());

                bool is_excluded_part = entity_name.find("tire 1")    != std::string::npos ||
                                       entity_name.find("tire 2")    != std::string::npos ||
                                       entity_name.find("tire 3")    != std::string::npos ||
                                       entity_name.find("tire 4")    != std::string::npos ||
                                       entity_name.find("brakerear") != std::string::npos;

                if (is_excluded_part)
                {
                    descendant->SetActive(!remove_wheels);

                    if (remove_wheels && out_excluded_entities)
                    {
                        out_excluded_entities->push_back(descendant);
                    }
                }
            }
        }

        // material tweaks
        {
            // body main - red clearcoat paint
            if (Material* material = car_entity->GetDescendantByName("Object_12")->GetComponent<Render>()->GetMaterial())
            {
                material->SetResourceName("car_paint" + std::string(EXTENSION_MATERIAL));
                material->SetProperty(MaterialProperty::Roughness, 0.0f);
                material->SetProperty(MaterialProperty::Clearcoat, 1.0f);
                material->SetProperty(MaterialProperty::Clearcoat_Roughness, 0.1f);
                material->SetColor(Color(100.0f / 255.0f, 0.0f, 0.0f, 1.0f));
                material->SetProperty(MaterialProperty::Normal, 0.03f);
                material->SetProperty(MaterialProperty::TextureTilingX, 100.0f);
                material->SetProperty(MaterialProperty::TextureTilingY, 100.0f);
            }

            // body metallic/carbon parts
            if (Material* material = car_entity->GetDescendantByName("Object_10")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.4f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // tires - rubber
            {
                const char* tire_parts[] = {"Object_127", "Object_142", "Object_157", "Object_172"};
                for (const char* part : tire_parts)
                {
                    if (Material* material = car_entity->GetDescendantByName(part)->GetComponent<Render>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.9f);
                    }
                }
            }

            // rims - polished metal
            if (Material* material = car_entity->GetDescendantByName("Object_180")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
                material->SetProperty(MaterialProperty::Roughness, 0.3f);
            }
            if (Material* material = car_entity->GetDescendantByName("Object_150")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
                material->SetProperty(MaterialProperty::Roughness, 0.3f);
            }

            // headlight and taillight glass
            if (Material* material = car_entity->GetDescendantByName("Object_38")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.5f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // windshield and engine glass
            if (Material* material = car_entity->GetDescendantByName("Object_58")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.0f);
                material->SetProperty(MaterialProperty::Metalness, 0.0f);
            }

            // side mirror glass
            if (Material* material = car_entity->GetDescendantByName("Object_98")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.0f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // engine block
            if (Material* material = car_entity->GetDescendantByName("Object_14")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.4f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // brake discs - anisotropic metal
            {
                const char* brake_parts[] = {"Object_129", "Object_144", "Object_174", "Object_159"};
                for (const char* part : brake_parts)
                {
                    if (Material* material = car_entity->GetDescendantByName(part)->GetComponent<Render>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                        material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                        material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                    }
                }
            }

            // interior leather
            if (Material* material = car_entity->GetDescendantByName("Object_90")->GetComponent<Render>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.75f);
            }
        }

        return car_entity;
    }

    void Car::CreateWheels(Entity* vehicle_ent, Physics* physics)
    {
        uint32_t mesh_flags  = Mesh::GetDefaultFlags();
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessOptimize);
        mesh_flags          &= ~static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods);

        std::shared_ptr<Mesh> mesh = ResourceCache::Load<Mesh>("project\\models\\wheel\\model.blend", mesh_flags);
        if (!mesh)
        {
            return;
        }

        Entity* wheel_root = mesh->GetRootEntity();
        if (!wheel_root)
        {
            return;
        }

        Entity* wheel_base = wheel_root->GetChildByIndex(0);
        if (!wheel_base)
        {
            return;
        }

        wheel_base->SetParent(nullptr);
        World::RemoveEntityImmediate(wheel_root);
        mesh->SetRootEntity(nullptr);
        // start at unit scale so ScaleWheelEntityToRadius can measure the natural mesh size
        // and rescale it to match the preset's target wheel radius. a hardcoded 0.2 here was
        // undersizing the wheel to ~0.21 m so the cooked sweep cylinder could never reach the
        // ground and target_compression was permanently clamped to zero in steady state
        wheel_base->SetScale(1.0f);

        if (Render* renderable = wheel_base->GetComponent<Render>())
        {
            Material* material = renderable->GetMaterial();
            material->SetTexture(MaterialTextureType::Color,     "project\\models\\wheel\\albedo.jpeg");
            material->SetTexture(MaterialTextureType::Metalness, "project\\models\\wheel\\metalness.png");
            material->SetTexture(MaterialTextureType::Normal,    "project\\models\\wheel\\normal.png");
            material->SetTexture(MaterialTextureType::Roughness, "project\\models\\wheel\\roughness.png");
        }

        // rescale wheel mesh so its visual radius matches the physics target radius.
        // ComputeWheelRadiusFromEntity is still called so it can update the mesh center
        // offset used to place the visual wheel, but its measured radius is unreliable
        // in some load orderings, so SetWheelRadius is called last with the target to
        // guarantee the cooked sweep cylinder, body height and wheel moi all agree
        const float target_wheel_radius = physics->GetTargetWheelRadius();
        physics->ScaleWheelEntityToRadius(wheel_base, target_wheel_radius);
        physics->ComputeWheelRadiusFromEntity(wheel_base);
        physics->SetWheelRadius(target_wheel_radius);

        // read from tuning spec instead of cfg, cfg can be raced to zero by a parallel car setup
        const ::car::car_preset& preset = ::car::tuning::spec;
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
        Entity* wheel_fl = wheel_base;
        wheel_fl->SetObjectName("wheel_front_left");
        wheel_fl->SetParent(vehicle_ent);
        wheel_fl->SetPositionLocal(math::Vector3(-half_track_front, wheel_y, front_z));

        // front right
        Entity* wheel_fr = wheel_base->Clone();
        wheel_fr->SetObjectName("wheel_front_right");
        wheel_fr->SetParent(vehicle_ent);
        wheel_fr->SetPositionLocal(math::Vector3(half_track_front, wheel_y, front_z));
        wheel_fr->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));

        // rear left
        Entity* wheel_rl = wheel_base->Clone();
        wheel_rl->SetObjectName("wheel_rear_left");
        wheel_rl->SetParent(vehicle_ent);
        wheel_rl->SetPositionLocal(math::Vector3(-half_track_rear, wheel_y, rear_z));

        // rear right
        Entity* wheel_rr = wheel_base->Clone();
        wheel_rr->SetObjectName("wheel_rear_right");
        wheel_rr->SetParent(vehicle_ent);
        wheel_rr->SetPositionLocal(math::Vector3(half_track_rear, wheel_y, rear_z));
        wheel_rr->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));

        physics->SetWheelEntity(WheelIndex::FrontLeft,  wheel_fl);
        physics->SetWheelEntity(WheelIndex::FrontRight, wheel_fr);
        physics->SetWheelEntity(WheelIndex::RearLeft,   wheel_rl);
        physics->SetWheelEntity(WheelIndex::RearRight,  wheel_rr);
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
        if (m_camera_follows && !default_camera)
        {
            std::vector<Entity*> root_entities;
            World::GetRootEntities(root_entities);
            
            for (Entity* root_entity : root_entities)
            {
                std::vector<Entity*> descendants;
                root_entity->GetDescendants(&descendants);
                descendants.push_back(root_entity);
                
                for (Entity* entity : descendants)
                {
                    if (entity->GetComponent<Camera>())
                    {
                        default_camera = entity->GetParent() ? entity->GetParent() : entity;
                        break;
                    }
                }
                if (default_camera)
                {
                    break;
                }
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

        if (m_is_occupied)
        {
            Physics* hud_physics = m_vehicle_entity ? m_vehicle_entity->GetComponent<Physics>() : nullptr;
            car_hud::draw_driver_hud(hud_physics);
            if (m_show_telemetry)
            {
                car_hud::draw_telemetry_window(hud_physics, &m_show_telemetry);
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
                "Shift\t-\tL1/R1\n"
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

        // throttle
        float throttle = 0.0f;
        if (is_gamepad_connected)
        {
            throttle = Input::GetGamepadTriggerRight();
        }
        if (Input::GetKey(KeyCode::Arrow_Up))
        {
            throttle = 1.0f;
        }

        // brake
        float brake = 0.0f;
        if (is_gamepad_connected)
        {
            brake = Input::GetGamepadTriggerLeft();
        }
        if (Input::GetKey(KeyCode::Arrow_Down))
        {
            brake = 1.0f;
        }

        // steering
        float steering = 0.0f;
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

        // handbrake
        float handbrake = (Input::GetKey(KeyCode::Space) || Input::GetKey(KeyCode::Button_East)) ? 1.0f : 0.0f;

        physics->SetVehicleThrottle(throttle);
        physics->SetVehicleBrake(brake);
        physics->SetVehicleSteering(steering);
        physics->SetVehicleHandbrake(handbrake);

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
        if (Input::GetKeyDown(KeyCode::R) || Input::GetKeyDown(KeyCode::Button_South))
        {
            ResetToSpawn();
        }

        // toggle telemetry window
        if (Input::GetKeyDown(KeyCode::F3))
        {
            m_show_telemetry = !m_show_telemetry;
        }

        // manual gear shifting (gran turismo style: L1 down, R1 up)
        if (Input::GetKeyDown(KeyCode::Left_Shoulder))
        {
            physics->ShiftDown();
        }
        if (Input::GetKeyDown(KeyCode::Right_Shoulder))
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

            audio_engine->SetSynthesisMode(true, [](float* buffer, int num_samples)
            {
                engine_sound::generate(buffer, num_samples, true);
            });

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
                audio_tire->SetSynthesisMode(true, [](float* buffer, int num_samples)
                {
                    tire_squeal_sound::generate(buffer, num_samples, true);
                });

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
