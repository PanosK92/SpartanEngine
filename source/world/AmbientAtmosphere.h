// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
#pragma once
#include "WorldSystems.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/Weather.h"
#include "../world/components/Camera.h"
#include "../world/components/Terrain.h"
#include "../world/components/Render.h"
#include "../world/components/Volume.h"
#include "../world/components/ParticleSystem.h"
#include "../physics/PhysicsWorld.h"
#include <array>
#include <cmath>
#include <algorithm>

namespace spartan::ambient_atmosphere
{
    using namespace math;
    // Fixed emitter order/capacity keeps GPU slices stable. Recycling moves only
    // the source: existing particles retain their world position and contact plane.
    enum Kind { Leaves, Seeds, Fireflies, Litter, KindCount };
    struct Patch
    {
        Entity* entity = nullptr;
        ParticleSystem* particles = nullptr;
        Vector3 position = Vector3::Zero;
        float rate = 0, weight = 0, authored_rate = 0;
        bool placed = false;
        bool warm_start = false;
    };
    inline std::array<Patch, 32> patches;
    inline uint64_t root_id = 0;
    inline float sample_clock = 0, clock = 0;
    inline uint32_t sequence = 0;
    inline uint32_t shelter_frames = 0;
    inline Vector3 previous_viewer = Vector3::Zero;
    inline Vector3 previous_forward = Vector3::Forward;
    inline Vector3 motion = Vector3::Zero;
    inline uint32_t seed_visible_frames = 0;
    inline bool viewer_known = false;
    inline std::vector<BoundingBox> canopies, interiors;

