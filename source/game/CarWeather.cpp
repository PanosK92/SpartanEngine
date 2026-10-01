/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#ifdef SP_GAME
#include "../car/CarPhysics.h"
#endif
#include "CarWeather.h"
#include "../car/CarRain.h"
#include "../car/Car.h"
#include "../world/Weather.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Physics.h"
using namespace std;
using namespace spartan::math;
namespace spartan::game
{
    namespace
    {
        Entity* drop_vehicle       = nullptr;
        Entity* drop_wetness_owner = nullptr;
        float drop_wetness         = 0.0f;
        Quaternion drop_rotation   = Quaternion::Identity;
        Vector3 drop_velocity_last = Vector3::Zero; // world, m/s
        Vector3 drop_acceleration  = Vector3::Zero; // world, m/s^2
        constexpr float acceleration_smoothing = 0.03f;
    }
    void TickCarWeather(float delta_time)
    {
        delta_time = clamp(delta_time, 0.0f, 0.1f);
        const float wetness = Weather::GetWetness();
        const float rain = Weather::GetRain();
        // the water on the car lives on the car's clock, it holds still while the game is paused
        if (Engine::IsFlagSet(EngineMode::Paused))
        {
            delta_time = 0.0f;
        }

        drop_vehicle = nullptr;
        if (wetness <= 0.0f && drop_wetness <= 0.0f)
        {
            CarRain::Tick(nullptr, CarRain::Conditions(), delta_time);
            return;
        }

        // the water stays on the car the player last drove, stepping out does not dry it
        Car* occupied = nullptr;
        Car* previous = nullptr;
        for (Car* car : Car::GetAll())
        {
            if (!car)
                continue;

            if (car->IsViewed())
            {
                occupied = car;
                break;
            }

            if (drop_wetness_owner && car->GetRootEntity() == drop_wetness_owner)
            {
                previous = car;
            }
        }
        if (!occupied)
        {
            occupied = previous;
        }
        Entity* root                = occupied ? occupied->GetRootEntity() : nullptr;
        Physics* physics            = root ? root->GetComponent<Physics>() : nullptr;
        car::Simulation* simulation = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
        if (!simulation)
        {
            CarRain::Tick(nullptr, CarRain::Conditions(), delta_time);
            return;
        }

        // the car carries its water, it stays wet driving under a roof and dries over minutes, faster in the airflow
        const Quaternion to_car       = root->GetRotation().Inverse();
        const Vector3 velocity_world  = physics->GetLinearVelocity();
        const Vector3 velocity        = to_car * velocity_world;
        const float airspeed          = velocity.Length();
        const float exposure          = Weather::GetExposure(root->GetPosition() + Vector3(0.0f, 1.5f, 0.0f));
        const float soak              = rain * exposure;
        if (root != drop_wetness_owner)
        {
            drop_wetness_owner = root;
            drop_wetness       = wetness * exposure;
            drop_velocity_last = velocity_world;
            drop_acceleration  = Vector3::Zero;
        }

        // measured from the body's own velocity, so braking, cornering, bumps and crashes all reach the water
        if (delta_time > 0.0f)
        {
            const Vector3 acceleration = (velocity_world - drop_velocity_last) / delta_time;
            drop_acceleration          = Vector3::Lerp(drop_acceleration, acceleration, min(1.0f, delta_time / acceleration_smoothing));
            drop_velocity_last         = velocity_world;
        }

        if (soak > 0.0f)
        {
            drop_wetness = min(1.0f, drop_wetness + delta_time * (0.02f + 0.13f * rain) * soak);
        }
        else
        {
            drop_wetness = max(0.0f, drop_wetness - delta_time * (1.0f + airspeed / 15.0f) / 150.0f);
        }

        drop_vehicle  = root;
        drop_rotation = root->GetRotation();

        CarRain::Conditions conditions;
        conditions.velocity     = velocity;
        conditions.acceleration = to_car * drop_acceleration;
        conditions.gravity      = to_car * Vector3(0.0f, -9.81f, 0.0f);
        conditions.wind         = to_car * World::GetWind();
        conditions.rain         = rain;
        conditions.exposure     = exposure;
        CarRain::Tick(root, conditions, delta_time);
    }

    void ClearCarWeather()
    {
        drop_vehicle = nullptr;
        drop_wetness_owner = nullptr;
        drop_wetness = 0.0f;
        drop_rotation = Quaternion::Identity;
        drop_velocity_last = drop_acceleration = Vector3::Zero;
        CarRain::Clear();
    }

    void FillCarWeather(SurfaceWater& water)
    {
        if (drop_vehicle)
        {
            water.entity_id = drop_vehicle->GetObjectId();
            water.origin = drop_vehicle->GetPosition();
            water.wetness = drop_wetness;
        }
        for (uint32_t i = 0; i < 3; ++i)
            water.axes[i] = drop_rotation * (i == 0 ? Vector3::Right : (i == 1 ? Vector3::Up : Vector3::Forward));
    }
}
