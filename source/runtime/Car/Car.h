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

#pragma once

//= INCLUDES ===================
#include <vector>
#include <memory>
#include "../Math/Vector3.h"
#include "../Math/BoundingBox.h"
//==============================

namespace pugi
{
    class xml_node;
}

namespace spartan
{
    class Entity;
    class Physics;

    // view modes for car camera
    enum class CarView
    {
        Chase,
        Hood,
        Dashboard
    };

    // self-contained drivable car class
    // handles entity hierarchy creation, physics setup, input, camera, sounds, and telemetry
    class Car
    {
    public:
        // configuration for car creation
        struct Config
        {
            math::Vector3 position       = math::Vector3::Zero;
            bool          drivable       = false;  // creates vehicle physics with wheels
            bool          static_physics = false;  // kinematic physics on the body (for display)
            bool          show_telemetry = false;  // shows vehicle telemetry hud
            bool          camera_follows = false;  // attach camera to follow the car
        };

        // factory method - creates a car and adds it to the registry
        static Car* Create(const Config& config);

        // create car from prefab xml node (for world loading)
        static Entity* CreatePrefab(pugi::xml_node& node, Entity* parent);

        // global lifecycle (tick happens automatically through entity system)
        static void ShutdownAll();
        static std::vector<Car*>& GetAll();

        // instance lifecycle
        void Destroy();
        
        // called automatically by the Physics component through the entity tick system
        void Tick();

        // entity access
        Entity* GetRootEntity() const  { return m_vehicle_entity; }
        Entity* GetBodyEntity() const  { return m_body_entity; }
        Entity* GetWindowEntity() const { return m_window_entity; }

        // vehicle interaction
        void Enter();
        void Exit();
        bool IsOccupied() const { return m_is_occupied; }

        // controls (only effective when occupied)
        void SetThrottle(float value);
        void SetBrake(float value);
        void SetSteering(float value);
        void SetHandbrake(float value);
        void ResetToSpawn();

        // view control
        void CycleView();
        CarView GetCurrentView() const { return m_current_view; }

        // telemetry
        void SetShowTelemetry(bool show) { m_show_telemetry = show; }
        bool GetShowTelemetry() const { return m_show_telemetry; }

        // camera orbit (right stick control)
        void AddCameraOrbitYaw(float delta);
        void AddCameraOrbitPitch(float delta);
        void DecayCameraOrbit(float dt);

    private:
        Car() = default;
        ~Car() = default;

        // helper to compute bounding box from all renderables in hierarchy
        math::BoundingBox GetCarAABB() const;

        // creation helpers
        Entity* CreateBody(bool remove_wheels, std::vector<Entity*>* out_excluded_entities = nullptr);
        void CreateWheels(Entity* vehicle_ent, Physics* physics);
        void CreateAudioSources(Entity* parent_entity);

        // tick helpers
        void TickInput();
        void TickSounds();
        void TickChaseCamera();
        void TickEnterExit();
        void TickViewSwitch();
        void DrawTelemetry();

        // chase camera - gt7 style
        struct ChaseCameraState
        {
            math::Vector3 position     = math::Vector3::Zero;
            math::Vector3 velocity     = math::Vector3::Zero;
            float         yaw          = 0.0f;
            float         yaw_bias     = 0.0f;
            float         pitch_bias   = 0.0f;
            float         speed_factor = 0.0f;
            bool          initialized  = false;
        };

        static math::Vector3 SmoothDamp(const math::Vector3& current, const math::Vector3& target, 
                                        math::Vector3& velocity, float smooth_time, float dt);
        static float LerpAngle(float a, float b, float t);

        // chase camera tuning
        static constexpr float chase_distance_base      = 5.0f;
        static constexpr float chase_distance_min       = 4.0f;
        static constexpr float chase_height_base        = 1.5f;
        static constexpr float chase_height_min         = 1.2f;
        static constexpr float chase_position_smoothing = 0.15f;
        static constexpr float chase_rotation_smoothing = 4.0f;
        static constexpr float chase_speed_smoothing    = 2.0f;
        static constexpr float chase_look_offset_up     = 0.6f;
        static constexpr float chase_look_ahead_amount  = 2.5f;
        static constexpr float chase_speed_reference    = 50.0f;
        static constexpr float orbit_bias_speed         = 1.5f;
        static constexpr float orbit_bias_decay         = 4.0f;
        static constexpr float yaw_bias_max             = 3.14159265f;
        static constexpr float pitch_bias_max           = 1.2f;

        // instance state
        Entity*           m_vehicle_entity  = nullptr;  // root entity for drivable cars
        Entity*           m_body_entity     = nullptr;  // car body mesh entity
        Entity*           m_window_entity   = nullptr;  // car window entity (for hiding when inside)
        math::Vector3     m_spawn_position  = math::Vector3::Zero;
        bool              m_is_occupied     = false;
        bool              m_show_telemetry  = false;
        bool              m_is_drivable     = false;
        bool              m_camera_follows  = false;    // auto-enter car when play mode starts
        bool              m_was_playing     = false;    // tracks play mode state for auto-enter
        CarView           m_current_view    = CarView::Chase;
        ChaseCameraState  m_chase_camera;

        // sound state
        float m_tire_squeal_volume = 0.0f;

        // haptic feedback state
        float m_haptic_left  = 0.0f;
        float m_haptic_right = 0.0f;

        // static registry of all cars
        static std::vector<Car*> s_cars;
    };
}
