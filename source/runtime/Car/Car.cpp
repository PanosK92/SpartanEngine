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
#include "CarSimulation.h"
#include "CarEngineSoundSynthesis.h"
#include "../Input/Input.h"
#include "../Rendering/Renderer.h"
#include "../Resource/ResourceCache.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../World/Components/AudioSource.h"
#include "../World/Components/Camera.h"
#include "../World/Components/Light.h"
#include "../World/Components/Physics.h"
#include "../IO/pugixml.hpp"
//==========================================

namespace spartan
{
    // static member initialization
    std::vector<Car*> Car::s_cars;

    // engine sound toggle: false = audio recording, true = synthesis
    static bool use_synthesized_engine_sound = false;

    // external references from game state (defined in Game.cpp)
    extern Entity* default_camera;
    extern Entity* default_car;
    extern Entity* default_car_window;

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
            physics->SetMass(1500.0f);
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

            // setup camera to follow if requested
            if (config.camera_follows)
            {
                // disable manual camera control if default_camera exists
                if (default_camera)
                {
                    if (Camera* camera = default_camera->GetChildByIndex(0)->GetComponent<Camera>())
                    {
                        camera->SetFlag(CameraFlags::CanBeControlled, false);
                    }
                }

                car->m_is_occupied              = true;
                car->m_chase_camera.initialized = false;
            }

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
                        if (car_part->GetComponent<Renderable>())
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

