// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
#pragma once
#include "../car/Car.h"
#include "../core/Engine.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Camera.h"
#include "../world/ParticleWake.h"
#include <algorithm>
#include <unordered_map>

namespace spartan::vehicle_particle_wake
{
    using namespace math;
    struct Trail
    {
        Vector3 previous = Vector3::Zero;
        float clock = 0;
        bool observed = false;
        std::array<particle_wake::Source, 6> samples = {};
    };
    inline std::unordered_map<uint64_t, Trail> trails;

    inline void Clear()
    {
        trails.clear();
        particle_wake::sources = {};
    }

    inline void Tick(float dt)
    {
        if (Engine::IsFlagSet(EngineMode::Paused)) return;
        if (!World::GetCamera()) { Clear(); return; }
        if (dt <= 0) return;
        const Vector3 viewer = World::GetCamera()->GetEntity()->GetPosition();
        auto cars = Car::GetAll();
        cars.erase(std::remove_if(cars.begin(), cars.end(), [&](Car* car) {
            Entity* root = car ? car->GetRootEntity() : nullptr;
            return !root || !root->GetActive() ||
                (root->GetPosition() - viewer).LengthSquared() > 120.0f * 120.0f;
        }), cars.end());
        std::sort(cars.begin(), cars.end(), [&](Car* a, Car* b) {
            const float da = a == Car::GetViewed() ? -1.0f : (a->GetRootEntity()->GetPosition() - viewer).LengthSquared();
            const float db = b == Car::GetViewed() ? -1.0f : (b->GetRootEntity()->GetPosition() - viewer).LengthSquared();
            return da < db;
        });
        // Track every nearby car, including spline traffic whose physics body
        // is asleep. Age detached trails after cars stop or leave the area.
        for (auto& [id, trail] : trails)
        {
            trail.observed = false;
            for (auto& sample : trail.samples)
            {
                sample.shape.z += dt;
                if (sample.shape.z >= 1.8f) sample.center_strength.w = 0;
            }
        }
        for (Car* car : cars)
        {
            Entity* root = car->GetRootEntity();
            const Vector3 position = root->GetPosition();
            auto [entry, inserted] = trails.try_emplace(root->GetObjectId());
            Trail* trail = &entry->second;
            trail->observed = true;
            const Vector3 displacement = position - trail->previous;
            trail->previous = position;
            // Use the motion actually visible in the world, regardless of the
            // car's physics LOD. First observations and teleports have no flow.
            if (inserted || displacement.Length() > std::max(10.0f, 100.0f * dt))
            {
                trail->samples = {};
                trail->clock = 0.3f;
                continue;
            }
            Vector3 velocity = displacement / dt;
            velocity.y = 0;
            const float speed = velocity.Length();
            if (speed < 2.0f) continue;
            trail->clock += dt;
            if (trail->clock < 0.3f) continue;
            const Vector3 axis = velocity / speed;
            const float half_length = std::max(3.5f, speed * std::min(trail->clock, 0.5f) * 0.65f);
            trail->clock = 0;
            for (size_t i = trail->samples.size() - 1; i > 0; --i) trail->samples[i] = trail->samples[i - 1];
            auto& sample = trail->samples[0];
            const Vector3 center = position - axis * (2.0f + half_length * 0.5f) + Vector3(0, 0.35f, 0);
            const float strength = std::min(speed * 0.48f, 16.0f) * (car == Car::GetViewed() ? 1.2f : 1.0f);
            sample.center_strength = Vector4(center.x, center.y, center.z, strength);
            sample.axis_length = Vector4(axis.x, axis.y, axis.z, half_length);
            sample.shape = Vector4(1.15f, 1.65f, 0, 0);
        }
        std::vector<Trail*> ordered;
        for (Car* car : cars) ordered.push_back(&trails.at(car->GetRootEntity()->GetObjectId()));
        for (auto it = trails.begin(); it != trails.end();)
        {
            Trail& trail = it->second;
            if (trail.observed) { ++it; continue; }
            const bool alive = std::any_of(trail.samples.begin(), trail.samples.end(),
                [](const auto& sample) { return sample.center_strength.w > 0; });
            if (!alive) it = trails.erase(it);
            else { ordered.push_back(&trail); ++it; }
        }
        // Give each car its newest sample before spending space on older trail
        // segments. A distant car can no longer monopolize a fixed wake slot.
        particle_wake::sources = {};
        uint32_t index = 0;
        for (size_t age = 0; age < 6; ++age)
            for (const Trail* trail : ordered)
                if (trail->samples[age].center_strength.w > 0 && index < particle_wake::capacity)
                    particle_wake::sources[index++] = trail->samples[age];
    }
}
