/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once
#include "../world/components/Physics.h"
#include "CarPhysicsTypes.h"
namespace car { struct car_preset; class Simulation; }
namespace spartan
{
    class CarPhysics final : public PhysicsBody
    {
    public:
        explicit CarPhysics(Physics& owner) : physics(owner) {}
        ~CarPhysics() override;
        static CarPhysics& Get(const Physics& owner);
        static void Initialize();
        static void RegisterForScripting(sol::state_view state);
        void* Create() override;
        void Remove() override;
        void Tick(bool playing) override;
        void ShiftOrigin(const math::Vector3& shift) override;
        bool SetTransform(const math::Vector3& position, const math::Quaternion& rotation, bool reset_simulation) override;

        // vehicle controls (only works when body type is Vehicle)
        void SetVehicleThrottle(float value);   // 0 to 1
        void SetVehicleBrake(float value);      // 0 to 1
        void SetVehicleSteering(float value);   // -1 (left) to 1 (right)
        void SetVehicleHandbrake(float value);  // 0 to 1 (locks rear wheels for drifting)
        void SetVehicleSimulationActive(bool active);
        bool IsVehicleSimulationActive() const;
        void UpdateTrafficWheels(float speed, float curvature, float delta_time);
        void SetVehicleBrakeReverseEnabled(bool enabled);
        bool GetVehicleBrakeReverseEnabled() const;
        void SetVehicleFullSteeringLock(bool enabled);
        void SetVehicleRoadSurface(const math::Vector3& position, const math::Vector3& tangent);
        void SetVehicleSimMode(VehicleSimMode mode);
        VehicleSimMode GetVehicleSimMode() const;

        // vehicle wheel entities (visual meshes that rotate with physics)
        void SetWheelEntity(WheelIndex wheel, Entity* entity);
        Entity* GetWheelEntity(WheelIndex wheel) const;

        // chassis visual entity and optional convex exclusions
        void SetChassisEntity(Entity* entity, const std::vector<Entity*>& entities_to_exclude = {});
        Entity* GetChassisEntity() const;

        // vehicle methods target the single active car simulation
        void SetWheelRadius(float radius);
        float GetWheelRadius() const;
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
        float GetWheelFrictionUse(WheelIndex wheel) const;           // force over the peak the patch can make, 1 is at the limit
        float GetWheelPeakLateralForce(WheelIndex wheel) const;      // N, the most cornering force available right now
        float GetWheelPeakLongitudinalForce(WheelIndex wheel) const; // N, the most drive or brake force available right now
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
        bool IsBurnoutActive() const;                           // line lock held, fronts braked and tc stood down

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
        void SetCar(class Car* car);
        class Car* GetCar() const;
        car::Simulation* GetVehicleSimulation() const;
        uint32_t GetVehicleCollisionGroup() const;
        void SetVehiclePreset(const car::car_preset& preset);
        void SetVehicleSimulationFrequency(float frequency);

        // center of mass (for tuning handling characteristics)
        void SetCenterOfMassOffset(const math::Vector3& offset);
        void SetCenterOfMassOffset(float x, float y, float z);
        math::Vector3 GetCenterOfMassOffset() const;

    private:
        Physics& physics;
        Entity* GetEntity() const { return physics.GetEntity(); }

        mutable std::unique_ptr<VehiclePhysicsState, VehiclePhysicsDeleter> m_vehicle;
        VehiclePhysicsState& VehicleState() const;

        void TickVehicleSubstep(float dt);
        void TickVehicleCheapSubstep(float dt);
        car::Simulation* EnsureVehicleSimulation();
        void UpdateWheelTransforms();
        void UpdateTireDeformation(int wheel_index, bool grounded, TireDeformationBatch* batch = nullptr);
        void UpdateCheapWheelTransforms();
        void CaptureCheapWheelRestPoses();

        void BuildChassisConvexShapes(Entity* chassis_entity, const std::vector<Entity*>& entities_to_exclude);
    };
}
