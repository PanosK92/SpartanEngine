// Copyright(c) 2015-2026 Panos Karabelas
// Licensed under the Spartan Engine License. See license.md in the repository root.
// https://github.com/PanosK92/SpartanEngine/blob/master/license.md
// Commercial use requires written permission and negotiated payment terms.
#pragma once
#include "World.h"
#include "Entity.h"
#include "WildlifeMotion.h"
#include "components/Camera.h"
#include "components/Terrain.h"
#include "components/Render.h"
#include "components/Light.h"
#include "../rendering/Material.h"
#include "../physics/PhysicsWorld.h"
#include <array>
#include <random>
#include <unordered_map>

namespace spartan::island_wildlife
{
    using namespace math;

    // A small, reusable population, independent of island area. These are deliberately
    // blockout animals; no imported rigs, physics bodies or persistent world entities.
    enum Species { Songbird, Gull, Squirrel, Marten, Lizard, Butterfly, SpeciesCount };
    enum class Activity { Roam, Forage, Sniff, Rest, Cruise, Landing, Perched, Takeoff, Flee, Watch };
    struct Animal
    {
        uint64_t id = 0;
        Species species = Songbird;
        Vector3 position = Vector3::Zero;
        Vector3 home = Vector3::Zero;
        Vector3 heading = Vector3::Forward;
        Steering steering;
        Activity activity = Activity::Roam;
        float activity_time = 0;
        Vector3 destination = Vector3::Zero;
        Vector3 alarm_source = Vector3::Zero;
        float alarm_delay = -1;
        float alarm_cooldown = 0;
        uint32_t flock = 0;
        std::string activity_tag;
        float clock = 0;
        float decision = 0;
        float age = 0;
        float retry = 0;
        float curiosity_cooldown = 0;
        float stride = 0;
        bool left_foot = false;
        bool placed = false;
    };
    inline std::vector<Animal> animals;
    inline uint64_t root_id = 0;
    struct Track { uint64_t id = 0; Vector3 position; float age = 0; };
    inline std::vector<Track> tracks;
    inline size_t next_track = 0;
    inline std::shared_ptr<Material> track_material;
    inline Vector3 previous_viewer = Vector3::Zero;
    inline bool viewer_known = false;
    inline float viewer_speed = 0;
    inline float weather_clock = 0;
    inline float authored_clouds = -1;
    inline uint64_t weather_light_id = 0;
    inline std::array<std::shared_ptr<Material>, SpeciesCount> materials;
    inline std::mt19937 random(73191);

    inline float Random(float lo, float hi)
    {
        return std::uniform_real_distribution<float>(lo, hi)(random);
    }

    inline bool HasWings(const Animal& animal)
    {
        return animal.species == Songbird || animal.species == Gull || animal.species == Butterfly;
    }

    inline bool IsAirborne(const Animal& animal)
    {
        return HasWings(animal) && (animal.activity == Activity::Cruise ||
            animal.activity == Activity::Landing || animal.activity == Activity::Takeoff);
    }

    inline float StandingHeight(const Animal& animal)
    {
        return animal.species == Gull ? .2f : animal.species == Butterfly ? .06f : .1f;
    }

    inline void BeginActivity(Animal& animal, Activity activity, float seconds)
    {
        animal.activity = activity;
        animal.activity_time = seconds;
        animal.decision = 0;
    }

    inline void Clear(bool remove_entities)
    {
        if (remove_entities)
            if (Entity* entity = World::GetEntityById(root_id)) World::RemoveEntity(entity);
        if (remove_entities && authored_clouds >= 0)
            if (Entity* light_entity = World::GetEntityById(weather_light_id))
                if (Light* light = light_entity->GetComponent<Light>()) light->SetCloudCoverage(authored_clouds);
        tracks.clear();
        next_track = 0;
        track_material.reset();
        viewer_known = false;
        weather_clock = 0;
        authored_clouds = -1;
        weather_light_id = 0;
        animals.clear();
        root_id = 0;
        materials.fill(nullptr);
        random.seed(73191);
    }

