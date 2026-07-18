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

//= INCLUDES ======================
#include "Component.h"
#include <cstdint>
#include <vector>
#include <memory>
#include "../../Math/Vector3.h"
#include "../../Math/Quaternion.h"
#include "../../RHI/RHI_Vertex.h"
//=================================

namespace sol
{
    class state_view;
}

namespace car
{
    struct car_preset;
    class Simulation;
}

namespace spartan
{
    class Entity;
    class Mesh;
    class PhysicsWorld;
    namespace math { class Quaternion; }

    enum class PhysicsForce
    {
        Constant,
        Impulse
    };

    enum class BodyType
    {
        Box,
        Sphere,
        Plane,
        Capsule,
        Mesh,
        MeshConvex, // compound shape built from convex hulls of entity hierarchy meshes
        Controller,
        Vehicle,
        Cloth,      // deformable surface simulated via verlet integration
        Max
    };

    // wheel indices for vehicles
    enum class WheelIndex
    {
        FrontLeft  = 0,
        FrontRight = 1,
        RearLeft   = 2,
        RearRight  = 3,
        Count      = 4
    };

    class Physics : public Component
    {
    public:
        Physics(Entity* entity);
        ~Physics();

        // component
        void Initialize() override;
        void Remove() override;
        void PreTick() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        static void RegisterForScripting(sol::state_view State);
        sol::reference AsLua(sol::state_view state) override;

        // static cleanup (call before physics world shutdown)
        static void Shutdown();

        // fft water buoyancy, runs once per fixed physics step for every dynamic body
        static void TickBuoyancy();

        // mass
        constexpr static inline float mass_from_volume = FLT_MAX;
        float GetMass() const { return m_mass; }
        void SetMass(float mass);

        // friction
        float GetFriction() const { return m_friction; }
        void SetFriction(float friction);

        // angular friction
        float GetFrictionRolling() const { return m_friction_rolling; }
        void SetFrictionRolling(float friction_rolling);

        // restitution
        float GetRestitution() const { return m_restitution; }
        void SetRestitution(float restitution);

        // forces
        void SetLinearVelocity(const math::Vector3& velocity) const;
        math::Vector3 GetLinearVelocity() const;
        void SetAngularVelocity(const math::Vector3& velocity) const;
        void ApplyForce(const math::Vector3& force, PhysicsForce mode) const;

        // position lock
        void SetPositionLock(bool lock);
        void SetPositionLock(const math::Vector3& lock);
        math::Vector3 GetPositionLock() const { return m_position_lock; }

        // rotation lock
        void SetRotationLock(bool lock);
        void SetRotationLock(const math::Vector3& lock);
        math::Vector3 GetRotationLock() const { return m_rotation_lock; }

        // center of mass
        void SetCenterOfMass(const math::Vector3& center_of_mass);
        const math::Vector3& GetCenterOfMass() const { return m_center_of_mass; }

        // body type
        BodyType GetBodyType() const { return m_body_type; }
        void SetBodyType(BodyType type);
        BodyType DetectBodyType();

        // ground
        bool IsGrounded() const;
        Entity* GetGroundEntity() const;

        // dimensional properties
        float GetCapsuleVolume();
        float GetCapsuleRadius();
        math::Vector3 GetControllerTopLocal() const;

        // static
        bool IsStatic() const { return m_is_static; }
        void SetStatic(bool is_static);

        // kinematic
        bool IsKinematic() const { return m_is_kinematic; }
        void SetKinematic(bool is_kinematic);

        // enabled (controls whether the physics body processes input/forces)
        bool IsEnabled() const  { return m_enabled; }
        void SetEnabled(bool enabled) { m_enabled = enabled; }

        // misc
        void Move(const math::Vector3& offset);
        void Crouch(const bool crouch);
        void SetBodyTransform(const math::Vector3& position, const math::Quaternion& rotation, bool rebuild_vehicle = true); // teleport physics body

        // vehicle controls (only works when body type is Vehicle)
        void SetVehicleThrottle(float value);   // 0 to 1
        void SetVehicleBrake(float value);      // 0 to 1
        void SetVehicleSteering(float value);   // -1 (left) to 1 (right)
        void SetVehicleHandbrake(float value);  // 0 to 1 (locks rear wheels for drifting)
        void SetVehicleSimulationActive(bool active);
        bool IsVehicleSimulationActive() const { return m_vehicle_simulation_active; }
        void SetVehicleHighQuality(bool high_quality) { m_vehicle_high_quality = high_quality; }

