/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "CarPhysics.h"
#include "CarWeather.h"
#include "CarRain.h"
#include "CarDirt.h"
#include "CarLens.h"
#include "Car.h"
#include "../world/Weather.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Physics.h"
#include "../world/components/Camera.h"
#include "../rendering/Renderer.h"
#include "../commands/console/ConsoleCommands.h"
using namespace std;
using namespace spartan::math;
namespace spartan
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

        TConsoleVar<float> cvar_headwind("car.dirt_airspeed", 0.0f, "m/s of headwind on the car, to watch the air scour the dirt and push the rain over the paint without driving");

        // the camera the lens belongs to, its motion measured from its own path so the swing through a corner reaches the drops
        bool lens_tracking         = false;
        Vector3 lens_position_last = Vector3::Zero;
        Vector3 lens_velocity      = Vector3::Zero; // world, m/s
        Vector3 lens_acceleration  = Vector3::Zero; // world, m/s^2
        constexpr float lens_speed_cut = 150.0f;    // m/s, faster than this the camera was cut, not moved

        void tick_lens(Car* viewed, float delta_time)
        {
            Camera* camera      = World::GetCamera();
            const CarView view  = viewed ? viewed->GetCurrentView() : CarView::Wheel;
            const bool outside  = viewed && viewed->IsViewed() && (view == CarView::Chase || view == CarView::Hood);
            const float amount  = cvar_lens_rain.GetValue();
            if (!camera || !outside || amount <= 0.0f)
            {
                CarLens::Clear();
                lens_tracking = false;
                return;
            }

            Entity* entity         = camera->GetEntity();
            const Vector3 position = entity->GetPosition();
            if (!lens_tracking)
            {
                lens_position_last = position;
                lens_velocity      = viewed->GetRootEntity()->GetComponent<Physics>() ? viewed->GetRootEntity()->GetComponent<Physics>()->GetLinearVelocity() : Vector3::Zero;
                lens_acceleration  = Vector3::Zero;
                lens_tracking      = true;
            }

            if (delta_time > 0.0f)
            {
                const Vector3 velocity = (position - lens_position_last) / delta_time;
                if (velocity.Length() < lens_speed_cut)
                {
                    const Vector3 smoothed = Vector3::Lerp(lens_velocity, velocity, min(1.0f, delta_time / 0.05f));
                    lens_acceleration      = Vector3::Lerp(lens_acceleration, (smoothed - lens_velocity) / delta_time, min(1.0f, delta_time / acceleration_smoothing));
                    lens_velocity          = smoothed;
                }
                lens_position_last = position;
            }

            const Quaternion to_lens = entity->GetRotation().Inverse();
            CarRain::Conditions conditions;
            conditions.velocity     = to_lens * lens_velocity;
            conditions.acceleration = to_lens * lens_acceleration;
            conditions.gravity      = to_lens * Vector3(0.0f, -9.81f, 0.0f);
            conditions.wind         = to_lens * World::GetWind();
            conditions.rain         = Weather::GetRain();
            conditions.exposure     = Weather::GetExposure(position);

            // a chase camera rides in the wake of the car it follows, through the mist its tyres throw off the wet road
            float spray          = 0.0f;
            Entity* root         = viewed->GetRootEntity();
            Physics* physics     = root ? root->GetComponent<Physics>() : nullptr;
            const Vector3 behind = position - (root ? root->GetPosition() : position);
            if (view == CarView::Chase && physics && behind.Dot(root->GetForward()) < 0.0f)
            {
                spray = water_drops::spray_flux(Weather::GetWetness(), physics->GetLinearVelocity().Length(), behind.Length(), 4);
            }
            CarLens::Tick(conditions, spray, camera->GetAspectRatio(), amount, delta_time);
        }
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

        // the water and the dirt stay on the car the player last drove, stepping out does not dry or wash it
        // the atlas is kept baked in dry weather too, the dirt lives in its texels
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
        tick_lens(occupied, delta_time);

        Entity* root                = occupied ? occupied->GetRootEntity() : nullptr;
        Physics* physics            = root ? root->GetComponent<Physics>() : nullptr;
        car::Simulation* simulation = physics ? CarPhysics::Get(*physics).GetVehicleSimulation() : nullptr;
        if (!simulation)
        {
            CarRain::Tick(nullptr, CarRain::Conditions(), delta_time);
            CarDirt::Tick(delta_time, CarRain::Conditions());
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
        conditions.wind         = to_car * World::GetWind() - Vector3::Forward * cvar_headwind.GetValue();
        conditions.rain         = rain;
        conditions.exposure     = exposure;
        CarRain::Tick(root, conditions, delta_time);
        CarDirt::Tick(delta_time, conditions);
    }

    void ClearCarWeather()
    {
        drop_vehicle = nullptr;
        drop_wetness_owner = nullptr;
        drop_wetness = 0.0f;
        drop_rotation = Quaternion::Identity;
        drop_velocity_last = drop_acceleration = Vector3::Zero;
        lens_tracking = false;
        CarLens::Clear();
        CarRain::Clear();
        CarDirt::Release();
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