    inline Entity* Block(Entity* parent, const char* name, Vector3 position, Vector3 scale, Species species)
    {
        Entity* part = World::CreateEntity();
        part->SetObjectName(name);
        part->SetTransient(true);
        part->SetParent(parent);
        part->SetPositionLocal(position);
        part->SetScaleLocal(scale);
        Render* render = part->AddComponent<Render>();
        render->SetMesh(MeshType::Cube);
        render->SetMaterial(materials[species]);
        render->SetMaxRenderDistance(180.0f);
        render->SetMaxShadowDistance(35.0f);
        render->SetFlag(RenderFlags::ExcludeFromRayTracing);
        return part;
    }

    inline Entity* Create(Species species, Entity* root)
    {
        static const char* names[] = {"songbird", "gull", "squirrel", "marten", "lizard", "butterfly"};
        static const Color colors[] = {
            Color(.24f,.18f,.12f,1), Color(.78f,.8f,.76f,1), Color(.38f,.17f,.06f,1),
            Color(.22f,.15f,.09f,1), Color(.27f,.34f,.12f,1), Color(.9f,.48f,.1f,1)
        };
        if (!materials[species])
        {
            materials[species] = std::make_shared<Material>();
            materials[species]->SetObjectName(std::string("wildlife_") + names[species]);
            materials[species]->SetColor(colors[species]);
            materials[species]->SetProperty(MaterialProperty::Roughness, .9f);
        }
        Entity* entity = World::CreateEntity();
        entity->SetObjectName(std::string("wildlife_") + names[species]);
        entity->AddTag("dynamic"); // transform-driven mobility, inherited by every body part
        entity->SetTransient(true);
        entity->SetParent(root);
        entity->SetActive(false);
        const bool wings = species == Songbird || species == Gull || species == Butterfly;
        if (wings)
        {
            float size = species == Gull ? 2.4f : species == Butterfly ? .35f : 1.0f;
            Block(entity, "body", Vector3(0,0,0), Vector3(.13f,.14f,.32f)*size, species);
            Block(entity, "head", Vector3(0,.08f,.19f)*size, Vector3(.12f,.12f,.12f)*size, species);
            Block(entity, "wing_left", Vector3(-.24f,0,0)*size, Vector3(.42f,.035f,.18f)*size, species);
            Block(entity, "wing_right", Vector3(.24f,0,0)*size, Vector3(.42f,.035f,.18f)*size, species);
            Block(entity, "tail", Vector3(0,0,-.22f)*size, Vector3(.16f,.035f,.18f)*size, species);
        }
        else
        {
            const float size = species == Marten ? 1.5f : species == Lizard ? .6f : 1.0f;
            Block(entity, "body", Vector3(0,.15f,0)*size, Vector3(.17f,.19f,.4f)*size, species);
            Block(entity, "head", Vector3(0,.23f,.23f)*size, Vector3(.15f,.15f,.17f)*size, species);
            Block(entity, "tail", Vector3(0,species == Squirrel ? .3f : .12f,-.33f)*size,
                Vector3(.09f,species == Squirrel ? .38f : .06f,.32f)*size, species);
            for (float x : {-.085f,.085f})
                for (float z : {-.13f,.13f})
                    Block(entity, "leg", Vector3(x,.05f,z)*size, Vector3(.045f,.12f,.055f)*size, species);
        }
        return entity;
    }