        // vehicle wheel entities (visual meshes that rotate with physics)
        void SetWheelEntity(WheelIndex wheel, Entity* entity);
        Entity* GetWheelEntity(WheelIndex wheel) const;

        // chassis visual entity and optional convex exclusions
        void SetChassisEntity(Entity* entity, const std::vector<Entity*>& entities_to_exclude = {});
        Entity* GetChassisEntity() const { return m_chassis_entity; }

        // vehicle methods target the single active car simulation
        void SetWheelRadius(float radius);
        float GetWheelRadius() const { return m_wheel_radius; }
        float GetSuspensionHeight() const; // distance from body center to wheel center
        void ComputeWheelRadiusFromEntity(Entity* wheel_entity); // auto-compute from mesh AABB
        // wheel visual scale derives from unscaled local mesh bounds
        void ScaleWheelEntityToDimensions(Entity* wheel_entity, float target_radius, float target_width);

        // read only vehicle telemetry
        float GetVehicleThrottle() const;
        float GetVehicleBrake() const;
        float GetVehicleSteering() const;
        float GetVehicleHandbrake() const;
        bool IsWheelGrounded(WheelIndex wheel) const;
        float GetWheelCompression(WheelIndex wheel) const;
        float GetWheelSuspensionForce(WheelIndex wheel) const;
        float GetWheelSlipAngle(WheelIndex wheel) const;
        float GetWheelSlipRatio(WheelIndex wheel) const;
        math::Vector3 GetWheelContactPoint(WheelIndex wheel) const;  // world-space ground contact
        math::Vector3 GetWheelContactNormal(WheelIndex wheel) const; // world-space ground normal
        float GetWheelSlipMagnitude(WheelIndex wheel) const;         // hypot of slip ratio and slip angle
        float GetWheelWidth(WheelIndex wheel) const;                 // physical tire width for this axle
        float GetWheelTireLoad(WheelIndex wheel) const;
        float GetWheelLateralForce(WheelIndex wheel) const;
        float GetWheelLongitudinalForce(WheelIndex wheel) const;
        float GetWheelAngularVelocity(WheelIndex wheel) const;  // rad/s
        float GetWheelRPM(WheelIndex wheel) const;              // revolutions per minute
        float GetWheelTemperature(WheelIndex wheel) const;
        float GetWheelTempGripFactor(WheelIndex wheel) const;
        float GetWheelBrakeTemp(WheelIndex wheel) const;
        float GetWheelBrakeEfficiency(WheelIndex wheel) const;
        float GetWheelSurfaceTemp(WheelIndex wheel, int zone) const;
        float GetWheelCoreTemp(WheelIndex wheel) const;
        float GetTirePressure() const;
        float GetTirePressureOptimal() const;

        // driver assists
        void SetAbsEnabled(bool enabled);
        bool GetAbsEnabled() const;
        bool IsAbsActive(WheelIndex wheel) const;               // is abs intervening on this wheel
        bool IsAbsActiveAny() const;                            // is abs intervening on any wheel
        float GetAbsPhase() const;                              // 0..1 modulation cycle, grab when >= 0.5

        void SetTcEnabled(bool enabled);
        bool GetTcEnabled() const;
        bool IsTcActive() const;                                // is traction control intervening
        float GetTcReduction() const;                           // current power reduction (0-1)

        // turbo
        void SetTurboEnabled(bool enabled);
        bool GetTurboEnabled() const;
        float GetBoostPressure() const;                         // current boost pressure (bar)
        float GetBoostMaxPressure() const;                      // max boost pressure (bar)

        // drs (drag reduction system)
        void SetDrsEnabled(bool enabled);
        bool GetDrsEnabled() const;
        void SetDrsActive(bool active);
        bool GetDrsActive() const;

        // differential type (0 = open, 1 = locked, 2 = lsd)
        void SetDiffType(int type);
        int  GetDiffType() const;
        const char* GetDiffTypeName() const;

        // tire wear
        float GetWheelWear(WheelIndex wheel) const;            // 0-1, 0 = new, 1 = destroyed
        float GetWheelWearGripFactor(WheelIndex wheel) const;  // grip multiplier from wear
        void  ResetTireWear();

