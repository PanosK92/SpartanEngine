/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ===================
#include "pch.h"
#include <sol/sol.hpp>
#include "ScriptingInternal.h"
#include "visual/VisualApiReflection.h"
#include "../car/Car.h"
#include "../car/CarPhysics.h"
#include "../car/CarSimulation.h"
#include "../world/Entity.h"
//==============================

namespace spartan::scripting
{
    namespace
    {
        template<typename R, typename... Args>
        auto vehicle_method(R (CarPhysics::*method)(Args...))
        {
            return [method](Physics& physics, Args... args) -> R { return (CarPhysics::Get(physics).*method)(args...); };
        }
        template<typename R, typename... Args>
        auto vehicle_method(R (CarPhysics::*method)(Args...) const)
        {
            return [method](Physics& physics, Args... args) -> R { return (CarPhysics::Get(physics).*method)(args...); };
        }
    }

    void register_car_physics(sol::state_view State)
    {
        // Extend the enum's backing table, preserving its read-only Lua proxy.
        sol::table body_types = State["BodyType"];
        sol::table values = body_types[sol::metatable_key][sol::meta_function::index];
        values["Vehicle"] = BodyType::Custom;
        visual::detail::add_enum_value("BodyType", "Vehicle", static_cast<int64_t>(BodyType::Custom));
        visual::enumeration<WheelIndex>(State, "WheelIndex", {
            {"FrontLeft",    WheelIndex::FrontLeft},
            {"FrontRight",   WheelIndex::FrontRight},
            {"RearLeft",     WheelIndex::RearLeft},
            {"RearRight",    WheelIndex::RearRight},
            {"Count",        WheelIndex::Count}});


        visual::usertype_extension<Physics> type(State, "Physics");
        type["SetVehicleThrottle"] = vehicle_method(&CarPhysics::SetVehicleThrottle);
        type["SetVehicleBrake"] = vehicle_method(&CarPhysics::SetVehicleBrake);
        type["SetVehicleSteering"] = vehicle_method(&CarPhysics::SetVehicleSteering);
        type["SetVehicleHandbrake"] = vehicle_method(&CarPhysics::SetVehicleHandbrake);
        type["SetWheelEntity"] = vehicle_method(&CarPhysics::SetWheelEntity);
        type["GetWheelEntity"] = vehicle_method(&CarPhysics::GetWheelEntity);
        type["SetChassisEntity"] = vehicle_method(&CarPhysics::SetChassisEntity);
        type["GetChassisEntity"] = vehicle_method(&CarPhysics::GetChassisEntity);
        type["SetWheelRadius"] = vehicle_method(&CarPhysics::SetWheelRadius);
        type["GetWheelRadius"] = vehicle_method(&CarPhysics::GetWheelRadius);
        type["GetSuspensionHeight"] = vehicle_method(&CarPhysics::GetSuspensionHeight);
        type["GetVehicleThrottle"] = vehicle_method(&CarPhysics::GetVehicleThrottle);
        type["GetVehicleBrake"] = vehicle_method(&CarPhysics::GetVehicleBrake);
        type["GetVehicleHeadlights"] = [](Physics& self) { return CarPhysics::Get(self).GetCar() ? static_cast<int>(CarPhysics::Get(self).GetCar()->GetHeadlights()) : 0; };
        type["SetVehicleHeadlights"] = [](Physics& self, int mode) { if (CarPhysics::Get(self).GetCar()) CarPhysics::Get(self).GetCar()->SetHeadlights(static_cast<CarHeadlights>(std::clamp(mode, 0, 2))); };
        type["GetVehicleRearLamps"] = [](Physics& self) { return CarPhysics::Get(self).GetCar() ? CarPhysics::Get(self).GetCar()->GetRearLamps() : false; };
        type["GetVehicleHeadlightLevel"] = [](Physics& self) { return CarPhysics::Get(self).GetCar() ? CarPhysics::Get(self).GetCar()->GetHeadlightLevel() : 0.0f; };
        type["GetVehicleRearLampLevel"] = [](Physics& self) { return CarPhysics::Get(self).GetCar() ? CarPhysics::Get(self).GetCar()->GetRearLampLevel() : 0.0f; };
        type["GetVehicleStopLamp"] = [](Physics& self) { return CarPhysics::Get(self).GetCar() ? CarPhysics::Get(self).GetCar()->GetStopLamp() : 0.0f; };
        type["GetVehicleSteering"] = vehicle_method(&CarPhysics::GetVehicleSteering);
        // the measured counterparts of the inputs: where the rack really is (-1 to 1) and how engaged the clutch is (0 to 1)
        type["GetVehicleSteeringRack"] = [](Physics& self) { ::car::Simulation* simulation = CarPhysics::Get(self).GetVehicleSimulation(); return simulation ? simulation->get_steering_rack() : 0.0f; };
        type["GetVehicleClutch"] = [](Physics& self) { ::car::Simulation* simulation = CarPhysics::Get(self).GetVehicleSimulation(); return simulation ? simulation->get_clutch() : 1.0f; };
        type["GetVehicleHandbrake"] = vehicle_method(&CarPhysics::GetVehicleHandbrake);
        type["IsWheelGrounded"] = vehicle_method(&CarPhysics::IsWheelGrounded);
        type["GetWheelCompression"] = vehicle_method(&CarPhysics::GetWheelCompression);
        type["GetWheelSuspensionForce"] = vehicle_method(&CarPhysics::GetWheelSuspensionForce);
        type["GetWheelContactPoint"] = vehicle_method(&CarPhysics::GetWheelContactPoint);
        type["GetWheelContactNormal"] = vehicle_method(&CarPhysics::GetWheelContactNormal);
        type["GetWheelWidth"] = vehicle_method(&CarPhysics::GetWheelWidth);
        type["GetWheelSlipAngle"] = vehicle_method(&CarPhysics::GetWheelSlipAngle);
        type["GetWheelSlipRatio"] = vehicle_method(&CarPhysics::GetWheelSlipRatio);
        type["GetWheelSlipMagnitude"] = vehicle_method(&CarPhysics::GetWheelSlipMagnitude);
        type["GetWheelTireLoad"] = vehicle_method(&CarPhysics::GetWheelTireLoad);
        type["GetWheelLateralForce"] = vehicle_method(&CarPhysics::GetWheelLateralForce);
        type["GetWheelLongitudinalForce"] = vehicle_method(&CarPhysics::GetWheelLongitudinalForce);
        type["GetWheelAngularVelocity"] = vehicle_method(&CarPhysics::GetWheelAngularVelocity);
        type["GetWheelRPM"] = vehicle_method(&CarPhysics::GetWheelRPM);
        type["GetWheelTemperature"] = vehicle_method(&CarPhysics::GetWheelTemperature);
        type["GetWheelTempGripFactor"] = vehicle_method(&CarPhysics::GetWheelTempGripFactor);
        type["GetWheelBrakeTemp"] = vehicle_method(&CarPhysics::GetWheelBrakeTemp);
        type["GetWheelBrakeEfficiency"] = vehicle_method(&CarPhysics::GetWheelBrakeEfficiency);
        type["SetAbsEnabled"] = vehicle_method(&CarPhysics::SetAbsEnabled);
        type["GetAbsEnabled"] = vehicle_method(&CarPhysics::GetAbsEnabled);
        type["IsAbsActive"] = vehicle_method(&CarPhysics::IsAbsActive);
        type["IsAbsActiveAny"] = vehicle_method(&CarPhysics::IsAbsActiveAny);
        type["SetTcEnabled"] = vehicle_method(&CarPhysics::SetTcEnabled);
        type["GetTcEnabled"] = vehicle_method(&CarPhysics::GetTcEnabled);
        type["IsTcActive"] = vehicle_method(&CarPhysics::IsTcActive);
        type["GetTcReduction"] = vehicle_method(&CarPhysics::GetTcReduction);
        type["IsBurnoutActive"] = vehicle_method(&CarPhysics::IsBurnoutActive);
        type["SetTurboEnabled"] = vehicle_method(&CarPhysics::SetTurboEnabled);
        type["GetTurboEnabled"] = vehicle_method(&CarPhysics::GetTurboEnabled);
        type["GetBoostPressure"] = vehicle_method(&CarPhysics::GetBoostPressure);
        type["GetBoostMaxPressure"] = vehicle_method(&CarPhysics::GetBoostMaxPressure);
        type["SetDrsEnabled"] = vehicle_method(&CarPhysics::SetDrsEnabled);
        type["GetDrsEnabled"] = vehicle_method(&CarPhysics::GetDrsEnabled);
        type["SetDrsActive"] = vehicle_method(&CarPhysics::SetDrsActive);
        type["GetDrsActive"] = vehicle_method(&CarPhysics::GetDrsActive);
        type["SetDiffType"] = vehicle_method(&CarPhysics::SetDiffType);
        type["GetDiffType"] = vehicle_method(&CarPhysics::GetDiffType);
        type["GetDiffTypeName"] = vehicle_method(&CarPhysics::GetDiffTypeName);
        type["GetWheelWear"] = vehicle_method(&CarPhysics::GetWheelWear);
        type["GetWheelWearGripFactor"] = vehicle_method(&CarPhysics::GetWheelWearGripFactor);
        type["ResetTireWear"] = vehicle_method(&CarPhysics::ResetTireWear);
        type["SetManualTransmission"] = vehicle_method(&CarPhysics::SetManualTransmission);
        type["GetManualTransmission"] = vehicle_method(&CarPhysics::GetManualTransmission);
        type["ShiftUp"] = vehicle_method(&CarPhysics::ShiftUp);
        type["ShiftDown"] = vehicle_method(&CarPhysics::ShiftDown);
        type["ShiftToNeutral"] = vehicle_method(&CarPhysics::ShiftToNeutral);
        type["GetCurrentGear"] = vehicle_method(&CarPhysics::GetCurrentGear);
        type["GetCurrentGearString"] = vehicle_method(&CarPhysics::GetCurrentGearString);
        type["GetEngineRPM"] = vehicle_method(&CarPhysics::GetEngineRPM);
        type["GetEngineTorque"] = vehicle_method(&CarPhysics::GetEngineTorque);
        type["GetMotorTorque"] = vehicle_method(&CarPhysics::GetMotorTorque);
        type["GetIdleRPM"] = vehicle_method(&CarPhysics::GetIdleRPM);
        type["GetRedlineRPM"] = vehicle_method(&CarPhysics::GetRedlineRPM);
        type["IsShifting"] = vehicle_method(&CarPhysics::IsShifting);
        type["SyncWheelOffsetsFromEntities"] = vehicle_method(&CarPhysics::SyncWheelOffsetsFromEntities);
    }
}