    inline bool Ground(Terrain* terrain, Vector3& position)
    {
        Vector3 normal;
        if (!terrain->SampleHeight(position.x, position.z, position.y) ||
            position.y < terrain->GetSeaLevel() + .8f ||
            !terrain->SampleNormal(position.x, position.z, normal) || normal.y < .78f) return false;
        // The baked prop mask includes roads and building pads, not just habitat.
        const Vector3 mask = terrain->SamplePropMask(position.x, position.z);
        if (mask.x + mask.y < .12f) return false;
        Vector3 hit;
        if (PhysicsWorld::RaycastStatic(position + Vector3(0,8,0), Vector3::Down, 9.0f, hit) &&
            hit.y > position.y + .45f) return false;
        return true;
    }

    inline void LeaveTrack(Animal& animal, Terrain* terrain, Entity* root, const std::unordered_map<uint64_t, Entity*>& live)
    {
        // Only mammals leave these paired paw prints, on gentle, sparse ground
        // where they can be read. A fixed pool bounds entity and draw costs.
        if (animal.species != Squirrel && animal.species != Marten) return;
        Vector3 normal;
        Vector3 spot = animal.position + Vector3(-animal.heading.z,0,animal.heading.x)*(animal.left_foot ? .07f : -.07f);
        animal.left_foot = !animal.left_foot;
        if (!Ground(terrain,spot) || !terrain->SampleNormal(spot.x,spot.z,normal) || normal.y < .94f) return;
        const Vector3 mask = terrain->SamplePropMask(spot.x,spot.z);
        if (mask.x > .8f) return;
        Entity* print = nullptr;
        if (tracks.size() < 96)
        {
            if (!track_material)
            {
                track_material = std::make_shared<Material>();
                track_material->SetObjectName("wildlife_track_soil");
                track_material->SetColor(Color(.13f,.105f,.07f,1));
                track_material->SetProperty(MaterialProperty::Roughness,1);
            }
            print = World::CreateEntity();
            print->SetObjectName("wildlife_paw_print");
            print->SetTransient(true);
            print->SetParent(root);
            for (int part = 0; part < 4; ++part)
            {
                Entity* pad = Block(print,"paw",part == 0 ? Vector3::Zero : Vector3((part-2)*.025f,0,.04f),
                    part == 0 ? Vector3(.055f,.003f,.06f) : Vector3(.017f,.003f,.025f),animal.species);
                pad->GetComponent<Render>()->SetMaterial(track_material);
                pad->GetComponent<Render>()->SetMaxRenderDistance(35);
                pad->GetComponent<Render>()->SetMaxShadowDistance(0);
            }
            tracks.push_back({print->GetObjectId(),spot,0});
            next_track = tracks.size()-1;
        }
        else if (const auto found = live.find(tracks[next_track].id); found != live.end()) print = found->second;
        if (print)
        {
            spot += normal*.005f;
            print->SetPosition(spot);
            const Vector3 tangent = animal.heading-normal*animal.heading.Dot(normal);
            print->SetRotation(Quaternion::FromLookRotation(tangent,normal));
            print->SetScale(Vector3::One);
            print->SetActive(true);
            tracks[next_track] = {print->GetObjectId(),spot,0};
        }
        next_track = (next_track+1)%96;
    }