        // transmission mode
        void SetManualTransmission(bool enabled);
        bool GetManualTransmission() const;
        void ShiftUp();
        void ShiftDown();
        void ShiftToNeutral();

        // engine and gearbox
        int GetCurrentGear() const;                             // gear index (0=R, 1=N, 2-8=1st-7th)
        const char* GetCurrentGearString() const;               // gear display string ("R", "N", "1"-"7")
        float GetEngineRPM() const;                             // current engine rpm
        float GetEngineTorque() const;                          // current engine (ice) torque output (Nm)
        float GetMotorTorque() const;                           // current electric motor torque (Nm)
        float GetIdleRPM() const;                               // engine idle rpm
        float GetRedlineRPM() const;                            // engine redline rpm
        bool IsShifting() const;                                // is gearbox currently shifting

        math::Vector3 TransformVehiclePointToRender(const math::Vector3& point) const;
        math::Quaternion TransformVehicleRotationToRender(const math::Quaternion& rotation) const;

        // sync physics wheel positions from wheel entity positions
        void SyncWheelOffsetsFromEntities();

        // car owner - set this to have the car tick automatically through the entity system
        void SetCar(class Car* car) { m_car = car; }
        class Car* GetCar() const   { return m_car; }
        car::Simulation* GetVehicleSimulation() const { return m_vehicle_simulation.get(); }
        uint32_t GetVehicleCollisionGroup() const;
        void SetVehiclePreset(const car::car_preset& preset);
        void SetVehicleSimulationFrequency(float frequency);

        // center of mass (for tuning handling characteristics)
        void SetCenterOfMassOffset(const math::Vector3& offset);
        void SetCenterOfMassOffset(float x, float y, float z);
        math::Vector3 GetCenterOfMassOffset() const;

        // mesh convex compound shape - set the source entity whose hierarchy will be walked
        // to build convex hull shapes from each mesh in the hierarchy
        void SetMeshConvexSourceEntity(Entity* entity);
        Entity* GetMeshConvexSourceEntity() const { return m_mesh_convex_source; }

        // cloth simulation parameters (only applies when body type is Cloth)
        float GetClothStiffness() const            { return m_cloth_stiffness; }
        void  SetClothStiffness(float stiffness)   { m_cloth_stiffness = std::clamp(stiffness, 0.0f, 1.0f); }
        float GetClothDamping() const              { return m_cloth_damping; }
        void  SetClothDamping(float damping)       { m_cloth_damping = std::clamp(damping, 0.0f, 1.0f); }
        uint32_t GetClothIterations() const        { return m_cloth_iterations; }
        void     SetClothIterations(uint32_t count) { m_cloth_iterations = std::clamp(count, 1u, 32u); }
        bool GetClothWindEnabled() const             { return m_cloth_wind_enabled; }
        void SetClothWindEnabled(bool enabled)       { m_cloth_wind_enabled = enabled; }
        const math::Vector3& GetClothPinDirection() const { return m_cloth_pin_direction; }
        void SetClothPinDirection(const math::Vector3& direction);

    private:
        // tick helpers (broken out for readability)
        void TickController(bool is_playing, float delta_time);
        void TickVehicle(bool is_playing);
        void TickVehicleSubstep(float dt); // vehicle force model, runs once per fixed physics step in lockstep with integration
        car::Simulation* EnsureVehicleSimulation();
        void TickCloth(bool is_playing, float delta_time);
        void TickDynamicBodies(bool is_playing);
        void TickDistanceActivation();
        void ApplyBuoyancy();
        float ComputeVolume();

        void UpdateWheelTransforms();
        void Create();
        void CreateBodies();
        void CreateCloth();
        void BuildChassisConvexShapes(Entity* chassis_entity, const std::vector<Entity*>& entities_to_exclude); // builds convex shapes from chassis mesh hierarchy

        float m_mass                   = 1.0f;
        float m_friction               = 0.4f;
        float m_friction_rolling       = 0.4f;
        float m_restitution            = 0.2f;
        bool m_is_static               = true;
        bool m_is_kinematic            = false;
        bool m_enabled                 = true;
        math::Vector3 m_position_lock  = math::Vector3::Zero;
        math::Vector3 m_rotation_lock  = math::Vector3::Zero;
        math::Vector3 m_center_of_mass = math::Vector3::Zero;
        math::Vector3 m_velocity       = math::Vector3::Zero;
        BodyType m_body_type           = BodyType::Max;
        void* m_controller               = nullptr;
        void* m_material                 = nullptr;
        void* m_mesh                     = nullptr;
        std::vector<void*> m_actors      = { nullptr };
        std::vector<bool> m_actors_active; // tracks which actors are currently in the scene (for distance-based activation)

