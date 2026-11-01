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
#include "../car/Car.h"
#include "../car/CarPhysics.h"
#include "../car/CarPresets.h"
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

        // nil means the player's car, otherwise the car whose vehicle or body entity was passed
        Car* resolve_car(sol::optional<Entity*> entity)
        {
            if (!entity || !entity.value())
            {
                return Car::GetPlayerCar();
            }
            for (Car* car : Car::GetAll())
            {
                if (car && (car->GetRootEntity() == entity.value() || car->GetBodyEntity() == entity.value()))
                {
                    return car;
                }
            }
            return nullptr;
        }

        ::car::Simulation* resolve_simulation(sol::optional<Entity*> entity)
        {
            Car* car = resolve_car(entity);
            Entity* root = car ? car->GetRootEntity() : nullptr;
            Physics* physics = root ? root->GetComponent<Physics>() : nullptr;
            return physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
        }

        sol::table make_stats(sol::state_view lua, const ::car::Simulation& simulation, const ::car::car_preset& spec)
        {
            float power_kw, power_rpm, torque_nm, torque_rpm;
            simulation.get_peak_output(power_kw, power_rpm, torque_nm, torque_rpm, &spec);

            sol::table stats = lua.create_table();
            stats["power_kw"]          = power_kw;
            stats["power_rpm"]         = power_rpm;
            stats["torque_nm"]         = torque_nm;
            stats["torque_rpm"]        = torque_rpm;
            stats["mass_kg"]           = spec.mass;
            stats["redline_rpm"]       = spec.engine_redline_rpm;
            stats["displacement_l"]    = spec.engine_displacement_l;
            stats["cylinders"]         = spec.engine_sound_cylinders;
            stats["boost_bar"]         = spec.turbo_enabled ? spec.boost_max_pressure : 0.0f;
            stats["forced_induction"]  = spec.turbo_enabled ? spec.forced_induction : -1;
            stats["drivetrain"]        = spec.drivetrain_type;
            stats["diff_type"]         = spec.diff_type;
            stats["gears"]             = spec.gear_count - 2;
            stats["final_drive"]       = spec.final_drive;
            stats["shift_time"]        = spec.shift_time;
            stats["engine_inertia"]    = spec.engine_inertia;
            stats["clutch_torque_nm"]  = spec.clutch_max_torque;
            stats["tire_friction"]     = spec.tire_friction;
            stats["brake_force"]       = spec.brake_force;
            stats["brake_bias_front"]  = spec.brake_bias_front;
            stats["spring_front_hz"]   = spec.front_spring_freq;
            stats["spring_rear_hz"]    = spec.rear_spring_freq;
            stats["lift_front"]        = spec.lift_coeff_front;
            stats["lift_rear"]         = spec.lift_coeff_rear;
            stats["drag"]              = spec.drag_coeff;
            stats["yaw_control"]       = spec.yaw_control_enabled;
            stats["electric"]          = spec.electric_enabled;
            return stats;
        }
    }

    void register_car_physics(sol::state_view State)
    {
        // Extend the enum's backing table, preserving its read-only Lua proxy.
        sol::table body_types = State["BodyType"];
        sol::table values = body_types[sol::metatable_key][sol::meta_function::index];
        values["Vehicle"] = BodyType::Custom;
        State.new_enum("WheelIndex",
            "FrontLeft",    WheelIndex::FrontLeft,
            "FrontRight",   WheelIndex::FrontRight,
            "RearLeft",     WheelIndex::RearLeft,
            "RearRight",    WheelIndex::RearRight,
            "Count",        WheelIndex::Count);


        sol::usertype<Physics> type = State["Physics"];
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
        type["GetVehicleSteering"] = vehicle_method(&CarPhysics::GetVehicleSteering);
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

    void register_car_tuning(sol::state_view state)
    {
        sol::table tuning = state.create_named_table("CarTuning");

        tuning["GetPlayerCar"] = []() -> Entity*
        {
            Car* car = Car::GetPlayerCar();
            return car ? car->GetRootEntity() : nullptr;
        };

        tuning["GetName"] = [](sol::optional<Entity*> entity) -> std::string
        {
            Car* car = resolve_car(entity);
            return car ? car->GetDisplayName() : std::string();
        };

        tuning["IsOccupied"] = [](sol::optional<Entity*> entity)
        {
            Car* car = resolve_car(entity);
            return car && car->IsOccupied();
        };

        // -1 for an unknown part or a car without a simulation
        tuning["GetStage"] = [](sol::optional<Entity*> entity, const std::string& part)
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            return simulation ? simulation->get_upgrade_stage(part) : -1;
        };

        tuning["GetStageMax"] = [](sol::optional<Entity*> entity, const std::string& part)
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            return simulation ? simulation->get_upgrade_stage_max(part) : 0;
        };

        // rebuilds the vehicle model with the part fitted, stage 0 takes it off
        tuning["SetStage"] = [](sol::optional<Entity*> entity, const std::string& part, int stage)
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            return simulation && simulation->set_upgrade_stage(part, stage);
        };

        tuning["GetSetting"] = [](sol::optional<Entity*> entity, const std::string& setting)
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            return simulation ? simulation->get_upgrade_setting(setting) : 0.0f;
        };

        tuning["SetSetting"] = [](sol::optional<Entity*> entity, const std::string& setting, float value)
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            return simulation && simulation->set_upgrade_setting(setting, value);
        };

        tuning["ResetAll"] = [](sol::optional<Entity*> entity)
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            if (simulation)
            {
                simulation->reset_upgrades();
            }
            return simulation != nullptr;
        };

        tuning["GetStats"] = [](sol::this_state s, sol::optional<Entity*> entity) -> sol::object
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            if (!simulation)
            {
                return sol::nil;
            }
            return make_stats(sol::state_view(s), *simulation, simulation->get_spec());
        };

        // the stats the car would have with one part at another stage, nothing is fitted
        tuning["PreviewStats"] = [](sol::this_state s, sol::optional<Entity*> entity, const std::string& part, int stage) -> sol::object
        {
            ::car::Simulation* simulation = resolve_simulation(entity);
            if (!simulation)
            {
                return sol::nil;
            }
            const ::car::car_preset spec = simulation->preview_upgrade_stage(part, stage);
            return make_stats(sol::state_view(s), *simulation, spec);
        };
    }
}