    inline bool Place(Animal& animal, Terrain* terrain, const Vector3& viewer)
    {
        // Finite attempts keep coastlines, airports and teleports inexpensive.
        for (int attempt = 0; attempt < 10; ++attempt)
        {
            const float angle = Random(0,6.2831853f);
            const float radius = Random(22,100);
            Vector3 center = viewer;
            float placement_radius = radius;
            if (animal.species == Songbird || animal.species == Gull)
            {
                for (const Animal& other : animals)
                {
                    if (other.id != animal.id && other.placed && other.species == animal.species &&
                        other.flock == animal.flock && (other.home-viewer).LengthSquared() < 120*120)
                    {
                        center = other.home;
                        placement_radius = Random(3,10);
                        break;
                    }
                }
            }
            Vector3 p = center + Vector3(cosf(angle)*placement_radius,0,sinf(angle)*placement_radius);
            if (animal.species == Gull)
            {
                if (!terrain->SampleHeight(p.x,p.z,p.y)) continue;
                if (p.y > terrain->GetSeaLevel() + 35) continue;
                p.y = std::max(p.y, terrain->GetSeaLevel()) + Random(15,28);
            }
            else
            {
                if (!Ground(terrain,p)) continue;
                if (animal.species == Songbird) p.y += Random(5,12);
                if (animal.species == Butterfly) p.y += Random(.7f,1.8f);
            }
            animal.position = animal.home = p;
            Vector3 landing = p;
            const bool can_land = HasWings(animal) && Ground(terrain, landing);
            if (can_land) animal.home = landing;
            if (can_land && Random(0,1) < .55f)
            {
                animal.position = landing;
                BeginActivity(animal, Activity::Perched, Random(5,12));
            }
            else BeginActivity(animal, HasWings(animal) ? Activity::Cruise : Activity::Roam, Random(6,12));
            animal.curiosity_cooldown = Random(2,6);
            animal.stride = 0;
            animal.alarm_delay = -1;
            animal.alarm_cooldown = 0;
            animal.heading = Vector3(cosf(angle),0,sinf(angle));
            animal.steering = {angle, angle, 0, false};
            animal.age = 0;
            animal.decision = Random(1,5);
            return true;
        }
        return false;
    }

    inline bool MoveOnGround(Animal& animal, Terrain* terrain, float dt, float speed)
    {
        Vector3 support = animal.position;
        if (Ground(terrain, support)) animal.position.y = support.y;
        Vector3 next = animal.position;
        const bool moved = AdvanceGround(animal.steering, dt, speed, [&](float yaw, float distance)
        {
            const Vector3 direction(cosf(yaw), 0, sinf(yaw));
            Vector3 previous = animal.position;
            const int steps = std::max(1, static_cast<int>(ceilf(distance/.2f)));
            for (int step = 1; step <= steps; ++step)
            {
                Vector3 point = animal.position + direction*(distance*step/steps);
                if (!Ground(terrain, point) || fabsf(point.y-previous.y) > .3f) return false;
                const Vector3 segment = point-previous;
                const float length = segment.Length();
                Vector3 hit;
                // Above the small discrepancy permitted between sampled and physical ground.
                if (length > .00001f && PhysicsWorld::RaycastStatic(
                    previous+Vector3(0,.55f,0), segment/length, length, hit)) return false;
                previous = point;
            }
            next = previous;
            return true;
        });
        animal.heading = Vector3(cosf(animal.steering.yaw),0,sinf(animal.steering.yaw));
        if (moved) animal.position = next;
        return moved;
    }

