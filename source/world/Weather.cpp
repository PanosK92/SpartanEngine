/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ============================
#include "pch.h"
#include "Weather.h"
#include "CarRain.h"
#include "World.h"
#include "Entity.h"
#include "RainSoundSynthesis.h"
#include "components/Light.h"
#include "components/Camera.h"
#include "components/AudioSource.h"
#include "components/ParticleSystem.h"
#include "components/Physics.h"
#include "../car/Car.h"
#include "../car/CarSimulation.h"
#include "../physics/PhysicsWorld.h"
#include "../profiling/Profiler.h"
#include "../core/Engine.h"
#include <array>
#include <vector>
#include <mutex>
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace
    {
        constexpr uint32_t resolution   = Weather::occlusion_resolution;
        constexpr uint32_t cell_count   = resolution * resolution;
        constexpr float cell_size       = Weather::occlusion_cell_size;
        constexpr float height_exposed  = -1.0e5f; // nothing above, open sky
        constexpr uint32_t ray_budget   = 512;     // per frame for cells that just scrolled into the window
        constexpr uint32_t ray_refresh  = 96;      // per frame for re-tracing known cells, a full pass takes cell_count / ray_refresh frames
        constexpr float ray_headroom    = 150.0f;  // rays start this far above the camera so roofs over it are found
        constexpr float ray_length      = 600.0f;

        // particles, the drops are born in a slab above and around the camera and fall at terminal speed
        constexpr float drops_per_second   = 38000.0f;
        constexpr uint32_t drops_max       = 70000; // the shared particle pool holds 100000, the rest stays for smoke and sparks
        constexpr float drops_lifetime     = 2.2f;
        constexpr float drops_radius       = 18.0f;
        constexpr float drops_height       = 7.0f;  // slab centre above the camera
        constexpr float drops_lead_seconds = 0.8f;  // born where the camera is heading so a moving camera still drives into rain

        struct occlusion_grid
        {
            array<float, cell_count> heights;
            array<int32_t, cell_count> cell_x;
            array<int32_t, cell_count> cell_z;
            array<uint8_t, cell_count> traced;
            int32_t origin_x        = 0;
            int32_t origin_z        = 0;
            uint32_t refresh_cursor = 0;
            bool initialized        = false;
        };

        occlusion_grid grid;
        vector<pair<float, uint32_t>> pending;

        float rain          = 0.0f;
        float wetness       = 0.0f;
        float puddles       = 0.0f;
        float shelter       = 0.0f;
        Light* last_light   = nullptr;
        Vector3 camera_last = Vector3::Zero;
        Vector3 camera_velocity = Vector3::Zero;
        bool camera_has_last = false;
        uint64_t rain_entity_id  = 0;
        uint64_t sound_entity_id = 0;
        once_flag sound_once;

        // the occupied car, its water is simulated drop by drop in CarRain
        constexpr float acceleration_smoothing = 0.03f; // seconds, keeps suspension chatter out while a crash still lands in full
        Entity* drop_vehicle       = nullptr;
        Entity* drop_wetness_owner = nullptr;
        float drop_wetness         = 0.0f;
        Quaternion drop_rotation   = Quaternion::Identity;
        Vector3 drop_velocity_last = Vector3::Zero; // world, m/s
        Vector3 drop_acceleration  = Vector3::Zero; // world, m/s^2

        int32_t wrap(int32_t value)
        {
            return value & static_cast<int32_t>(resolution - 1);
        }

        void grid_reset()
        {
            grid.heights.fill(height_exposed);
            grid.cell_x.fill(INT32_MIN);
            grid.cell_z.fill(INT32_MIN);
            grid.traced.fill(0);
            grid.refresh_cursor = 0;
            grid.initialized    = true;
        }

        void grid_trace(uint32_t slot, float top_y)
        {
            Vector3 origin((grid.cell_x[slot] + 0.5f) * cell_size, top_y, (grid.cell_z[slot] + 0.5f) * cell_size);
            PhysicsRaycastHit hit;
            grid.heights[slot] = PhysicsWorld::RaycastStatic(origin, Vector3::Down, ray_length, hit) ? hit.position.y : height_exposed;
            grid.traced[slot]  = 1;
        }

        // 1 under open sky, 0 below the topmost static surface, mirrors rain_exposure_at() in common_rain.hlsl for flat ground
        float grid_exposure(const Vector3& position)
        {
            if (!grid.initialized)
                return 1.0f;

            const int32_t cell_x = static_cast<int32_t>(floorf(position.x / cell_size));
            const int32_t cell_z = static_cast<int32_t>(floorf(position.z / cell_size));
            const uint32_t slot  = static_cast<uint32_t>(wrap(cell_z)) * resolution + static_cast<uint32_t>(wrap(cell_x));
            if (grid.cell_x[slot] != cell_x || grid.cell_z[slot] != cell_z)
                return 1.0f;

            const float tolerance = 0.12f;
            return clamp((position.y - grid.heights[slot] + 2.0f * tolerance) / tolerance, 0.0f, 1.0f);
        }

        // c++ mirrors of road_hash(), road_noise() and puddle_basin() so physics finds water exactly where it is drawn
        float fract(float value)
        {
            return value - floorf(value);
        }

        float puddle_hash(float x, float y)
        {
            float qx = fract(x * 0.1031f);
            float qy = fract(y * 0.1030f);
            float qz = fract(x * 0.0973f);
            const float d = qx * (qy + 33.33f) + qy * (qz + 33.33f) + qz * (qx + 33.33f);
            qx += d;
            qy += d;
            qz += d;
            return fract((qx + qy) * qz);
        }

        float puddle_noise(float x, float y)
        {
            const float ix = floorf(x);
            const float iy = floorf(y);
            float fx = x - ix;
            float fy = y - iy;
            fx = fx * fx * (3.0f - 2.0f * fx);
            fy = fy * fy * (3.0f - 2.0f * fy);
            const float bottom = std::lerp(puddle_hash(ix, iy), puddle_hash(ix + 1.0f, iy), fx);
            const float top    = std::lerp(puddle_hash(ix, iy + 1.0f), puddle_hash(ix + 1.0f, iy + 1.0f), fx);
            return std::lerp(bottom, top, fy);
        }

        float puddle_basin(float x, float z)
        {
            return puddle_noise(x * 0.12f, z * 0.12f) * 0.5f +
                   puddle_noise(x * 0.4f + 13.7f, z * 0.4f + 5.3f) * 0.3f +
                   puddle_noise(x * 1.1f + 41.3f, z * 1.1f + 27.1f) * 0.2f;
        }

        void grid_tick(const Vector3& camera_position)
        {
            SP_PROFILE_CPU();

            if (!grid.initialized)
            {
                grid_reset();
            }

            grid.origin_x = static_cast<int32_t>(floorf(camera_position.x / cell_size)) - static_cast<int32_t>(resolution / 2);
            grid.origin_z = static_cast<int32_t>(floorf(camera_position.z / cell_size)) - static_cast<int32_t>(resolution / 2);

            // a slot holds whichever world cell of the window maps onto it, cells that just scrolled in start exposed
            pending.clear();
            for (uint32_t sz = 0; sz < resolution; sz++)
            {
                const int32_t cell_z = grid.origin_z + wrap(static_cast<int32_t>(sz) - grid.origin_z);
                for (uint32_t sx = 0; sx < resolution; sx++)
                {
                    const int32_t cell_x = grid.origin_x + wrap(static_cast<int32_t>(sx) - grid.origin_x);
                    const uint32_t slot  = sz * resolution + sx;
                    if (grid.cell_x[slot] != cell_x || grid.cell_z[slot] != cell_z)
                    {
                        grid.cell_x[slot]  = cell_x;
                        grid.cell_z[slot]  = cell_z;
                        grid.heights[slot] = height_exposed;
                        grid.traced[slot]  = 0;
                    }

                    if (!grid.traced[slot])
                    {
                        const float dx = (cell_x + 0.5f) * cell_size - camera_position.x;
                        const float dz = (cell_z + 0.5f) * cell_size - camera_position.z;
                        pending.emplace_back(dx * dx + dz * dz, slot);
                    }
                }
            }

            // new cells nearest the camera first, what is right in front of you matters most
            const float top_y = camera_position.y + ray_headroom;
            uint32_t budget   = ray_budget;
            if (!pending.empty())
            {
                const size_t count = min<size_t>(pending.size(), budget);
                if (count < pending.size())
                {
                    nth_element(pending.begin(), pending.begin() + count, pending.end());
                }
                for (size_t i = 0; i < count; i++)
                {
                    grid_trace(pending[i].second, top_y);
                }
                budget -= static_cast<uint32_t>(count);
            }

            // what is left refreshes the window round robin so streamed in or moved geometry is picked up
            budget = min(budget, ray_refresh);
            for (uint32_t i = 0; i < budget; i++)
            {
                grid_trace(grid.refresh_cursor, top_y);
                grid.refresh_cursor = (grid.refresh_cursor + 1) % cell_count;
            }
        }

        ParticleSystem* rain_particles(bool create)
        {
            Entity* entity = rain_entity_id ? World::GetEntityById(rain_entity_id) : nullptr;
            if (!entity && create)
            {
                entity = World::CreateEntity();
                entity->SetObjectName("weather_rain");
                entity->SetTransient(true);
                rain_entity_id = entity->GetObjectId();

                ParticleSystem* particles = entity->AddComponent<ParticleSystem>();
                particles->ApplyPreset(ParticlePreset::Rain);
                particles->SetMaxParticles(drops_max);
                particles->SetLifetime(drops_lifetime);
                particles->SetStartSpeed(6.0f);
                particles->SetGravityModifier(-1.0f);
                particles->SetDrag(1.1f); // terminal speed of g / drag, about 9 m/s, what a 2 mm drop falls at
                particles->SetEmissionRadius(drops_radius);
                particles->SetEmissionDirection(Vector3::Down);
                particles->SetEmissionConeAngle(0.04f);
                particles->SetDirectionalBlend(1.0f);
                particles->SetStartSize(0.014f);
                particles->SetEndSize(0.014f);
                particles->SetStartColor(Color(0.72f, 0.76f, 0.82f, 0.22f));
                particles->SetEndColor(Color(0.72f, 0.76f, 0.82f, 0.18f));
                particles->SetVelocityStretch(5.0f);
                particles->SetWindInfluence(0.6f);
                particles->SetTurbulenceStrength(0.0f);
                particles->SetBlendMode(ParticleBlendMode::Alpha);
                particles->SetLightingMode(ParticleLightingMode::Lit);
                particles->SetRainOccluded(true);
            }

            return entity ? entity->GetComponent<ParticleSystem>() : nullptr;
        }

        AudioSource* rain_audio(bool create)
        {
            Entity* entity = sound_entity_id ? World::GetEntityById(sound_entity_id) : nullptr;
            if (!entity && create)
            {
                entity = World::CreateEntity();
                entity->SetObjectName("weather_rain_sound");
                entity->SetTransient(true);
                sound_entity_id = entity->GetObjectId();

                AudioSource* audio = entity->AddComponent<AudioSource>();
                audio->SetIs3d(false);
                audio->SetLoop(true);
                audio->SetPlayOnStart(false);
                audio->SetVolume(0.0f);
            }

            return entity ? entity->GetComponent<AudioSource>() : nullptr;
        }

        void tick_particles(const Vector3& camera_position)
        {
            ParticleSystem* particles = rain_particles(rain > 0.0f);
            if (!particles)
                return;

            Entity* entity = particles->GetEntity();
            if (rain <= 0.0f)
            {
                // stop emitting and let the drops already in the air land
                particles->SetEmissionRate(0.0f);
                if (!particles->HasLiveParticles() && entity->GetActive())
                {
                    entity->SetActive(false);
                }
                return;
            }

            if (!entity->GetActive())
            {
                entity->SetActive(true);
            }

            // drizzle is sparse fine drops, a downpour is dense and fat
            particles->SetEmissionRate(drops_per_second * rain * (0.35f + 0.65f * rain));
            float size = 0.004f + 0.003f * rain;
            particles->SetStartSize(size);
            particles->SetEndSize(size);

            Vector3 lead = camera_velocity * drops_lead_seconds;
            lead.y       = 0.0f;
            entity->SetPosition(camera_position + lead + Vector3(0.0f, drops_height, 0.0f));
        }

        void tick_vehicle_drops(float delta_time)
        {
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

                if (car->IsOccupied())
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
            car::Simulation* simulation = physics ? physics->GetVehicleSimulation() : nullptr;
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
            const float exposure          = grid_exposure(root->GetPosition() + Vector3(0.0f, 1.5f, 0.0f));
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

        void tick_audio()
        {
            AudioSource* audio = rain_audio(rain > 0.0f || wetness > 0.0f);
            if (!audio)
                return;

            rain_sound::synthesizer& synth = rain_sound::get_synthesizer();
            call_once(sound_once, [&synth]()
            {
                synth.initialize(AudioSource::GetDeviceSampleRate());
            });

            if (!audio->IsSynthesisMode())
            {
                audio->SetSynthesisMode(true, [](float* buffer, int frames)
                {
                    rain_sound::get_synthesizer().generate(buffer, frames);
                });
            }

            synth.set_parameters(rain, shelter);

            if (rain > 0.0f)
            {
                audio->SetVolume(0.5f);
                if (!audio->IsPlaying())
                {
                    audio->StartSynthesis();
                }
            }
            else if (audio->IsPlaying())
            {
                audio->StopSynthesis();
            }
        }
    }

    void Weather::Tick(float delta_time)
    {
        SP_PROFILE_CPU();

        delta_time = clamp(delta_time, 0.0f, 0.1f);
        Light* light = World::GetDirectionalLight();
        rain         = light ? light->GetRain() : 0.0f;

        // a freshly loaded world starts in the state its weather would have settled into
        if (light != last_light)
        {
            last_light      = light;
            wetness         = rain > 0.0f ? 1.0f : 0.0f;
            puddles         = rain * 0.75f;
            camera_has_last = false;
            grid.initialized = false;
            drop_wetness_owner = nullptr;
            drop_wetness       = 0.0f;
            CarRain::Clear();
        }

        // soaking takes seconds in a downpour, drying takes minutes
        if (rain > 0.0f)
        {
            wetness = min(1.0f, wetness + delta_time * (0.02f + 0.13f * rain));
        }
        else
        {
            wetness = max(0.0f, wetness - delta_time / 100.0f);
        }

        // puddles fill over a minute of steady rain and drain slower than they fill
        const float puddles_target = rain * 0.75f;
        if (puddles < puddles_target)
        {
            puddles = min(puddles_target, puddles + delta_time * rain / 60.0f);
        }
        else
        {
            puddles = max(puddles_target, puddles - delta_time / 240.0f);
        }

        tick_vehicle_drops(delta_time);

        Camera* camera = World::GetCamera();
        if (!camera)
            return;

        const Vector3 camera_position = camera->GetEntity()->GetPosition();
        if (camera_has_last && delta_time > 0.0f)
        {
            Vector3 velocity = (camera_position - camera_last) / delta_time;
            // a teleport is not a velocity
            if (velocity.Length() > 150.0f)
            {
                velocity = Vector3::Zero;
            }
            camera_velocity = Vector3::Lerp(camera_velocity, velocity, min(1.0f, delta_time * 4.0f));
        }
        camera_last     = camera_position;
        camera_has_last = true;

        if (rain > 0.0f || wetness > 0.0f)
        {
            grid_tick(camera_position);

            PhysicsRaycastHit hit;
            const float sheltered = PhysicsWorld::RaycastStatic(camera_position, Vector3::Up, 80.0f, hit) ? 1.0f : 0.0f;
            shelter += (sheltered - shelter) * min(1.0f, delta_time * 3.0f);
        }

        tick_particles(camera_position);
        tick_audio();
    }

    float Weather::GetRain()
    {
        return rain;
    }

    float Weather::GetWetness()
    {
        return wetness;
    }

    float Weather::GetPuddliness()
    {
        return max(World::GetPuddliness(), puddles);
    }

    float Weather::GetRainPuddliness()
    {
        return puddles;
    }

    float Weather::GetShelter()
    {
        return shelter;
    }

    float Weather::GetWaterDepth(const Vector3& position, bool porous)
    {
        const bool weathered = rain > 0.0f || wetness > 0.0f || puddles > 0.0f;
        const float exposure = weathered ? grid_exposure(position) : 0.0f;

        // the sheet a downpour keeps running across sealed ground, about a millimetre at full rain, soil drinks most of it
        const float film = wetness * exposure * (0.00008f + 0.0009f * rain) * (porous ? 0.3f : 1.0f);

        // same level, basin and ragged shoreline as puddle_apply(), 0.08 height units read as a centimetre of water
        const float puddliness = max(World::GetPuddliness(), puddles * exposure * (porous ? 0.55f : 1.0f));
        float standing = 0.0f;
        if (puddliness > 0.0f)
        {
            const float ragged = puddle_noise(position.x * 4.3f + 7.1f, position.z * 4.3f + 3.9f) * 0.05f;
            const float height = puddle_basin(position.x, position.z) + ragged + (porous ? 0.035f : 0.0f);
            const float level  = std::lerp(0.25f, 0.49f, puddliness);
            const float fade   = clamp(puddliness / 0.15f, 0.0f, 1.0f);
            standing           = max(0.0f, level - height) * 0.125f * fade * fade * (3.0f - 2.0f * fade);
        }

        return film + standing;
    }

    Entity* Weather::GetDropsVehicle()
    {
        return drop_vehicle;
    }

    float Weather::GetDropsWetness()
    {
        return drop_vehicle ? drop_wetness : 0.0f;
    }

    Vector3 Weather::GetDropsAxis(uint32_t plane)
    {
        return drop_rotation * (plane == 0 ? Vector3::Right : (plane == 1 ? Vector3::Up : Vector3::Forward));
    }

    const float* Weather::GetOcclusionHeights()
    {
        return grid.heights.data();
    }

    Vector2 Weather::GetOcclusionMin()
    {
        return Vector2(grid.origin_x * cell_size, grid.origin_z * cell_size);
    }
}