    inline float Smooth(float lo, float hi, float value)
    {
        float t = std::clamp((value - lo) / (hi - lo), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    inline float Random()
    {
        uint32_t n = ++sequence * 747796405u + 2891336453u;
        n = ((n >> ((n >> 28u) + 4u)) ^ n) * 277803737u;
        return static_cast<float>((n ^ (n >> 22u)) & 0xffffffu) / 16777216.0f;
    }

    inline void Clear(bool remove_entities)
    {
        if (remove_entities)
            if (Entity* root = World::GetEntityById(root_id)) World::RemoveEntity(root);
        patches = {};
        root_id = 0;
        sample_clock = clock = 0;
        sequence = 0;
        shelter_frames = 0;
        motion = Vector3::Zero;
        seed_visible_frames = 0;
        viewer_known = false;
        canopies.clear();
        interiors.clear();
    }

    inline void SampleNeighborhood(const Vector3& viewer)
    {
        canopies.clear();
        interiors.clear();
        // Same actual canopy bounds and spatial groups used by the soundscape.
        // Reservoir sampling avoids favouring the first scatter tile in the world.
        uint32_t seen = 0;
        for (Entity* entity : World::GetEntities())
        {
            if (!entity->GetActive()) continue;
            if (Volume* volume = entity->GetComponent<Volume>())
                if (volume->GetReverbEnabled()) interiors.push_back(volume->GetBoundingBox() * entity->GetMatrix());
            if (!entity->HasTag("terrain_canopy")) continue;
            Render* render = entity->GetComponent<Render>();
            if (!render || !render->HasInstancing()) continue;
            render->UpdateAabb();
            if ((viewer - render->GetBoundingBox().GetClosestPoint(viewer)).LengthSquared() > 140 * 140) continue;
            for (const Render::InstanceBoundsGroup& group : render->GetInstanceBoundsGroups())
            {
                if ((viewer - group.bounds.GetClosestPoint(viewer)).LengthSquared() > 140 * 140) continue;
                // dense enough that a forest edge reads within a few metres, leaves and litter are gated on it
                const uint32_t stride = std::max(1u, group.count / 128u);
                for (uint32_t j = 0; j < group.count; j += stride)
                {
                    const BoundingBox& bounds = render->GetInstanceBounds(render->GetGroupedInstanceIndex(group.offset + j));
                    if ((viewer - bounds.GetCenter()).LengthSquared() > 140 * 140) continue;
                    ++seen;
                    if (canopies.size() < 1024) canopies.push_back(bounds);
                    else
                    {
                        uint32_t index = static_cast<uint32_t>(Random() * seen);
                        if (index < canopies.size()) canopies[index] = bounds;
                    }
                }
            }
        }
    }

    // 1 under a crown, fading to 0 a few metres past its edge, leaves only fall and lie where trees shed them
    inline float CanopyCover(const Vector3& position, float reach)
    {
        float nearest = reach * reach;
        for (const BoundingBox& canopy : canopies)
        {
            const Vector3 probe(position.x, canopy.GetCenter().y, position.z);
            nearest = std::min(nearest, (canopy.GetClosestPoint(probe) - probe).LengthSquared());
        }
        return 1 - Smooth(reach * 0.3f, reach, sqrtf(nearest));
    }

    inline bool Place(Patch& patch, Kind kind, Terrain* terrain, const Vector3& viewer, const Vector3& ahead)
    {
        if ((kind == Leaves || kind == Litter) && canopies.empty())
        {
            return false;
        }
        const size_t cluster = (&patch - patches.data()) / KindCount;
        const bool nearby = cluster < 4;
        for (int attempt = 0; attempt < 12; ++attempt)
        {
            Vector3 position;
            float radius = kind == Litter ? (nearby ? 12.0f : 18.0f) : (nearby ? 10.0f : 16.0f);
            // falling leaves are shed from a crown, the emitter covers that crown and a little drift past it
            if (kind == Leaves)
            {
                const BoundingBox& canopy = canopies[std::min(static_cast<size_t>(Random() * canopies.size()), canopies.size() - 1)];
                const Vector3 extent = canopy.GetExtents();
                position = canopy.GetCenter();
                radius   = std::clamp(std::max(extent.x, extent.z) + 2.0f, 4.0f, radius);
                if (nearby && (position - viewer).LengthSquared() > 30 * 30) continue;
            }
            else if (kind == Litter && ahead.LengthSquared() > 6 * 6)
            {
                // litter in view is only laid beyond 35 m, so a travelling camera has its carpet laid on the road ahead
                const Vector3 along = ahead.Normalized();
                const Vector3 across(-along.z, 0, along.x);
                radius   = 16.0f;
                position = viewer + along * (46.0f + Random() * 14.0f) + across * ((Random() - 0.5f) * 14.0f);
            }
            else
            {
                float angle = static_cast<float>(cluster % 4) * 1.5707963f + (nearby ? 0.0f : 0.7853982f) + (Random() - 0.5f) * 0.35f;
                float distance = nearby ? 10.0f : 30.0f;
                position = viewer + ahead * (nearby ? 0.15f : 0.7f) + Vector3(cosf(angle) * distance, 0, sinf(angle) * distance);
            }
            float height;
            TerrainSurfaceSample surface;
            if (!terrain->SampleHeight(position.x, position.z, height) ||
                height < terrain->GetSeaLevel() + 0.6f || !terrain->SampleSurface(position.x, position.z, surface)) continue;
            float weight = 1;
            if (kind == Seeds || kind == Fireflies)
            {
                const Vector3 mask = terrain->SamplePropMask(position.x, position.z);
                weight = kind == Seeds ? mask.x * (1 - surface.woodland * 0.7f) :
                    std::max(mask.y, mask.x * std::max(surface.woodland, surface.scrub));
                weight = std::max(weight, kind == Seeds ? 0.65f : 0.4f);
                position.y = height + radius * 0.45f + 1.0f;
                // Terrain can lie below a raised building pad. Reject roofs and
                // decks as well as authored interior volumes before placing fluff.
                Vector3 hit;
                if (PhysicsWorld::RaycastStatic(Vector3(position.x, height + 25, position.z), Vector3::Down, 26, hit) &&
                    hit.y > height + 0.5f) continue;
            }
            else
            {
                weight = CanopyCover(position, 12.0f);
                if (weight < 0.1f) continue;
                position.y = height + (kind == Litter ? 18.0f : radius * 0.45f + 4.0f);
            }
            bool rejected = false;
            const Vector3 occupied_probe = kind == Litter ? Vector3(position.x, height + 1.0f, position.z) : position;
            for (const BoundingBox& room : interiors)
                if ((room.GetClosestPoint(occupied_probe) - occupied_probe).LengthSquared() < (radius + 1) * (radius + 1)) { rejected = true; break; }
            for (size_t j = static_cast<size_t>(kind); j < patches.size(); j += KindCount)
                if (&patches[j] != &patch && patches[j].placed && (patches[j].position - position).LengthSquared() < 7 * 7) { rejected = true; break; }
            if (rejected || (viewer - position).LengthSquared() > 115 * 115) continue;
            patch.position = position;
            patch.weight = std::clamp(weight, 0.0f, 1.0f);
            patch.entity->SetPosition(position);
            patch.particles->SetEmissionRadius(radius);
            Vector3 normal = Vector3::Up;
            terrain->SampleNormal(position.x, position.z, normal);
            patch.particles->SetGroundPlane(Vector4(normal.x, normal.y, normal.z, -normal.Dot(Vector3(position.x, height + 0.1f, position.z))));
            // Reset the motion baseline before the renderer sees the recycled emitter.
            patch.particles->UpdateRuntime(position, 0);
            patch.placed = true;
            patch.warm_start = true;
            return true;
        }
        return false;
    }

    inline void Tick(float dt)
    {
        if (!world_systems::GetConfig().ambient_atmosphere || !World::GetCamera()) return;
        Terrain* terrain = Terrain::FindActive();
        if (!terrain || !terrain->HasHeightfield()) return;
        // leaves are born and settle only under open sky, the gpu reads the same roof grid as rain,
        // and the warm start waits until that grid has traced the cells around the camera
        Weather::RequestShelter();
        if (shelter_frames < 45)
        {
            ++shelter_frames;
            return;
        }
        const Vector3 viewer  = World::GetCamera()->GetEntity()->GetPosition();
        const Vector3 forward = World::GetCamera()->GetEntity()->GetForward();
        // a cut moves the camera further than its own motion carries it in a frame, or turns it at once
        const bool turned = forward.LengthSquared() > 0.5f && previous_forward.LengthSquared() > 0.5f && forward.Dot(previous_forward) < 0.8f;
        const bool cut = !viewer_known || (viewer - previous_viewer - motion * dt).Length() > 4.0f || turned;
        Vector3 ahead = !cut && dt > 0.001f ? (viewer - previous_viewer) / dt : Vector3::Zero;
        ahead.y = 0;
        if (ahead.LengthSquared() > 45 * 45) ahead = ahead.Normalized() * 45;
        motion = cut ? Vector3::Zero : motion + (ahead - motion) * (1 - expf(-std::clamp(dt, 0.0f, 0.1f) * 4));
        previous_viewer  = viewer;
        previous_forward = forward;
        viewer_known = true;
        dt = std::clamp(dt, 0.0f, 0.1f);
        clock += dt;
        sample_clock -= dt;
        bool sample = sample_clock <= 0 || cut;
        if (sample) { sample_clock = 0.5f; SampleNeighborhood(viewer); }
        if (cut) seed_visible_frames = 4;
        const bool moving = motion.LengthSquared() > 6 * 6;
        const Vector3 along = moving ? motion.Normalized() : Vector3::Zero;
        if (!root_id)
        {
            Entity* root = World::CreateEntity();
            root->SetObjectName("ambient_atmosphere");
            root->SetTransient(true);
            root_id = root->GetObjectId();
            const char* effects[] = {"leaves", "seeds", "fireflies", "litter"};
            for (size_t i = 0; i < patches.size(); ++i)
            {
                Patch& patch = patches[i];
                patch.entity = World::CreateEntity();
                patch.entity->SetObjectName(std::string("atmosphere_") + effects[i % KindCount]);
                patch.entity->SetTransient(true);
                patch.entity->SetParent(root);
                patch.particles = patch.entity->AddComponent<ParticleSystem>();
                if (!patch.particles->LoadEffect(std::string("project/plan_resources/atmosphere/") + effects[i % KindCount] + ".particle"))
                {
                    // Missing packages must never produce the component's default fire.
                    patch.particles->SetMaxParticles(1);
                    patch.authored_rate = 0;
                }
                else patch.authored_rate = patch.particles->GetEmissionRate();
                patch.particles->SetEmissionRate(0);
            }
        }
        const float hour = World::GetTimeOfDay() * 24;
        const float night = 1 - Smooth(5, 7, hour) + Smooth(18, 21, hour);
        const float dry = 1 - std::clamp(Weather::GetRain(), 0.0f, 1.0f);
        bool indoors = false;
        for (const BoundingBox& room : interiors) if (room.Contains(viewer)) { indoors = true; break; }
        for (size_t i = 0; i < patches.size(); ++i)
        {
            Patch& patch = patches[i];
            if (patch.authored_rate <= 0) continue;
            Kind kind = static_cast<Kind>(i % KindCount);
            // Do not wait a whole particle lifetime to replenish the view at racing
            // speed. Old particles keep simulating in the same fixed GPU slice.
            const float recycle_distance = kind == Litter ? 58.0f : (i / KindCount < 4 ? 38.0f : 85.0f);
            // the carpet laid ahead is relaid once the camera has passed it
            const bool passed = moving && kind == Litter && (patch.position - viewer).Dot(along) < -10.0f;
            if (patch.placed && ((patch.position - viewer).LengthSquared() > recycle_distance * recycle_distance || passed))
            {
                patch.placed = false;
                patch.rate = 0;
            }
            // after a cut the carpet around the new view is filled at once, the frame changed anyway
            if (patch.placed && cut && kind == Litter) patch.warm_start = true;
            if (!patch.placed && sample) Place(patch, kind, terrain, viewer, motion);
            if (kind == Litter) patch.particles->SetSeedVisible(seed_visible_frames > 0);
            float distance = (patch.position - viewer).Length();
            float target = patch.placed && !indoors ?
                patch.authored_rate * patch.weight * (1 - Smooth(65, 100, distance)) * (kind == Leaves || kind == Litter ? 0.6f + 0.4f * dry : dry) : 0;
            if (kind == Fireflies) target *= night;
            else target *= 0.85f + 0.15f * sinf(clock * 0.37f + static_cast<float>(i) * 2.4f);
            if (patch.warm_start && target > 0)
            {
                // Arrive to an inhabited landscape, not a minutes-long fill-up.
                const float fill = kind == Litter ? 0.9f : (kind == Leaves ? 0.35f : 0.25f);
                patch.particles->TriggerBurst(static_cast<float>(patch.particles->GetMaxParticles()) * fill);
                patch.warm_start = false;
            }
            patch.rate += (target - patch.rate) * (1 - expf(-dt * 2));
            if (target == 0 && patch.rate < 0.01f) patch.rate = 0;
            patch.particles->SetEmissionRate(patch.rate);
        }
        if (seed_visible_frames > 0) --seed_visible_frames;
    }
}