    inline bool FindLanding(Animal& animal, Terrain* terrain, const Vector3& viewer)
    {
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            // Prefer the flock's patch, with independent spots so birds do not stack.
            Vector3 spot = (attempt < 4 ? animal.home : animal.position) + Vector3(Random(-12,12),0,Random(-12,12));
            if (!Ground(terrain, spot) || (spot-viewer).LengthSquared() < 10*10) continue;
            if (animal.species == Gull && spot.y > terrain->GetSeaLevel()+35) continue;
            bool occupied = false;
            for (const Animal& other : animals)
                if (other.id != animal.id && other.placed && HasWings(other) &&
                    (other.activity == Activity::Landing || other.activity == Activity::Perched) &&
                    (spot-(other.activity == Activity::Landing ? other.destination : other.position)).LengthSquared() < 2)
                    occupied = true;
            if (occupied) continue;
            animal.destination = spot + Vector3(0,StandingHeight(animal),0);
            BeginActivity(animal, Activity::Landing, 16);
            return true;
        }
        return false;
    }

    inline void TakeOff(Animal& animal, const Vector3& away_from, bool alarm)
    {
        if (!IsAirborne(animal)) animal.position.y += StandingHeight(animal);
        Vector3 direction = animal.position-away_from;
        direction.y = 0;
        if (direction.LengthSquared() > .001f) animal.steering.target_yaw = atan2f(direction.z,direction.x);
        BeginActivity(animal, Activity::Takeoff, animal.species == Butterfly ? 1.5f : 3.0f);
        animal.alarm_cooldown = alarm ? 8.0f : 0.0f;
    }

    inline float UpdateRoutine(Animal& animal, Terrain* terrain, const Vector3& viewer, bool night, float dt)
    {
        animal.activity_time -= dt;
        animal.decision -= dt;
        animal.alarm_cooldown = std::max(0.0f,animal.alarm_cooldown-dt);
        const bool winged = HasWings(animal);
        animal.curiosity_cooldown = std::max(0.0f, animal.curiosity_cooldown-dt);
        const float viewer_distance = (animal.position-viewer).Length();
        const float scare_radius = winged ? (viewer_speed > 3 ? 12.0f : 5.0f) :
            animal.species == Lizard ? 4.0f : viewer_speed > 3 ? 10.0f : 3.5f;
        const bool near = viewer_distance < scare_radius;
        if (!winged && !near && viewer_distance < 14 && animal.activity != Activity::Flee &&
            animal.activity != Activity::Watch && animal.curiosity_cooldown <= 0)
        {
            BeginActivity(animal,Activity::Watch,Random(2,4));
            animal.curiosity_cooldown = Random(12,22);
        }
        if (animal.alarm_delay >= 0)
        {
            animal.alarm_delay -= dt;
            if (animal.alarm_delay <= 0)
            {
                animal.alarm_delay = -1;
                TakeOff(animal,animal.alarm_source,true);
            }
        }
        if (near && winged && animal.activity != Activity::Takeoff && animal.alarm_cooldown <= 0)
            TakeOff(animal,viewer,true);
        if (near && !winged)
        {
            if (animal.activity != Activity::Flee) BeginActivity(animal,Activity::Flee,3);
            animal.activity_time = 3; // finish escaping before resuming a routine
        }
        if (animal.activity == Activity::Watch && animal.activity_time > 0)
        {
            Vector3 toward = viewer-animal.position;
            if (toward.x*toward.x+toward.z*toward.z > .001f)
            {
                animal.steering.target_yaw = atan2f(toward.z,toward.x);
                animal.steering.yaw += std::clamp(AngleDelta(animal.steering.target_yaw,animal.steering.yaw),-1.5f*dt,1.5f*dt);
                animal.heading = Vector3(cosf(animal.steering.yaw),0,sinf(animal.steering.yaw));
            }
            return 0;
        }
        if (!winged && animal.activity_time <= 0)
        {
            switch (animal.activity)
            {
                case Activity::Roam: BeginActivity(animal,Activity::Forage,Random(3,6)); break;
                case Activity::Forage: BeginActivity(animal,Activity::Sniff,Random(2,4)); break;
                case Activity::Sniff:
                    BeginActivity(animal,Activity::Rest,night && animal.species != Marten ? Random(12,24) : Random(3,6)); break;
                default: BeginActivity(animal,Activity::Roam,Random(6,12)); break;
            }
        }
        if (winged && animal.activity_time <= 0)
        {
            if (animal.activity == Activity::Perched)
                TakeOff(animal,animal.position-animal.heading,false);
            else if (animal.activity == Activity::Takeoff)
                BeginActivity(animal,Activity::Cruise,Random(6,12));
            else if (animal.activity == Activity::Cruise)
            {
                if (!FindLanding(animal,terrain,viewer)) animal.activity_time = Random(3,6);
            }
            else BeginActivity(animal,Activity::Cruise,Random(3,6)); // abandon an obstructed approach
        }

        if (winged && animal.activity == Activity::Perched)
        {
            Vector3 support = animal.position;
            if (!Ground(terrain,support)) TakeOff(animal,viewer,false);
            else animal.position.y = support.y;
            return 0;
        }

        if (animal.decision <= 0 && (!animal.steering.avoiding || winged))
        {
            if (winged || animal.steering.retry <= 0)
            {
                Vector3 direction = animal.activity == Activity::Flee ? animal.position-viewer : animal.home-animal.position;
                direction.y = 0;
                if (animal.activity != Activity::Flee) direction += Vector3(Random(-10,10),0,Random(-10,10));
                if (animal.activity != Activity::Takeoff && direction.LengthSquared() > .001f)
                    animal.steering.target_yaw = atan2f(direction.z,direction.x);
                animal.decision = animal.activity == Activity::Flee ? .75f : Random(2,4);
            }
        }
        if (!winged)
        {
            float speed = animal.activity == Activity::Roam ? (animal.species == Lizard ? .4f : .8f) :
                animal.activity == Activity::Forage ? (animal.species == Marten ? .35f : .16f) :
                animal.activity == Activity::Flee ? (animal.species == Lizard ? 1.2f : 2.5f) : 0.0f;
            return MoveOnGround(animal,terrain,dt,speed) ? speed : 0;
        }

        const float speed = animal.species == Gull ? 5.0f : animal.species == Butterfly ? .9f : 3.5f;
        Vector3 next = animal.position;
        if (animal.activity == Activity::Landing)
        {
            Vector3 support = animal.destination;
            if (!Ground(terrain,support)) { BeginActivity(animal,Activity::Cruise,3); return 0; }
            animal.destination.y = support.y+StandingHeight(animal);
            Vector3 delta = animal.destination-animal.position;
            Vector3 horizontal(delta.x,0,delta.z);
            const float distance = horizontal.Length();
            if (distance > .05f) animal.steering.target_yaw = atan2f(delta.z,delta.x);
            // Approach the selected patch before descending the last few metres.
            if (distance > .05f) next += horizontal/distance*std::min(distance,speed*dt);
            const float target_y = animal.destination.y + std::min(5.0f,distance*.35f);
            next.y += std::clamp(target_y-next.y,-3.0f*dt,3.0f*dt);
        }
        else
        {
            animal.steering.yaw += std::clamp(AngleDelta(animal.steering.target_yaw,animal.steering.yaw),-2.5f*dt,2.5f*dt);
            animal.heading = Vector3(cosf(animal.steering.yaw),0,sinf(animal.steering.yaw));
            next += animal.heading*(speed*dt);
            float floor;
            if (!terrain->SampleHeight(next.x,next.z,floor)) return 0;
            floor = std::max(floor,terrain->GetSeaLevel());
            const float altitude = animal.species == Gull ? 12.0f : animal.species == Butterfly ? 1.1f : 6.0f;
            next.y += std::clamp(floor+altitude+sinf(animal.clock*1.7f)*altitude*.08f-next.y,-2.0f*dt,3.5f*dt);
        }
        if (animal.activity == Activity::Landing)
        {
            animal.steering.yaw += std::clamp(AngleDelta(animal.steering.target_yaw,animal.steering.yaw),-2.5f*dt,2.5f*dt);
            animal.heading = Vector3(cosf(animal.steering.yaw),0,sinf(animal.steering.yaw));
        }
        float floor;
        const Vector3 segment = next-animal.position;
        const float length = segment.Length();
        Vector3 hit;
        if (!terrain->SampleHeight(next.x,next.z,floor) || next.y < floor+StandingHeight(animal)*.8f ||
            (length > .00001f && PhysicsWorld::RaycastStatic(animal.position,segment/length,length,hit)))
        {
            // A failed flight step also keeps its heading; choose another route at a bounded rate.
            if (animal.decision <= 0)
            {
                animal.steering.target_yaw += 1.5707963f;
                animal.decision = 1;
            }
            if (animal.activity == Activity::Landing) BeginActivity(animal,Activity::Cruise,3);
            return 0;
        }
        animal.position = next;
        if (animal.activity == Activity::Landing && (animal.position-animal.destination).LengthSquared() < .015f)
        {
            animal.position = animal.destination-Vector3(0,StandingHeight(animal),0);
            animal.home = animal.position;
            BeginActivity(animal,Activity::Perched,night ? Random(20,35) : Random(7,15));
            return 0;
        }
        return speed;
    }

    inline void Tick(float dt)
    {
        if (World::GetName() != "plan.world" || !World::GetCamera()) return;
        Terrain* terrain = Terrain::FindActive();
        if (!terrain || !terrain->HasHeightfield()) return;
        const Vector3 viewer = World::GetCamera()->GetEntity()->GetPosition();
        const float elapsed = std::max(0.0f,dt);
        viewer_speed = viewer_known && elapsed > .0001f ? (viewer-previous_viewer).Length()/elapsed : 0;
        previous_viewer = viewer;
        viewer_known = true;
        dt = std::clamp(dt, 0.0f, .05f);
        // A slow cloud bank passes, then clears back toward the authored sky.
        // Keep wind direction stable: cloud advection uses wind multiplied by time.
        weather_clock += elapsed;
        if (Light* light = World::GetDirectionalLight())
        {
            if (authored_clouds < 0)
            {
                authored_clouds = light->GetCloudCoverage();
                weather_light_id = light->GetEntity()->GetObjectId();
            }
            const float wave = .5f-.5f*cosf(weather_clock*6.2831853f/240.0f);
            const float bank = wave*wave*(3-2*wave);
            light->SetCloudCoverage(authored_clouds+(std::max(authored_clouds,.78f)-authored_clouds)*bank);
        }
        Entity* root = root_id ? World::GetEntityById(root_id) : nullptr;
        if (!root)
        {
            Clear(false);
            root = World::CreateEntity();
            root->SetObjectName("island_wildlife");
            root->SetTransient(true);
            root_id = root->GetObjectId();
        }
        // Build at most two blockouts per frame, avoiding a first-frame burst.
        for (int i = 0; i < 2 && animals.size() < 72; ++i)
        {
            Animal animal;
            animal.species = static_cast<Species>(animals.size() % SpeciesCount);
            animal.flock = static_cast<uint32_t>(animals.size() / SpeciesCount) / 4;
            animal.id = Create(animal.species, root)->GetObjectId();
            animal.clock = Random(0,100);
            animals.push_back(animal);
        }
        const float hour = World::GetTimeOfDay() * 24;
        const bool night = hour < 6 || hour > 20;
        // One world lookup, then a tiny local index; never scan the entire island per animal.
        std::unordered_map<uint64_t, Entity*> live;
        for (Entity* child : root->GetChildren()) live.emplace(child->GetObjectId(), child);
        for (Track& track : tracks)
        {
            track.age += elapsed;
            if (Entity* print = live.count(track.id) ? live.at(track.id) : nullptr)
            {
                const float fade = std::clamp((90-track.age)/15,0.0f,1.0f);
                print->SetActive(fade > 0 && (track.position-viewer).LengthSquared() < 40*40);
                print->SetScale(Vector3::One*std::max(.001f,fade));
            }
        }
        for (const Animal& source : animals)
        {
            if (!source.placed || (source.species != Songbird && source.species != Gull) ||
                (source.position-viewer).LengthSquared() >= (viewer_speed > 3 ? 144.0f : 25.0f)) continue;
            for (Animal& member : animals)
            {
                if (!member.placed || member.species != source.species || member.flock != source.flock ||
                    member.alarm_delay >= 0 || member.alarm_cooldown > 0 || member.activity == Activity::Takeoff ||
                    (member.position-source.position).LengthSquared() > 35*35) continue;
                member.alarm_source = viewer;
                member.alarm_delay = Random(.08f,.4f);
            }
        }
        for (Animal& animal : animals)
        {
            const auto found = live.find(animal.id);
            if (found == live.end()) continue;
            Entity* entity = found->second;
            animal.clock += dt;
            const bool flying = animal.species == Songbird || animal.species == Gull || animal.species == Butterfly;
            Vector3 offset = animal.position - viewer;
            offset.y = 0;
            if (!animal.placed || offset.LengthSquared() > 180*180)
            {
                entity->SetActive(false);
                animal.retry -= dt;
                if (animal.retry > 0) continue;
                animal.retry = Random(1,3);
                animal.placed = Place(animal,terrain,viewer);
                if (!animal.placed) continue;
            }
            animal.age += dt;
            entity->SetActive(true);
            const Vector3 previous_position = animal.position;
            const float speed = UpdateRoutine(animal,terrain,viewer,night,dt);
            animal.stride += (animal.position-previous_position).Length();
            if (!flying && animal.stride >= (animal.species == Marten ? .55f : .35f))
            {
                animal.stride = 0;
                LeaveTrack(animal,terrain,root,live);
            }
            const bool airborne = IsAirborne(animal);
            static const char* activity_names[] = {"roam","forage","sniff","rest","cruise","landing","perched","takeoff","flee","watch"};
            const std::string tag = std::string("wildlife_") + activity_names[static_cast<int>(animal.activity)];
            if (tag != animal.activity_tag)
            {
                if (!animal.activity_tag.empty()) entity->RemoveTag(animal.activity_tag);
                entity->AddTag(tag);
                animal.activity_tag = tag;
            }
            Vector3 position = animal.position;
            if (flying && !airborne) position.y += StandingHeight(animal);
            if (!flying && speed > 0 && animal.species != Lizard) position.y += fabsf(sinf(animal.clock*12))*.045f;
            entity->SetPosition(position);
            entity->SetRotation(Quaternion::FromLookRotation(animal.heading));
            // Gentle emergence and distance shrink prevent abrupt recycling at the perimeter.
            const float distance = (position-viewer).Length();
            const float fade = std::min(std::min(animal.age,1.0f),std::clamp((180-distance)/30,0.0f,1.0f));
            entity->SetScale(Vector3::One * std::max(.001f,fade));
            for (Entity* part : entity->GetChildren())
            {
                const std::string& name = part->GetObjectName();
                if (name == "wing_left" || name == "wing_right")
                {
                    const float flap = !airborne ? 78.0f :
                        (animal.activity == Activity::Cruise && sinf(animal.clock*.65f) > .35f ? 5.0f :
                            sinf(animal.clock*(animal.species == Butterfly ? 30 : 12))*40);
                    const float size = animal.species == Gull ? 2.4f : animal.species == Butterfly ? .35f : 1.0f;
                    part->SetPositionLocal(Vector3((name == "wing_left" ? -1.0f : 1.0f)*(airborne ? .24f : .09f)*size,0,0));
                    part->SetRotationLocal(Quaternion::FromEulerAngles(0,0,name == "wing_left" ? flap : -flap));
                }
                else if (name == "head")
                {
                    const bool pecking = animal.activity == Activity::Perched || animal.activity == Activity::Forage;
                    const float dip = pecking && sinf(animal.clock*.65f) > 0 ? std::max(0.0f,sinf(animal.clock*4.5f))*35 : 0;
                    const float sniff = animal.activity == Activity::Sniff ? sinf(animal.clock*2)*28 : 0;
                    part->SetRotationLocal(Quaternion::FromEulerAngles(dip,sniff,0));
                }
                else if (name == "tail" && !flying)
                    part->SetRotationLocal(Quaternion::FromEulerAngles(0,sinf(animal.clock*4)*15,0));
                else if (name == "leg")
                    part->SetRotationLocal(Quaternion::FromEulerAngles(speed > 0 ? sinf(animal.clock*12 + part->GetPositionLocal().z*20)*25 : 0,0,0));
            }
        }
    }
}