        // vehicle wheel entities and state
        Entity* m_wheel_entities[static_cast<int>(WheelIndex::Count)] = { nullptr, nullptr, nullptr, nullptr };
        float m_wheel_radius   = 0.35f; // wheel radius for spin calculation (default)
        math::Vector3 m_wheel_mesh_center_offsets[static_cast<int>(WheelIndex::Count)] = {};
        bool m_wheel_offsets_synced = false;  // flag to ensure wheel offsets are synced from entities once
        const void* m_wheel_ground_actors[static_cast<int>(WheelIndex::Count)] = {};
        uint8_t m_wheel_ground_surfaces[static_cast<int>(WheelIndex::Count)] = {};
        bool m_vehicle_simulation_active = true;
        bool m_vehicle_high_quality = true;

        // vehicle chassis entity and suspension state
        Entity* m_chassis_entity          = nullptr;
        std::vector<Entity*> m_chassis_entities_to_exclude;
        math::Vector3 m_chassis_base_pos  = math::Vector3::Zero; // base local position of chassis
        float m_chassis_suspension_offset = 0.0f;                // current suspension offset (smoothed)

        // mesh convex source entity - the entity hierarchy to walk for building compound convex shapes
        Entity* m_mesh_convex_source = nullptr;

        // deferred creation flag for loading (wait until renderable is available)
        bool m_needs_creation = false;

        // cached scale for detecting editor-time scale changes
        math::Vector3 m_scale_previous = math::Vector3::Zero;

        void UpdateShapeGeometry();

        // interpolation state for smooth rendering between fixed physics timesteps
        math::Vector3 m_prev_position     = math::Vector3::Zero; // position at previous physics step
        math::Quaternion m_prev_rotation;                        // rotation at previous physics step
        math::Vector3 m_current_position  = math::Vector3::Zero; // position at current physics step
        math::Quaternion m_current_rotation;                     // rotation at current physics step
        bool m_interpolation_initialized  = false;               // flag to track first-frame initialization

        math::Vector3 m_vehicle_physics_position = math::Vector3::Zero;
        math::Quaternion m_vehicle_physics_rotation;
        math::Vector3 m_vehicle_render_position = math::Vector3::Zero;
        math::Quaternion m_vehicle_render_rotation;

        // car owner (ticked automatically through entity system)
        class Car* m_car = nullptr;
        std::unique_ptr<car::Simulation> m_vehicle_simulation;
        float m_vehicle_simulation_interval = 0.0f;
        float m_vehicle_simulation_accumulator = 0.0f;

        // cloth simulation state
        struct ClothParticle
        {
            math::Vector3 position;
            math::Vector3 previous_position;
            float inverse_mass = 1.0f; // 0 = pinned
        };

        struct ClothConstraint
        {
            uint32_t index_a = 0;
            uint32_t index_b = 0;
            float rest_length = 0.0f;
        };

        std::vector<ClothParticle> m_cloth_particles;
        std::vector<ClothConstraint> m_cloth_constraints;
        std::vector<uint32_t> m_cloth_indices;           // triangle indices for normal recalculation
        std::vector<RHI_Vertex_PosTexNorTan> m_cloth_base_vertices; // cached original vertices (for tex/tan preservation)
        std::vector<uint32_t> m_cloth_weld_map;          // maps each vertex to its canonical (lowest-index coincident) vertex
        std::shared_ptr<Mesh> m_cloth_mesh;
        uint32_t m_cloth_global_vertex_offset = 0;       // offset into the global geometry buffer
        uint32_t m_cloth_vertex_count         = 0;
        float m_cloth_stiffness               = 0.9f;    // constraint stiffness (0-1)
        float m_cloth_damping                 = 0.01f;   // velocity damping (0-1)
        uint32_t m_cloth_iterations           = 8;       // constraint solver iterations per step
        bool m_cloth_wind_enabled             = true;
        math::Vector3 m_cloth_pin_direction   = math::Vector3::Up;
    };
}