    Entity* Car::CreatePrefab(pugi::xml_node& node, Entity* parent)
    {
        Config config;
        config.position       = parent ? parent->GetPosition() : math::Vector3::Zero;
        config.drivable       = node.attribute("drivable").as_bool(false);
        config.static_physics = node.attribute("static_physics").as_bool(false);
        config.show_telemetry = node.attribute("telemetry").as_bool(false);
        config.camera_follows = node.attribute("camera_follows").as_bool(false);

        // when loading from world file, default_camera might not be set
        // find camera entity from root entities if needed
        if (config.camera_follows && !default_camera)
        {
            std::vector<Entity*> root_entities;
            World::GetRootEntities(root_entities);
            
            for (Entity* root_entity : root_entities)
            {
                // look for entity with camera component in its children
                std::vector<Entity*> descendants;
                root_entity->GetDescendants(&descendants);
                descendants.push_back(root_entity);
                
                for (Entity* entity : descendants)
                {
                    if (entity->GetComponent<Camera>())
                    {
                        // found camera, set its parent as default_camera (physics body with camera child)
                        default_camera = entity->GetParent() ? entity->GetParent() : entity;
                        break;
                    }
                }
                if (default_camera)
                    break;
            }
        }

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
            return;

        m_is_occupied = true;
        m_chase_camera.initialized = false;

        Entity* camera = default_camera ? default_camera->GetChildByName("component_camera") : nullptr;
        if (camera)
        {
            if (m_current_view == CarView::Chase)
            {
                m_chase_camera.initialized = false;
            }
            else
            {
                camera->SetParent(m_body_entity);
                // position based on view
            }

            camera->GetComponent<Camera>()->SetFlag(CameraFlags::CanBeControlled, false);
        }

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
            return;

        m_is_occupied = false;
        m_chase_camera.initialized = false;

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
            camera->GetComponent<Camera>()->SetFlag(CameraFlags::CanBeControlled, true);
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
                audio->StopClip();
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
            return;
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleThrottle(value);
        }
    }

    void Car::SetBrake(float value)
    {
        if (!m_vehicle_entity)
            return;
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleBrake(value);
        }
    }

    void Car::SetSteering(float value)
    {
        if (!m_vehicle_entity)
            return;
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleSteering(value);
        }
    }

    void Car::SetHandbrake(float value)
    {
        if (!m_vehicle_entity)
            return;
        if (Physics* physics = m_vehicle_entity->GetComponent<Physics>())
        {
            physics->SetVehicleHandbrake(value);
        }
    }

    void Car::ResetToSpawn()
    {
        if (!m_vehicle_entity)
            return;
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
        m_current_view = static_cast<CarView>((static_cast<int>(m_current_view) + 1) % 2);

        Entity* camera = m_body_entity ? m_body_entity->GetChildByName("component_camera") : nullptr;
        if (!camera && default_camera)
        {
            camera = default_camera->GetChildByName("component_camera");
        }

        if (camera)
        {
            if (m_current_view == CarView::Chase)
            {
                camera->SetParent(default_camera);
                m_chase_camera.initialized = false;
            }
            else
            {
                camera->SetParent(m_body_entity);
                // hood position
                math::Quaternion car_local_rot = m_body_entity->GetRotationLocal();
                math::Quaternion camera_correction = car_local_rot.Inverse();
                camera->SetPositionLocal(math::Vector3(0.0f, 0.8f, -1.0f));
                camera->SetRotationLocal(camera_correction);
            }
        }
    }

    void Car::AddCameraOrbitYaw(float delta)
    {
        m_chase_camera.yaw_bias += delta;
        m_chase_camera.yaw_bias = std::clamp(m_chase_camera.yaw_bias, -yaw_bias_max, yaw_bias_max);
    }

    void Car::AddCameraOrbitPitch(float delta)
    {
        m_chase_camera.pitch_bias += delta;
        m_chase_camera.pitch_bias = std::clamp(m_chase_camera.pitch_bias, -pitch_bias_max, pitch_bias_max);
    }

    void Car::DecayCameraOrbit(float dt)
    {
        m_chase_camera.yaw_bias   *= expf(-orbit_bias_decay * dt);
        m_chase_camera.pitch_bias *= expf(-orbit_bias_decay * dt);
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
            return math::BoundingBox::Unit;

        math::BoundingBox combined(math::Vector3::Infinity, math::Vector3::InfinityNeg);
        std::vector<Entity*> descendants;
        m_body_entity->GetDescendants(&descendants);
        descendants.push_back(m_body_entity);

        for (Entity* entity : descendants)
        {
            if (Renderable* renderable = entity->GetComponent<Renderable>())
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
            return nullptr;

        Entity* car_entity = mesh_car->GetRootEntity();
        car_entity->SetObjectName("ferrari_laferrari");
        car_entity->SetScale(2.0f);

        if (remove_wheels)
        {
            auto to_lower = [](std::string s)
            {
                std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
                return s;
            };

            std::vector<Entity*> descendants;
            car_entity->GetDescendants(&descendants);

            for (Entity* descendant : descendants)
            {
                std::string entity_name = to_lower(descendant->GetObjectName());

                if (entity_name.find("tire 1")    != std::string::npos ||
                    entity_name.find("tire 2")    != std::string::npos ||
                    entity_name.find("tire 3")    != std::string::npos ||
                    entity_name.find("tire 4")    != std::string::npos ||
                    entity_name.find("brakerear") != std::string::npos)
                {
                    descendant->SetActive(false);

                    if (out_excluded_entities)
                    {
                        out_excluded_entities->push_back(descendant);
                    }
                }
            }
        }

        // material tweaks
        {
            // body main - red clearcoat paint
            if (Material* material = car_entity->GetDescendantByName("Object_12")->GetComponent<Renderable>()->GetMaterial())
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
            if (Material* material = car_entity->GetDescendantByName("Object_10")->GetComponent<Renderable>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.4f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // tires - rubber
            {
                const char* tire_parts[] = {"Object_127", "Object_142", "Object_157", "Object_172"};
                for (const char* part : tire_parts)
                {
                    if (Material* material = car_entity->GetDescendantByName(part)->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Roughness, 0.7f);
                    }
                }
            }

            // rims - polished metal
            if (Material* material = car_entity->GetDescendantByName("Object_180")->GetComponent<Renderable>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
                material->SetProperty(MaterialProperty::Roughness, 0.3f);
            }
            if (Material* material = car_entity->GetDescendantByName("Object_150")->GetComponent<Renderable>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
                material->SetProperty(MaterialProperty::Roughness, 0.3f);
            }

            // headlight and taillight glass
            if (Material* material = car_entity->GetDescendantByName("Object_38")->GetComponent<Renderable>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.5f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // windshield and engine glass
            if (Material* material = car_entity->GetDescendantByName("Object_58")->GetComponent<Renderable>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.0f);
                material->SetProperty(MaterialProperty::Metalness, 0.0f);
            }

            // side mirror glass
            if (Material* material = car_entity->GetDescendantByName("Object_98")->GetComponent<Renderable>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.0f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // engine block
            if (Material* material = car_entity->GetDescendantByName("Object_14")->GetComponent<Renderable>()->GetMaterial())
            {
                material->SetProperty(MaterialProperty::Roughness, 0.4f);
                material->SetProperty(MaterialProperty::Metalness, 1.0f);
            }

            // brake discs - anisotropic metal
            {
                const char* brake_parts[] = {"Object_129", "Object_144", "Object_174", "Object_159"};
                for (const char* part : brake_parts)
                {
                    if (Material* material = car_entity->GetDescendantByName(part)->GetComponent<Renderable>()->GetMaterial())
                    {
                        material->SetProperty(MaterialProperty::Metalness, 1.0f);
                        material->SetProperty(MaterialProperty::Anisotropic, 1.0f);
                        material->SetProperty(MaterialProperty::AnisotropicRotation, 0.2f);
                    }
                }
            }

            // interior leather
            if (Material* material = car_entity->GetDescendantByName("Object_90")->GetComponent<Renderable>()->GetMaterial())
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
            return;

        Entity* wheel_root = mesh->GetRootEntity();
        Entity* wheel_base = wheel_root->GetChildByIndex(0);
        if (!wheel_base)
            return;

        wheel_base->SetParent(nullptr);
        World::RemoveEntity(wheel_root);
        wheel_base->SetScale(0.2f);

        if (Renderable* renderable = wheel_base->GetComponent<Renderable>())
        {
            Material* material = renderable->GetMaterial();
            material->SetTexture(MaterialTextureType::Color,     "project\\models\\wheel\\albedo.jpeg");
            material->SetTexture(MaterialTextureType::Metalness, "project\\models\\wheel\\metalness.png");
            material->SetTexture(MaterialTextureType::Normal,    "project\\models\\wheel\\normal.png");
            material->SetTexture(MaterialTextureType::Roughness, "project\\models\\wheel\\roughness.png");
        }

        physics->ComputeWheelRadiusFromEntity(wheel_base);
        const float suspension_height = physics->GetSuspensionHeight();
        const float wheel_x           = 0.95f;
        const float wheel_y           = -suspension_height;
        const float front_z           = 1.45f;
        const float rear_z            = -1.35f;

        // front left
        Entity* wheel_fl = wheel_base;
        wheel_fl->SetObjectName("wheel_front_left");
        wheel_fl->SetParent(vehicle_ent);
        wheel_fl->SetPositionLocal(math::Vector3(-wheel_x, wheel_y, front_z));

        // front right
        Entity* wheel_fr = wheel_base->Clone();
        wheel_fr->SetObjectName("wheel_front_right");
        wheel_fr->SetParent(vehicle_ent);
        wheel_fr->SetPositionLocal(math::Vector3(wheel_x, wheel_y, front_z));
        wheel_fr->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));

        // rear left
        Entity* wheel_rl = wheel_base->Clone();
        wheel_rl->SetObjectName("wheel_rear_left");
        wheel_rl->SetParent(vehicle_ent);
        wheel_rl->SetPositionLocal(math::Vector3(-wheel_x, wheel_y, rear_z));

        // rear right
        Entity* wheel_rr = wheel_base->Clone();
        wheel_rr->SetObjectName("wheel_rear_right");
        wheel_rr->SetParent(vehicle_ent);
        wheel_rr->SetPositionLocal(math::Vector3(wheel_x, wheel_y, rear_z));
        wheel_rr->SetRotationLocal(math::Quaternion::FromAxisAngle(math::Vector3::Up, math::pi));

        physics->SetWheelEntity(WheelIndex::FrontLeft,  wheel_fl);
        physics->SetWheelEntity(WheelIndex::FrontRight, wheel_fr);
        physics->SetWheelEntity(WheelIndex::RearLeft,   wheel_rl);
        physics->SetWheelEntity(WheelIndex::RearRight,  wheel_rr);
    }

    void Car::CreateAudioSources(Entity* parent_entity)
    {
        // initialize the engine sound synthesizer
        engine_sound::initialize(48000);

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

        // engine sound (either synthesized or from audio clip)
        {
            Entity* sound = World::CreateEntity();
            sound->SetObjectName("sound_engine");
            sound->SetParent(parent_entity);

            AudioSource* audio_source = sound->AddComponent<AudioSource>();
            audio_source->SetLoop(true);
            audio_source->SetPlayOnStart(false);
            audio_source->SetVolume(0.8f);

            // set up audio clip for recording mode (default)
            audio_source->SetAudioClip("project\\music\\car_idle.wav");
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

        // tire squeal
        {
            Entity* sound = World::CreateEntity();
            sound->SetObjectName("sound_tire_squeal");
            sound->SetParent(parent_entity);

            AudioSource* audio_source = sound->AddComponent<AudioSource>();
            audio_source->SetAudioClip("project\\music\\tire_squeal.wav");
            audio_source->SetLoop(true);
            audio_source->SetPlayOnStart(false);
            audio_source->SetVolume(0.0f);
        }
    }

    void Car::Tick()
    {
        if (!m_body_entity)
            return;

        TickInput();
        TickSounds();
        TickChaseCamera();
        TickEnterExit();
        TickViewSwitch();

        if (m_show_telemetry)
        {
            DrawTelemetry();
            if (use_synthesized_engine_sound)
                engine_sound::debug_window();
        }

        // osd hint
        if (m_is_occupied)
        {
            Renderer::DrawString("R2: Gas | L2: Brake | O: Handbrake | Triangle: View | L1/R1: Shift | X: Reset", 
                               math::Vector2(0.005f, 0.98f));
        }
    }

    void Car::TickInput()
    {
        if (!m_vehicle_entity || !m_is_occupied)
            return;

        Physics* physics = m_vehicle_entity->GetComponent<Physics>();
        if (!physics || !Engine::IsFlagSet(EngineMode::Playing))
            return;

        bool is_gamepad_connected = Input::IsGamepadConnected();
        float dt = static_cast<float>(Timer::GetDeltaTimeSec());

        // throttle
        float throttle = 0.0f;
        if (is_gamepad_connected)
            throttle = Input::GetGamepadTriggerRight();
        if (Input::GetKey(KeyCode::Arrow_Up))
            throttle = 1.0f;

        // brake
        float brake = 0.0f;
        if (is_gamepad_connected)
            brake = Input::GetGamepadTriggerLeft();
        if (Input::GetKey(KeyCode::Arrow_Down))
            brake = 1.0f;

        // steering
        float steering = 0.0f;
        if (is_gamepad_connected)
            steering = Input::GetGamepadThumbStickLeft().x;
        if (Input::GetKey(KeyCode::Arrow_Left))
            steering = -1.0f;
        if (Input::GetKey(KeyCode::Arrow_Right))
            steering = 1.0f;

        // handbrake
        float handbrake = (Input::GetKey(KeyCode::Space) || Input::GetKey(KeyCode::Button_East)) ? 1.0f : 0.0f;

        physics->SetVehicleThrottle(throttle);
        physics->SetVehicleBrake(brake);
        physics->SetVehicleSteering(steering);
        physics->SetVehicleHandbrake(handbrake);

        // camera orbit
        if (is_gamepad_connected)
        {
            math::Vector2 right_stick = Input::GetGamepadThumbStickRight();

            float stick_x = fabsf(right_stick.x);
            if (stick_x > 0.3f)
            {
                AddCameraOrbitYaw(right_stick.x * orbit_bias_speed * dt);
            }
            else if (stick_x < 0.1f && fabsf(m_chase_camera.yaw_bias) > 0.01f)
            {
                m_chase_camera.yaw_bias *= expf(-orbit_bias_decay * dt);
            }

            float stick_y = fabsf(right_stick.y);
            if (stick_y > 0.3f)
            {
                AddCameraOrbitPitch(right_stick.y * orbit_bias_speed * dt);
            }
            else if (stick_y < 0.1f && fabsf(m_chase_camera.pitch_bias) > 0.01f)
            {
                m_chase_camera.pitch_bias *= expf(-orbit_bias_decay * dt);
            }
        }

        // reset to spawn
        if (Input::GetKeyDown(KeyCode::R) || Input::GetKeyDown(KeyCode::Button_South))
        {
            ResetToSpawn();
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
            return;

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

            if (use_synthesized_engine_sound)
            {
                // enable synthesis mode (this stops clip if playing and switches mode)
                audio_engine->SetSynthesisMode(true, [](float* buffer, int num_samples)
                {
                    engine_sound::generate(buffer, num_samples, true);
                });

                if (!audio_engine->IsPlaying())
                    audio_engine->StartSynthesis();

                // update synthesizer parameters
                float load = throttle * (0.5f + rpm_normalized * 0.5f);
                engine_sound::set_parameters(engine_rpm, throttle, load, boost);

                float volume = 0.6f + rpm_normalized * 0.3f + throttle * 0.1f;
                audio_engine->SetVolume(volume);
            }
            else
            {
                // disable synthesis mode (this stops synthesis if playing and switches mode)
                audio_engine->SetSynthesisMode(false, nullptr);

                if (!audio_engine->IsPlaying())
                    audio_engine->PlayClip();

                // adjust pitch and volume based on rpm
                float pitch = 0.5f + rpm_normalized * 1.5f;  // 0.5x at idle, 2.0x at redline
                float volume = 0.4f + rpm_normalized * 0.4f + throttle * 0.2f;
                audio_engine->SetPitch(pitch);
                audio_engine->SetVolume(volume);
            }
        }
        else if (!m_is_occupied && audio_engine && audio_engine->IsPlaying())
        {
            audio_engine->StopClip();
            audio_engine->StopSynthesis();
        }

        // tire squeal
        if (audio_tire && physics)
        {
            float speed_kmh = physics->GetLinearVelocity().Length() * 3.6f;

            float max_slip_angle = 0.0f;
            float max_slip_ratio = 0.0f;
            int grounded_count = 0;

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

            float fade_rate = (target_intensity > m_tire_squeal_volume) ? 0.04f : 0.025f;
            m_tire_squeal_volume += (target_intensity - m_tire_squeal_volume) * fade_rate;

            const float max_volume = 0.25f;
            float volume = m_tire_squeal_volume * max_volume;

            if (m_tire_squeal_volume > 0.02f)
            {
                if (!audio_tire->IsPlaying())
                    audio_tire->PlayClip();

                audio_tire->SetVolume(volume);
                audio_tire->SetPitch(0.95f + m_tire_squeal_volume * 0.15f);
            }
            else
            {
                m_tire_squeal_volume = 0.0f;
                if (audio_tire->IsPlaying())
                    audio_tire->StopClip();
            }
        }
    }

    void Car::TickChaseCamera()
    {
        if (!m_is_occupied || m_current_view != CarView::Chase || !m_vehicle_entity || !default_camera)
            return;

        Entity* camera = default_camera->GetChildByName("component_camera");
        if (!camera)
        {
            camera = m_vehicle_entity->GetChildByName("component_camera");
            if (!camera)
                camera = m_body_entity->GetChildByName("component_camera");
            if (camera)
            {
                camera->SetParent(default_camera);
                m_chase_camera.initialized = false;
            }
        }

        if (!camera)
            return;

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
            return;

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
                            return;
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
            return;

        // triangle for view change (gran turismo style)
        if (Input::GetKeyDown(KeyCode::V) || Input::GetKeyDown(KeyCode::Button_North))
        {
            CycleView();
        }
    }

    void Car::DrawTelemetry()
    {
        if (!Engine::IsFlagSet(EngineMode::EditorVisible))
            return;

        if (!m_vehicle_entity)
            return;

        Physics* physics = m_vehicle_entity->GetComponent<Physics>();
        if (!physics)
            return;

        const char* wheel_names[] = { "FL", "FR", "RL", "RR" };
        math::Vector3 velocity = physics->GetLinearVelocity();
        float speed_kmh  = velocity.Length() * 3.6f;
        float engine_rpm = physics->GetEngineRPM();
        float redline    = physics->GetRedlineRPM();

        physics->DrawDebugVisualization();

        ImVec2 display_size = ImGui::GetIO().DisplaySize;
        if (display_size.x < 100.0f || display_size.y < 100.0f)
            return;

        // window layout constants
        const float margin = 10.0f;
        const float group_spacing = 8.0f;
        
        // approximate window sizes
        const float dashboard_width = 480.0f;
        const float dashboard_height = 360.0f;
        const float aero_width = 420.0f;
        const float aero_height = 220.0f;
        const float wheels_width = 560.0f;
        const float wheels_height = 620.0f;
        
        // right group: dashboard + aerodynamics stacked vertically
        float right_group_height = dashboard_height + group_spacing + aero_height;
        float right_group_y = (display_size.y - right_group_height) * 0.5f;
        right_group_y = std::clamp(right_group_y, margin, display_size.y - right_group_height - margin);
        float right_group_x = display_size.x - dashboard_width - margin;
        right_group_x = std::max(right_group_x, margin);
        
        // left group: wheels centered vertically
        float left_group_y = (display_size.y - wheels_height) * 0.5f;
        left_group_y = std::clamp(left_group_y, margin, display_size.y - wheels_height - margin);
        float left_group_x = margin;
        
        // dashboard window (top of right group)
        ImGui::SetNextWindowPos(ImVec2(right_group_x, right_group_y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(dashboard_width, dashboard_height), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Dashboard", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 base_pos = ImGui::GetCursorScreenPos();

            const float pi = 3.14159f;
            const float start_angle = pi * 0.75f;
            const float end_angle   = pi * 2.25f;
            const float angle_range = end_angle - start_angle;

            // speedometer
            {
                const float gauge_radius = 90.0f;
                const float max_speed    = 350.0f;

                ImVec2 gauge_center = ImVec2(base_pos.x + gauge_radius + 20, base_pos.y + gauge_radius + 15);

                draw_list->AddCircle(gauge_center, gauge_radius + 4, IM_COL32(80, 80, 80, 255), 64, 2.5f);
                draw_list->AddCircleFilled(gauge_center, gauge_radius, IM_COL32(25, 25, 30, 255), 64);

                const int arc_segments = 64;
                for (int i = 0; i < arc_segments; i++)
                {
                    float a1 = start_angle + (angle_range * i / arc_segments);
                    float a2 = start_angle + (angle_range * (i + 1) / arc_segments);
                    float speed_at_segment = (float)i / arc_segments * max_speed;

                    ImU32 arc_color;
                    if (speed_at_segment < 150.0f)
                        arc_color = IM_COL32(50, 100, 50, 255);
                    else if (speed_at_segment < 250.0f)
                        arc_color = IM_COL32(100, 100, 40, 255);
                    else
                        arc_color = IM_COL32(120, 40, 40, 255);

                    ImVec2 p1(gauge_center.x + cosf(a1) * (gauge_radius - 12), gauge_center.y + sinf(a1) * (gauge_radius - 12));
                    ImVec2 p2(gauge_center.x + cosf(a1) * (gauge_radius - 4),  gauge_center.y + sinf(a1) * (gauge_radius - 4));
                    ImVec2 p3(gauge_center.x + cosf(a2) * (gauge_radius - 4),  gauge_center.y + sinf(a2) * (gauge_radius - 4));
                    ImVec2 p4(gauge_center.x + cosf(a2) * (gauge_radius - 12), gauge_center.y + sinf(a2) * (gauge_radius - 12));
                    draw_list->AddQuadFilled(p1, p2, p3, p4, arc_color);
                }

                for (int speed = 0; speed <= (int)max_speed; speed += 10)
                {
                    float fraction = (float)speed / max_speed;
                    float angle = start_angle + fraction * angle_range;
                    bool is_major = (speed % 50 == 0);
                    float inner_r = is_major ? gauge_radius - 22 : gauge_radius - 17;
                    float outer_r = gauge_radius - 4;

                    ImVec2 inner_pt(gauge_center.x + cosf(angle) * inner_r, gauge_center.y + sinf(angle) * inner_r);
                    ImVec2 outer_pt(gauge_center.x + cosf(angle) * outer_r, gauge_center.y + sinf(angle) * outer_r);
                    draw_list->AddLine(inner_pt, outer_pt, is_major ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255), is_major ? 2.0f : 1.0f);

                    if (is_major)
                    {
                        char num_str[8];
                        snprintf(num_str, sizeof(num_str), "%d", speed);
                        float text_r = gauge_radius - 34;
                        ImVec2 text_pos(gauge_center.x + cosf(angle) * text_r - 8, gauge_center.y + sinf(angle) * text_r - 6);
                        draw_list->AddText(text_pos, IM_COL32(200, 200, 200, 255), num_str);
                    }
                }

                float clamped_speed = std::min(speed_kmh, max_speed);
                float needle_angle = start_angle + (clamped_speed / max_speed) * angle_range;
                float needle_length = gauge_radius - 22;

                ImVec2 needle_tip(gauge_center.x + cosf(needle_angle) * needle_length, gauge_center.y + sinf(needle_angle) * needle_length);
                ImVec2 needle_base_l(gauge_center.x + cosf(needle_angle + 1.57f) * 3, gauge_center.y + sinf(needle_angle + 1.57f) * 3);
                ImVec2 needle_base_r(gauge_center.x + cosf(needle_angle - 1.57f) * 3, gauge_center.y + sinf(needle_angle - 1.57f) * 3);
                ImVec2 needle_back(gauge_center.x + cosf(needle_angle + pi) * 12, gauge_center.y + sinf(needle_angle + pi) * 12);

                draw_list->AddTriangleFilled(needle_tip, needle_base_l, needle_base_r, IM_COL32(220, 60, 60, 255));
                draw_list->AddTriangleFilled(needle_base_l, needle_base_r, needle_back, IM_COL32(180, 40, 40, 255));

                draw_list->AddCircleFilled(gauge_center, 10, IM_COL32(60, 60, 65, 255), 24);
                draw_list->AddCircle(gauge_center, 10, IM_COL32(100, 100, 100, 255), 24, 2.0f);

                char speed_str[16];
                snprintf(speed_str, sizeof(speed_str), "%.0f", speed_kmh);
                ImVec2 speed_text_size = ImGui::CalcTextSize(speed_str);
                draw_list->AddText(ImVec2(gauge_center.x - speed_text_size.x * 0.5f, gauge_center.y + 20), IM_COL32(255, 255, 255, 255), speed_str);
                draw_list->AddText(ImVec2(gauge_center.x - 15, gauge_center.y + 34), IM_COL32(150, 150, 150, 255), "km/h");
            }

            // tachometer
            {
                const float gauge_radius = 90.0f;
                const float max_rpm_display = 10000.0f;

                ImVec2 gauge_center = ImVec2(base_pos.x + gauge_radius * 2 + 60 + gauge_radius + 20, base_pos.y + gauge_radius + 15);

                draw_list->AddCircle(gauge_center, gauge_radius + 4, IM_COL32(80, 80, 80, 255), 64, 2.5f);
                draw_list->AddCircleFilled(gauge_center, gauge_radius, IM_COL32(25, 25, 30, 255), 64);

                const int arc_segments = 64;
                for (int i = 0; i < arc_segments; i++)
                {
                    float a1 = start_angle + (angle_range * i / arc_segments);
                    float a2 = start_angle + (angle_range * (i + 1) / arc_segments);
                    float rpm_at_segment = (float)i / arc_segments * max_rpm_display;

                    ImU32 arc_color;
                    if (rpm_at_segment < 6000.0f)
                        arc_color = IM_COL32(50, 80, 50, 255);
                    else if (rpm_at_segment < redline)
                        arc_color = IM_COL32(100, 100, 40, 255);
                    else
                        arc_color = IM_COL32(180, 40, 40, 255);

                    ImVec2 p1(gauge_center.x + cosf(a1) * (gauge_radius - 12), gauge_center.y + sinf(a1) * (gauge_radius - 12));
                    ImVec2 p2(gauge_center.x + cosf(a1) * (gauge_radius - 4),  gauge_center.y + sinf(a1) * (gauge_radius - 4));
                    ImVec2 p3(gauge_center.x + cosf(a2) * (gauge_radius - 4),  gauge_center.y + sinf(a2) * (gauge_radius - 4));
                    ImVec2 p4(gauge_center.x + cosf(a2) * (gauge_radius - 12), gauge_center.y + sinf(a2) * (gauge_radius - 12));
                    draw_list->AddQuadFilled(p1, p2, p3, p4, arc_color);
                }

                for (int rpm = 0; rpm <= (int)max_rpm_display; rpm += 500)
                {
                    float fraction = (float)rpm / max_rpm_display;
                    float angle = start_angle + fraction * angle_range;
                    bool is_major = (rpm % 1000 == 0);
                    float inner_r = is_major ? gauge_radius - 22 : gauge_radius - 17;
                    float outer_r = gauge_radius - 4;

                    ImU32 tick_color;
                    if (rpm >= (int)redline)
                        tick_color = IM_COL32(255, 80, 80, 255);
                    else
                        tick_color = is_major ? IM_COL32(255, 255, 255, 255) : IM_COL32(150, 150, 150, 255);

                    ImVec2 inner_pt(gauge_center.x + cosf(angle) * inner_r, gauge_center.y + sinf(angle) * inner_r);
                    ImVec2 outer_pt(gauge_center.x + cosf(angle) * outer_r, gauge_center.y + sinf(angle) * outer_r);
                    draw_list->AddLine(inner_pt, outer_pt, tick_color, is_major ? 2.0f : 1.0f);

                    if (is_major)
                    {
                        char num_str[8];
                        snprintf(num_str, sizeof(num_str), "%d", rpm / 1000);
                        float text_r = gauge_radius - 34;
                        ImVec2 text_pos(gauge_center.x + cosf(angle) * text_r - 4, gauge_center.y + sinf(angle) * text_r - 6);
                        ImU32 text_color = (rpm >= (int)redline) ? IM_COL32(255, 100, 100, 255) : IM_COL32(200, 200, 200, 255);
                        draw_list->AddText(text_pos, text_color, num_str);
                    }
                }

                float clamped_rpm = std::min(engine_rpm, max_rpm_display);
                float needle_angle = start_angle + (clamped_rpm / max_rpm_display) * angle_range;
                float needle_length = gauge_radius - 22;

                ImU32 needle_color = (engine_rpm > redline) ? IM_COL32(255, 100, 100, 255) : IM_COL32(220, 60, 60, 255);
                ImU32 needle_back_color = (engine_rpm > redline) ? IM_COL32(200, 60, 60, 255) : IM_COL32(180, 40, 40, 255);

                ImVec2 needle_tip(gauge_center.x + cosf(needle_angle) * needle_length, gauge_center.y + sinf(needle_angle) * needle_length);
                ImVec2 needle_base_l(gauge_center.x + cosf(needle_angle + 1.57f) * 3, gauge_center.y + sinf(needle_angle + 1.57f) * 3);
                ImVec2 needle_base_r(gauge_center.x + cosf(needle_angle - 1.57f) * 3, gauge_center.y + sinf(needle_angle - 1.57f) * 3);
                ImVec2 needle_back(gauge_center.x + cosf(needle_angle + pi) * 12, gauge_center.y + sinf(needle_angle + pi) * 12);

                draw_list->AddTriangleFilled(needle_tip, needle_base_l, needle_base_r, needle_color);
                draw_list->AddTriangleFilled(needle_base_l, needle_base_r, needle_back, needle_back_color);

                draw_list->AddCircleFilled(gauge_center, 10, IM_COL32(60, 60, 65, 255), 24);
                draw_list->AddCircle(gauge_center, 10, IM_COL32(100, 100, 100, 255), 24, 2.0f);

                char rpm_str[16];
                snprintf(rpm_str, sizeof(rpm_str), "%.0f", engine_rpm);
                ImVec2 rpm_text_size = ImGui::CalcTextSize(rpm_str);
                ImU32 rpm_text_color = (engine_rpm > redline) ? IM_COL32(255, 100, 100, 255) : IM_COL32(255, 255, 255, 255);
                draw_list->AddText(ImVec2(gauge_center.x - rpm_text_size.x * 0.5f, gauge_center.y + 20), rpm_text_color, rpm_str);
                draw_list->AddText(ImVec2(gauge_center.x - 10, gauge_center.y + 34), IM_COL32(150, 150, 150, 255), "RPM");

                // gear indicator
                const char* gear_str = physics->GetCurrentGearString();
                bool is_shifting = physics->IsShifting();
                ImU32 gear_color = is_shifting ? IM_COL32(255, 255, 0, 255) : IM_COL32(255, 255, 255, 255);
                ImVec2 gear_pos = ImVec2(base_pos.x + gauge_radius * 2 + 45, base_pos.y + gauge_radius - 10);
                draw_list->AddText(nullptr, 24.0f, gear_pos, gear_color, gear_str);
            }

            ImGui::Dummy(ImVec2(90 * 4 + 80, 90 * 2 + 35));
            ImGui::Separator();

            // pedal bars
            {
                float throttle_val = physics->GetVehicleThrottle();
                float brake_val    = physics->GetVehicleBrake();
                float steer_val    = physics->GetVehicleSteering();

                const float bar_width  = 30.0f;
                const float bar_height = 80.0f;

                ImGui::BeginGroup();
                ImGui::Text("THR");
                ImVec2 throttle_pos = ImGui::GetCursorScreenPos();
                draw_list->AddRectFilled(throttle_pos, ImVec2(throttle_pos.x + bar_width, throttle_pos.y + bar_height), IM_COL32(40, 40, 40, 255));
                float throttle_fill = bar_height * throttle_val;
                draw_list->AddRectFilled(
                    ImVec2(throttle_pos.x, throttle_pos.y + bar_height - throttle_fill),
                    ImVec2(throttle_pos.x + bar_width, throttle_pos.y + bar_height),
                    IM_COL32(50, 200, 50, 255));
                draw_list->AddRect(throttle_pos, ImVec2(throttle_pos.x + bar_width, throttle_pos.y + bar_height), IM_COL32(100, 100, 100, 255));
                ImGui::Dummy(ImVec2(bar_width, bar_height));
                ImGui::Text("%.0f%%", throttle_val * 100.0f);
                ImGui::EndGroup();

                ImGui::SameLine(60);

                ImGui::BeginGroup();
                ImGui::Text("BRK");
                ImVec2 brake_pos = ImGui::GetCursorScreenPos();
                draw_list->AddRectFilled(brake_pos, ImVec2(brake_pos.x + bar_width, brake_pos.y + bar_height), IM_COL32(40, 40, 40, 255));
                float brake_fill = bar_height * brake_val;
                draw_list->AddRectFilled(
                    ImVec2(brake_pos.x, brake_pos.y + bar_height - brake_fill),
                    ImVec2(brake_pos.x + bar_width, brake_pos.y + bar_height),
                    IM_COL32(220, 50, 50, 255));
                draw_list->AddRect(brake_pos, ImVec2(brake_pos.x + bar_width, brake_pos.y + bar_height), IM_COL32(100, 100, 100, 255));
                ImGui::Dummy(ImVec2(bar_width, bar_height));
                ImGui::Text("%.0f%%", brake_val * 100.0f);
                ImGui::EndGroup();

                ImGui::SameLine(140);

                ImGui::BeginGroup();
                ImGui::Text("STEER");
                ImVec2 steer_pos = ImGui::GetCursorScreenPos();
                const float steer_width = 120.0f;
                const float steer_height = 20.0f;
                draw_list->AddRectFilled(steer_pos, ImVec2(steer_pos.x + steer_width, steer_pos.y + steer_height), IM_COL32(40, 40, 40, 255));
                float center_x = steer_pos.x + steer_width * 0.5f;
                float indicator_x = center_x + (steer_val * steer_width * 0.5f);
                draw_list->AddLine(ImVec2(center_x, steer_pos.y), ImVec2(center_x, steer_pos.y + steer_height), IM_COL32(100, 100, 100, 255));
                draw_list->AddRectFilled(
                    ImVec2(indicator_x - 4, steer_pos.y + 2),
                    ImVec2(indicator_x + 4, steer_pos.y + steer_height - 2),
                    IM_COL32(255, 200, 50, 255));
                draw_list->AddRect(steer_pos, ImVec2(steer_pos.x + steer_width, steer_pos.y + steer_height), IM_COL32(100, 100, 100, 255));
                ImGui::Dummy(ImVec2(steer_width, steer_height));
                ImGui::Text("%.0f%%", steer_val * 100.0f);
                ImGui::EndGroup();
            }

            ImGui::Separator();

            // driver assists
            bool abs_enabled = physics->GetAbsEnabled();
            bool tc_enabled  = physics->GetTcEnabled();
            bool manual_trans = physics->GetManualTransmission();
            bool abs_active  = physics->IsAbsActiveAny();
            bool tc_active   = physics->IsTcActive();

            if (ImGui::Checkbox("ABS", &abs_enabled))
                physics->SetAbsEnabled(abs_enabled);
            if (abs_enabled && abs_active)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "(active)");
            }

            ImGui::SameLine(140);
            if (ImGui::Checkbox("TCS", &tc_enabled))
                physics->SetTcEnabled(tc_enabled);
            if (tc_enabled && tc_active)
            {
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1, 1, 0, 1), "(active)");
            }

            if (ImGui::Checkbox("Manual", &manual_trans))
                physics->SetManualTransmission(manual_trans);

            bool turbo_enabled = physics->GetTurboEnabled();
            ImGui::SameLine(140);
            if (ImGui::Checkbox("Turbo", &turbo_enabled))
                physics->SetTurboEnabled(turbo_enabled);
            if (turbo_enabled)
            {
                float boost = physics->GetBoostPressure();
                ImGui::SameLine();
                ImGui::TextColored(boost > 0.5f ? ImVec4(0.3f, 1, 0.3f, 1) : ImVec4(0.7f, 0.7f, 0.7f, 1), "%.2f bar", boost);
            }

            ImGui::Checkbox("Synth Audio", &use_synthesized_engine_sound);

            if (physics->GetVehicleHandbrake() > 0.1f)
            {
                ImGui::TextColored(ImVec4(1, 0.3f, 0.3f, 1), "HANDBRAKE");
            }
        }
        ImGui::End();

        // aerodynamics window (below dashboard in right group)
        float aero_window_y = right_group_y + dashboard_height + group_spacing;
        ImGui::SetNextWindowPos(ImVec2(right_group_x, aero_window_y), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(aero_width, 0.0f), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Aerodynamics", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 section_start = ImGui::GetCursorScreenPos();
            
            // get aerodynamics data
            const car::aero_debug_data& aero = car::get_aero_debug();
            float frontal_area = car::get_frontal_area();
            float side_area = car::get_side_area();
            float drag_coeff = car::get_drag_coeff();
            
            // car dimensions for visualization
            float car_length = car::cfg.length;
            float car_width = car::cfg.width;
            float car_height = car::cfg.height + car::cfg.wheel_radius * 2.0f;
            
            // get shape data for drawing (convex hull from actual mesh)
            const car::shape_2d& shape = car::get_shape_data();
            
            // fixed width for this window
            const float side_view_width = 220.0f;
            const float front_view_width = 160.0f;
            const float view_height = 100.0f;
            const float view_spacing = 15.0f;
            const float margin = 5.0f;
            
            // side view (left)
            ImVec2 side_view_pos = ImVec2(section_start.x + margin, section_start.y + 20.0f);
            
            // calculate a common pixels-per-meter scale based on the largest dimension (car length)
            // this ensures both views use the same scale for proper proportions
            float shape_length = shape.max_z - shape.min_z;  // side view range
            float shape_width = shape.max_x - shape.min_x;   // front view range
            float max_horizontal = std::max(shape_length, shape_width);
            float pixels_per_meter = (side_view_width * 0.90f) / max_horizontal;
            
            // helper: draw convex hull profile (single closed loop of points)
            auto draw_convex_profile = [&](const std::vector<std::pair<float, float>>& profile,
                                           float min_axis, float max_axis, float min_y, float max_y,
                                           float draw_x, float draw_y, float draw_w, float draw_h,
                                           ImU32 fill_color, ImU32 outline_color)
            {
                if (profile.size() < 3)
                    return;
                
                float axis_range = max_axis - min_axis;
                float y_range = max_y - min_y;
                
                if (axis_range < 0.01f || y_range < 0.01f)
                    return;
                
                // use common scale for proper proportions between views
                float scale_x = axis_range * pixels_per_meter;
                float scale_y = y_range * pixels_per_meter;
                
                // center horizontally in draw area
                float offset_x = draw_x + (draw_w - scale_x) * 0.5f;
                float offset_y = draw_y + draw_h * 0.80f;  // position near bottom for wheels
                
                // convert hull points to screen coordinates
                std::vector<ImVec2> screen_pts;
                screen_pts.reserve(profile.size());
                
                for (const auto& pt : profile)
                {
                    float norm_axis = (pt.first - min_axis) / axis_range;
                    float norm_y = (pt.second - min_y) / y_range;
                    float screen_x = offset_x + norm_axis * scale_x;
                    float screen_y = offset_y - norm_y * scale_y;
                    screen_pts.push_back(ImVec2(screen_x, screen_y));
                }
                
                // draw filled convex polygon and outline
                if (screen_pts.size() >= 3)
                {
                    draw_list->AddConvexPolyFilled(screen_pts.data(), (int)screen_pts.size(), fill_color);
                    draw_list->AddPolyline(screen_pts.data(), (int)screen_pts.size(), outline_color, ImDrawFlags_Closed, 2.0f);
                }
            };
            
            // draw car side profile
            {
                float x = side_view_pos.x;
                float y = side_view_pos.y;
                float w = side_view_width;
                float h = view_height;
                
                if (shape.valid && shape.side_profile.size() >= 3)
                {
                    draw_convex_profile(shape.side_profile, shape.min_z, shape.max_z, shape.min_y, shape.max_y,
                                        x, y, w, h, IM_COL32(45, 50, 60, 255), IM_COL32(80, 130, 180, 255));
                }
                else
                {
                    // fallback: car-like silhouette
                    ImVec2 fallback_pts[] = {
                        ImVec2(x + w * 0.02f, y + h * 0.75f),
                        ImVec2(x + w * 0.08f, y + h * 0.45f),
                        ImVec2(x + w * 0.25f, y + h * 0.40f),
                        ImVec2(x + w * 0.30f, y + h * 0.15f),
                        ImVec2(x + w * 0.70f, y + h * 0.15f),
                        ImVec2(x + w * 0.85f, y + h * 0.35f),
                        ImVec2(x + w * 0.98f, y + h * 0.75f),
                    };
                    draw_list->AddConvexPolyFilled(fallback_pts, 7, IM_COL32(45, 50, 60, 255));
                    draw_list->AddPolyline(fallback_pts, 7, IM_COL32(80, 130, 180, 255), ImDrawFlags_Closed, 2.0f);
                }
                
                // wheels
                float wheel_r = h * 0.20f;
                draw_list->AddCircleFilled(ImVec2(x + w * 0.18f, y + h * 0.85f), wheel_r, IM_COL32(30, 30, 35, 255), 16);
                draw_list->AddCircleFilled(ImVec2(x + w * 0.82f, y + h * 0.85f), wheel_r, IM_COL32(30, 30, 35, 255), 16);
                
                draw_list->AddText(ImVec2(x, y - 15), IM_COL32(150, 150, 150, 255), "Side");
            }
            
            // front view (right)
            ImVec2 front_view_pos = ImVec2(side_view_pos.x + side_view_width + view_spacing, section_start.y + 20.0f);
            
            {
                float x = front_view_pos.x;
                float y = front_view_pos.y;
                float w = front_view_width;
                float h = view_height;
                
                if (shape.valid && shape.front_profile.size() >= 3)
                {
                    draw_convex_profile(shape.front_profile, shape.min_x, shape.max_x, shape.min_y, shape.max_y,
                                        x, y, w, h, IM_COL32(45, 50, 60, 255), IM_COL32(80, 130, 180, 255));
                }
                else
                {
                    // fallback: car-like front silhouette
                    ImVec2 fallback_pts[] = {
                        ImVec2(x + w * 0.05f, y + h * 0.75f),
                        ImVec2(x + w * 0.05f, y + h * 0.35f),
                        ImVec2(x + w * 0.15f, y + h * 0.15f),
                        ImVec2(x + w * 0.85f, y + h * 0.15f),
                        ImVec2(x + w * 0.95f, y + h * 0.35f),
                        ImVec2(x + w * 0.95f, y + h * 0.75f),
                    };
                    draw_list->AddConvexPolyFilled(fallback_pts, 6, IM_COL32(45, 50, 60, 255));
                    draw_list->AddPolyline(fallback_pts, 6, IM_COL32(80, 130, 180, 255), ImDrawFlags_Closed, 2.0f);
                }
                
                // wheels
                float wheel_w_px = w * 0.12f;
                float wheel_h_px = h * 0.35f;
                draw_list->AddRectFilled(ImVec2(x + w * 0.06f - wheel_w_px * 0.5f, y + h * 0.70f), ImVec2(x + w * 0.06f + wheel_w_px * 0.5f, y + h * 0.70f + wheel_h_px), IM_COL32(30, 30, 35, 255), 3.0f);
                draw_list->AddRectFilled(ImVec2(x + w * 0.94f - wheel_w_px * 0.5f, y + h * 0.70f), ImVec2(x + w * 0.94f + wheel_w_px * 0.5f, y + h * 0.70f + wheel_h_px), IM_COL32(30, 30, 35, 255), 3.0f);
                
                draw_list->AddText(ImVec2(x, y - 15), IM_COL32(150, 150, 150, 255), "Front");
            }
            
            // compute forces
            float aero_speed_ms = speed_kmh / 3.6f;
            float drag_force_n = 0.0f;
            float front_df_n = 0.0f;
            float rear_df_n = 0.0f;
            float side_force_n = 0.0f;
            
            if (aero.valid && aero.drag_force.magnitude() > 0.1f)
            {
                drag_force_n = aero.drag_force.magnitude();
                front_df_n = aero.front_downforce.magnitude();
                rear_df_n = aero.rear_downforce.magnitude();
                side_force_n = aero.side_force.magnitude();
            }
            else if (aero_speed_ms > 0.5f)
            {
                const float air_density = 1.225f;
                float dyn_pressure = 0.5f * air_density * aero_speed_ms * aero_speed_ms;
                drag_force_n = dyn_pressure * drag_coeff * frontal_area;
                front_df_n = fabsf(car::get_lift_coeff_front() * dyn_pressure * frontal_area);
                rear_df_n = fabsf(car::get_lift_coeff_rear() * dyn_pressure * frontal_area);
            }
            
            // draw force arrows on side view
            auto draw_arrow = [&](ImVec2 start, float dx, float dy, ImU32 color, float force_n)
            {
                float len = sqrtf(dx * dx + dy * dy);
                if (len < 5.0f) return;
                
                float nx = dx / len;
                float ny = dy / len;
                ImVec2 end = ImVec2(start.x + dx, start.y + dy);
                
                draw_list->AddLine(start, end, color, 3.0f);
                float hs = std::min(len * 0.35f, 10.0f);
                draw_list->AddTriangleFilled(end,
                    ImVec2(end.x - hs * (nx + ny * 0.5f), end.y - hs * (ny - nx * 0.5f)),
                    ImVec2(end.x - hs * (nx - ny * 0.5f), end.y - hs * (ny + nx * 0.5f)), color);
                
                char val[16];
                if (force_n >= 1000.0f) snprintf(val, 16, "%.1fkN", force_n / 1000.0f);
                else snprintf(val, 16, "%.0fN", force_n);
                draw_list->AddText(ImVec2(end.x + (dy != 0 ? 4 : -18), end.y + (dx != 0 ? -14 : -4)), color, val);
            };
            
            const float fs = 0.035f;
            const float max_len = 50.0f;
            
            // side view arrows
            {
                float x = side_view_pos.x, y = side_view_pos.y, w = side_view_width, h = view_height;
                if (drag_force_n > 10.0f)
                    draw_arrow(ImVec2(x + w * 0.06f, y + h * 0.45f), -std::clamp(drag_force_n * fs, 10.0f, max_len), 0, IM_COL32(255, 140, 50, 255), drag_force_n);
                if (front_df_n > 10.0f)
                    draw_arrow(ImVec2(x + w * 0.20f, y + h * 0.08f), 0, std::clamp(front_df_n * fs, 10.0f, max_len), IM_COL32(80, 160, 255, 255), front_df_n);
                if (rear_df_n > 10.0f)
                    draw_arrow(ImVec2(x + w * 0.80f, y + h * 0.08f), 0, std::clamp(rear_df_n * fs, 10.0f, max_len), IM_COL32(80, 160, 255, 255), rear_df_n);
            }
            
            // front view arrows
            {
                float x = front_view_pos.x, y = front_view_pos.y, w = front_view_width, h = view_height;
                float total_df = front_df_n + rear_df_n;
                if (total_df > 10.0f)
                    draw_arrow(ImVec2(x + w * 0.5f, y + h * 0.02f), 0, std::clamp(total_df * fs * 0.5f, 10.0f, max_len), IM_COL32(80, 160, 255, 255), total_df);
                if (side_force_n > 50.0f)
                {
                    float dir = (aero.valid && aero.side_force.x < 0) ? -1.0f : 1.0f;
                    draw_arrow(ImVec2(x + w * 0.5f, y + h * 0.40f), dir * std::clamp(side_force_n * fs, 10.0f, max_len), 0, IM_COL32(255, 220, 80, 255), side_force_n);
                }
            }
            
            // reserve space
            ImGui::Dummy(ImVec2(side_view_width + view_spacing + front_view_width + margin * 2, view_height + 25.0f));
            
            // compact stats
            ImGui::Separator();
            float total_df = front_df_n + rear_df_n;
            
            ImGui::Text("Frontal: %.2f m\xC2\xB2  Side: %.2f m\xC2\xB2  Cd: %.2f", frontal_area, side_area, drag_coeff);
            
            if (aero_speed_ms > 0.5f && total_df > 1.0f)
            {
                float balance = front_df_n / total_df * 100.0f;
                ImGui::TextColored(ImVec4(0.4f, 0.6f, 1.0f, 1.0f), "Downforce: %.0fN (%.0f%%F/%.0f%%R)", total_df, balance, 100.0f - balance);
                if (aero.valid && aero.ground_effect_factor > 1.01f)
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.6f, 1.0f), "GE:+%.0f%%", (aero.ground_effect_factor - 1.0f) * 100.0f);
            }
            
            // legend
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Drag");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.3f, 0.6f, 1.0f, 1.0f), "Downforce");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Side");
        }
        ImGui::End();

        // wheels window (left group, centered vertically)
        ImGui::SetNextWindowPos(ImVec2(left_group_x, left_group_y), ImGuiCond_FirstUseEver);

        if (ImGui::Begin("Wheels", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize))
        {
            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            // tire constants (2x size)
            const float tire_width   = 48.0f;
            const float tire_height  = 80.0f;
            const float tire_space_x = 120.0f;
            const float tire_space_y = 140.0f;  // increased for more vertical separation
            const float force_scale  = 0.003f;
            const float max_arrow    = 50.0f;

            // suspension constants (2x size)
            const float coil_width    = 48.0f;
            const float max_height    = 110.0f;
            const float min_height    = 40.0f;
            const int   coil_segments = 7;
            const float susp_space_x  = 100.0f;
            const float susp_space_y  = 160.0f;  // increased for more vertical separation
            const float susp_offset_x = 320.0f;  // adjusted for larger tires

            if (ImGui::CollapsingHeader("Wheel Forces & Suspension", ImGuiTreeNodeFlags_DefaultOpen))
            {
                ImVec2 section_start = ImGui::GetCursorScreenPos();
                const float start_y = 30.0f;

                // helper: draw an arrow
                auto draw_arrow = [&](ImVec2 center, float dx, float dy, ImU32 color, float thickness = 3.0f)
                {
                    if (fabsf(dx) < 1.0f && fabsf(dy) < 1.0f)
                        return;
                    ImVec2 tip = ImVec2(center.x + dx, center.y + dy);
                    draw_list->AddLine(center, tip, color, thickness);
                    float len = sqrtf(dx * dx + dy * dy);
                    if (len > 5.0f)
                    {
                        float nx = dx / len, ny = dy / len;
                        float hs = std::min(len * 0.3f, 10.0f);  // larger arrow heads
                        draw_list->AddTriangleFilled(tip,
                            ImVec2(tip.x - hs * (nx + ny * 0.5f), tip.y - hs * (ny - nx * 0.5f)),
                            ImVec2(tip.x - hs * (nx - ny * 0.5f), tip.y - hs * (ny + nx * 0.5f)), color);
                    }
                };

                // force arrow colors (used for legend too)
                const ImU32 color_lateral  = IM_COL32(100, 150, 255, 255);  // blue - lateral/cornering
                const ImU32 color_traction = IM_COL32(100, 255, 100, 255);  // green - acceleration
                const ImU32 color_braking  = IM_COL32(255, 100, 100, 255);  // red - braking

                // slip colors (used for legend too)
                const ImU32 color_slip_angle = IM_COL32(255, 200, 100, 255);  // orange - slip angle
                const ImU32 color_slip_ratio = IM_COL32(200, 100, 255, 255);  // purple - slip ratio

                // helper: draw a tire with force arrows
                auto draw_tire = [&](const char* label, WheelIndex wheel, float offset_x, float offset_y)
                {
                    ImVec2 center = ImVec2(section_start.x + offset_x + tire_width * 0.5f, section_start.y + offset_y + tire_height * 0.5f);
                    ImVec2 tl = ImVec2(section_start.x + offset_x, section_start.y + offset_y);
                    ImVec2 br = ImVec2(tl.x + tire_width, tl.y + tire_height);

                    bool grounded = physics->IsWheelGrounded(wheel);
                    draw_list->AddRectFilled(tl, br, grounded ? IM_COL32(60, 60, 60, 255) : IM_COL32(80, 40, 40, 255), 8.0f);
                    draw_list->AddRect(tl, br, grounded ? IM_COL32(120, 120, 120, 255) : IM_COL32(150, 80, 80, 255), 8.0f, 0, 3.0f);

                    float lat_f = physics->GetWheelLateralForce(wheel);
                    float lon_f = physics->GetWheelLongitudinalForce(wheel);
                    float lat_arrow = std::clamp(lat_f * force_scale, -max_arrow, max_arrow);
                    float lon_arrow = std::clamp(-lon_f * force_scale, -max_arrow, max_arrow);

                    if (fabsf(lat_arrow) > 2.0f)
                        draw_arrow(center, lat_arrow, 0.0f, color_lateral, 3.5f);
                    if (fabsf(lon_arrow) > 2.0f)
                        draw_arrow(center, 0.0f, lon_arrow, (lon_f > 0) ? color_traction : color_braking, 3.5f);

                    // label centered above tire
                    ImVec2 label_size = ImGui::CalcTextSize(label);
                    float label_x = tl.x + (tire_width - label_size.x) * 0.5f;
                    draw_list->AddText(ImVec2(label_x, tl.y - label_size.y - 6), IM_COL32(255, 255, 255, 255), label);

                    // slip info below tire - side by side, centered
                    float slip_angle = physics->GetWheelSlipAngle(wheel) * 57.2958f;
                    float slip_ratio = physics->GetWheelSlipRatio(wheel);

                    // build both texts to calculate total width
                    char angle_text[16];
                    snprintf(angle_text, sizeof(angle_text), "%.0f\xC2\xB0", slip_angle);  // degree symbol
                    char ratio_text[16];
                    snprintf(ratio_text, sizeof(ratio_text), "%.0f%%", slip_ratio * 100.0f);

                    ImVec2 angle_size = ImGui::CalcTextSize(angle_text);
                    ImVec2 ratio_size = ImGui::CalcTextSize(ratio_text);
                    const float spacing = 8.0f;  // space between the two values
                    float total_width = angle_size.x + spacing + ratio_size.x;
                    float slip_start_x = tl.x + (tire_width - total_width) * 0.5f;

                    // slip angle (orange) on left
                    draw_list->AddText(ImVec2(slip_start_x, br.y + 6), color_slip_angle, angle_text);
                    // slip ratio (purple) on right
                    draw_list->AddText(ImVec2(slip_start_x + angle_size.x + spacing, br.y + 6), color_slip_ratio, ratio_text);
                };

                // helper: draw a coil spring
                auto draw_coil = [&](const char* label, float compression, float offset_x, float offset_y)
                {
                    float cx = section_start.x + offset_x + coil_width * 0.5f;
                    float top_y = section_start.y + offset_y;
                    float ext = 1.0f - compression;
                    float spring_h = min_height + (max_height - min_height) * ext;

                    ImU32 color = (compression > 0.8f) ? IM_COL32(220, 50, 50, 255) :
                                  (compression > 0.5f) ? IM_COL32(220, 180, 50, 255) : IM_COL32(50, 200, 50, 255);

                    // top mount plate (scaled)
                    draw_list->AddRectFilled(ImVec2(cx - 18, top_y), ImVec2(cx + 18, top_y + 6), IM_COL32(100, 100, 100, 255));

                    float seg_h = spring_h / coil_segments;
                    float hw = coil_width * 0.4f;
                    float coil_top = top_y + 8;

                    for (int i = 0; i < coil_segments; i++)
                    {
                        float y1 = coil_top + i * seg_h;
                        float y2 = coil_top + (i + 0.5f) * seg_h;
                        float y3 = coil_top + (i + 1) * seg_h;
                        float xl = cx - hw, xr = cx + hw;

                        if (i % 2 == 0)
                        {
                            draw_list->AddLine(ImVec2(xl, y1), ImVec2(xr, y2), color, 4.0f);
                            draw_list->AddLine(ImVec2(xr, y2), ImVec2(xl, y3), color, 4.0f);
                        }
                        else
                        {
                            draw_list->AddLine(ImVec2(xr, y1), ImVec2(xl, y2), color, 4.0f);
                            draw_list->AddLine(ImVec2(xl, y2), ImVec2(xr, y3), color, 4.0f);
                        }
                    }

                    // bottom mount plate (scaled)
                    float bot_y = coil_top + spring_h;
                    draw_list->AddRectFilled(ImVec2(cx - 18, bot_y), ImVec2(cx + 18, bot_y + 6), IM_COL32(100, 100, 100, 255));
                    draw_list->AddLine(ImVec2(cx, top_y + 6), ImVec2(cx, bot_y), IM_COL32(70, 70, 70, 255), 2.5f);

                    // label centered above spring
                    ImVec2 label_size = ImGui::CalcTextSize(label);
                    float label_x = cx - label_size.x * 0.5f;
                    draw_list->AddText(ImVec2(label_x, top_y - label_size.y - 6), IM_COL32(255, 255, 255, 255), label);

                    // percentage below spring
                    char pct[16];
                    snprintf(pct, sizeof(pct), "%.0f%%", compression * 100.0f);
                    ImVec2 pct_size = ImGui::CalcTextSize(pct);
                    draw_list->AddText(ImVec2(cx - pct_size.x * 0.5f, bot_y + 10), IM_COL32(180, 180, 180, 255), pct);
                };

                // draw tires (left side) - offset down to make room for labels
                draw_tire("FL", WheelIndex::FrontLeft,  20.0f, start_y);
                draw_tire("FR", WheelIndex::FrontRight, 20.0f + tire_space_x, start_y);
                draw_tire("RL", WheelIndex::RearLeft,   20.0f, start_y + tire_space_y + 40.0f);  // extra space for slip text
                draw_tire("RR", WheelIndex::RearRight,  20.0f + tire_space_x, start_y + tire_space_y + 40.0f);

                // draw suspension (right side)
                float comp_fl = physics->GetWheelCompression(WheelIndex::FrontLeft);
                float comp_fr = physics->GetWheelCompression(WheelIndex::FrontRight);
                float comp_rl = physics->GetWheelCompression(WheelIndex::RearLeft);
                float comp_rr = physics->GetWheelCompression(WheelIndex::RearRight);

                draw_coil("FL", comp_fl, susp_offset_x, start_y);
                draw_coil("FR", comp_fr, susp_offset_x + susp_space_x, start_y);
                draw_coil("RL", comp_rl, susp_offset_x, start_y + susp_space_y + 40.0f);
                draw_coil("RR", comp_rr, susp_offset_x + susp_space_x, start_y + susp_space_y + 40.0f);

                // reserve space for the full layout - calculate based on actual content
                // rear row starts at: start_y + susp_space_y + 40 = 230
                // suspension bottom: +8 (top plate) + max_height + 6 (bottom plate) = 124
                // percentage text below: +25
                float content_height = start_y + susp_space_y + 40.0f + max_height + 40.0f;
                ImGui::Dummy(ImVec2(susp_offset_x + susp_space_x + coil_width + 40, content_height));

                // force legend
                ImGui::Separator();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Tire Forces:");

                // helper: draw legend item with colored square
                auto draw_legend_item = [&](ImU32 color, const char* text)
                {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    draw_list->AddRectFilled(pos, ImVec2(pos.x + 12, pos.y + 12), color);
                    ImGui::Dummy(ImVec2(16, 12));
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "%s", text);
                };

                draw_legend_item(color_lateral,  "lateral (cornering force)");
                draw_legend_item(color_traction, "longitudinal (acceleration)");
                draw_legend_item(color_braking,  "longitudinal (braking)");

                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Slip Values (below tires):");
                draw_legend_item(color_slip_angle, "slip angle - tire direction vs travel");
                draw_legend_item(color_slip_ratio, "slip ratio - wheel spin vs vehicle speed");
            }

            // temperature table
            if (ImGui::CollapsingHeader("Temperature", ImGuiTreeNodeFlags_DefaultOpen))
            {
                if (ImGui::BeginTable("temps", 5, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
                {
                    ImGui::TableSetupColumn("Wheel");
                    ImGui::TableSetupColumn("Tire C");
                    ImGui::TableSetupColumn("Grip %");
                    ImGui::TableSetupColumn("Brake C");
                    ImGui::TableSetupColumn("Brake Eff %");
                    ImGui::TableHeadersRow();

                    for (int i = 0; i < 4; i++)
                    {
                        WheelIndex wheel = static_cast<WheelIndex>(i);
                        float tire_temp  = physics->GetWheelTemperature(wheel);
                        float grip       = physics->GetWheelTempGripFactor(wheel);
                        float brake_temp = physics->GetWheelBrakeTemp(wheel);
                        float brake_eff  = physics->GetWheelBrakeEfficiency(wheel);

                        ImGui::TableNextRow();
                        ImGui::TableNextColumn(); ImGui::Text("%s", wheel_names[i]);
                        ImGui::TableNextColumn();
                        {
                            ImVec4 col = (tire_temp > 110) ? ImVec4(1, 0.5f, 0, 1) :
                                         (tire_temp < 70)  ? ImVec4(0.5f, 0.5f, 1, 1) :
                                         ImVec4(0.2f, 1, 0.2f, 1);
                            ImGui::TextColored(col, "%.0f", tire_temp);
                        }
                        ImGui::TableNextColumn(); ImGui::Text("%.0f", grip * 100.0f);
                        ImGui::TableNextColumn();
                        {
                            ImVec4 col = (brake_temp > 700) ? ImVec4(1, 0, 0, 1) :
                                         (brake_temp > 400) ? ImVec4(1, 0.5f, 0, 1) :
                                         ImVec4(0.8f, 0.8f, 0.8f, 1);
                            ImGui::TextColored(col, "%.0f", brake_temp);
                        }
                        ImGui::TableNextColumn(); ImGui::Text("%.0f", brake_eff * 100.0f);
                    }
                    ImGui::EndTable();
                }
            }

            // debug toggles
            if (ImGui::CollapsingHeader("Debug"))
            {
                bool draw_rays = physics->GetDrawRaycasts();
                bool draw_susp = physics->GetDrawSuspension();
                if (ImGui::Checkbox("Draw Raycasts", &draw_rays))
                    physics->SetDrawRaycasts(draw_rays);
                if (ImGui::Checkbox("Draw Suspension", &draw_susp))
                    physics->SetDrawSuspension(draw_susp);

                // 3d debug visualization legend
                if (draw_rays || draw_susp)
                {
                    ImGui::Spacing();
                    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "3D Visualization Legend:");

                    auto draw_debug_legend = [&](float r, float g, float b, const char* text)
                    {
                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        draw_list->AddRectFilled(pos, ImVec2(pos.x + 10, pos.y + 10), IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 255));
                        ImGui::Dummy(ImVec2(14, 10));
                        ImGui::SameLine();
                        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "%s", text);
                    };

                    if (draw_rays)
                    {
                        draw_debug_legend(0.0f, 1.0f, 0.0f, "raycast hit ground");
                        draw_debug_legend(1.0f, 0.0f, 0.0f, "raycast missed");
                    }
                    if (draw_susp)
                    {
                        draw_debug_legend(1.0f, 1.0f, 0.0f, "suspension top mount");
                        draw_debug_legend(0.0f, 0.5f, 1.0f, "suspension wheel contact");
                    }
                }
            }
        }
        ImGui::End();
    }
}
