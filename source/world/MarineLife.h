/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

#include "World.h"
#include "Entity.h"
#include "components/Water.h"
#include "components/Terrain.h"
#include "components/Camera.h"
#include "components/Render.h"
#include "../geometry/GeometryGeneration.h"
#include "../geometry/Mesh.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include <array>
#include <random>

namespace spartan::marine_life
{
    using namespace math;

    struct Habitat
    {
        const char* name;
        Color color;
        int coastal_groups, ocean_groups, members;
        float length, orbit, speed, depth, clearance;
    };

    inline const Habitat habitats[Water::SpeciesCount] = {
        { "Schooling fish", Color(.15f,.51f,.59f,1), 4, 3, 7, .65f, 5, 1.6f, 2, .28f },
        { "Shark",          Color(.21f,.29f,.34f,1), 1, 3, 1, 3.2f, 13, 2.5f, 2.2f, .8f },
        { "Jellyfish",      Color(.70f,.43f,.78f,1), 4, 2, 2, .75f, 1.8f, .18f, 1.6f, .7f },
        { "Whale",          Color(.12f,.19f,.24f,1), 1, 2, 1, 11, 24, 2.8f, 4, 2.8f },
        { "Crab",           Color(.65f,.19f,.08f,1), 5, 2, 1, .45f, 1.4f, .22f, 0, .08f }
    };

    struct Model
    {
        std::shared_ptr<Mesh> body, tail;
        std::shared_ptr<Material> material;
    };
    struct Group
    {
        Vector3 center;
        float phase = 0, retry = 0;
        bool placed = false;
        std::array<uint64_t, 7> members{};
    };

    inline std::array<Model, Water::SpeciesCount> models;
    inline std::array<std::vector<Group>, Water::SpeciesCount> groups;
    inline uint64_t root_id = 0, ocean_id = 0;
    inline std::mt19937 generator(3541);

    inline void Clear(bool remove_entities)
    {
        if (root_id && remove_entities)
            if (Entity* root = World::GetEntityById(root_id)) World::RemoveEntity(root);
        groups = {};
        models = {};
        root_id = ocean_id = 0;
        generator.seed(3541);
    }

    // Bake the lobes, fins and limbs into one small mesh per species and a separate articulated tail.
    inline std::shared_ptr<Mesh> BuildMesh(Water::Species species, bool tail)
    {
        std::vector<RHI_Vertex_PosTexNorTan> sphere, vertices;
        std::vector<uint32_t> triangles, indices;
        geometry_generation::generate_sphere(&sphere, &triangles, 1, 12, 8);
        auto lobe = [&](Vector3 center, Vector3 radii, Vector3 angles = Vector3::Zero)
        {
            const Quaternion rotation = Quaternion::FromEulerAngles(angles);
            const uint32_t offset = static_cast<uint32_t>(vertices.size());
            for (auto vertex : sphere)
            {
                vertex.set_position(center + rotation * (vertex.get_position() * radii));
                vertex.set_normal(rotation * (vertex.get_normal() / radii).Normalized());
                vertex.set_tangent(rotation * (vertex.get_tangent() * radii).Normalized());
                vertices.push_back(vertex);
            }
            for (uint32_t i : triangles) indices.push_back(offset + i);
        };
        if (species == Water::Jellyfish)
        {
            lobe({0,.09f,0}, {.38f,.21f,.38f});
            for (int i = 0; i < 8; ++i)
            {
                const float a = i * pi / 4;
                lobe({.24f*cosf(a),-.23f,.24f*sinf(a)}, {.022f,.30f+.05f*cosf(3*a),.022f});
            }
        }
        else if (species == Water::Crab)
        {
            lobe({0,0,0}, {.38f,.15f,.26f});
            for (float side : {-1.0f, 1.0f})
            {
                for (int i = 0; i < 4; ++i)
                    lobe({side*.41f,-.07f,i*.10f-.19f}, {.22f,.027f,.035f}, {0,side*(i*12-18),-side*20});
                lobe({side*.33f,.04f,.29f}, {.12f,.09f,.19f}, {0,side*25,0});
            }
        }
        else if (tail)
        {
            for (float side : {-1.0f, 1.0f})
            {
                if (species == Water::Whale)
                    lobe({side*.105f,0,-.065f}, {.16f,.021f,.056f}, {0,side*24,0});
                else
                    lobe({0,side*.09f,-.065f}, {.022f,.14f,.052f}, {-side*30,0,0});
            }
        }
        else
        {
            const bool fish = species == Water::Fish;
            lobe({0,0,.09f}, {fish ? .10f : .12f,fish ? .17f : .12f,.40f});
            lobe({0,0,-.23f}, {.062f,.072f,.21f});
            lobe({0,.12f,-.04f}, {.015f,species == Water::Shark ? .14f : .075f,.064f}, {-26,0,0});
            for (float side : {-1.0f, 1.0f})
                lobe({side*.13f,-.052f,.035f}, {.15f,.018f,.055f}, {0,side*28,-side*15});
        }
        auto mesh = std::make_shared<Mesh>();
        mesh->SetObjectName(std::string(habitats[species].name) + (tail ? " tail" : " body"));
        mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        mesh->AddGeometry(vertices, indices);
        mesh->CreateGpuBuffers();
        return mesh;
    }

    inline Entity* Spawn(Water::Species species, Entity* root, Vector3 position, float size)
    {
        Model& model = models[species];
        if (!model.body)
        {
            model.body = BuildMesh(species, false);
            if (species != Water::Jellyfish && species != Water::Crab) model.tail = BuildMesh(species, true);
            model.material = std::make_shared<Material>();
            model.material->SetPersistent(false);
            model.material->SetObjectName(std::string("marine_") + habitats[species].name);
            model.material->SetColor(habitats[species].color);
            model.material->SetProperty(MaterialProperty::Roughness, .42f);
        }
        auto part = [&](Entity* parent, const char* name, const std::shared_ptr<Mesh>& mesh, Vector3 local)
        {
            Entity* entity = World::CreateEntity();
            entity->SetTransient(true);
            entity->AddTag("dynamic");
            entity->SetObjectName(name);
            entity->SetParent(parent);
            entity->SetPositionLocal(local);
            entity->SetScaleLocal(Vector3::One);
            entity->SetRotationLocal(Quaternion::Identity);
            Render* render = entity->AddComponent<Render>();
            render->SetOwnedMesh(mesh);
            render->SetMaterial(model.material);
            render->SetMaxRenderDistance(160);
            render->SetFlag(RenderFlags::ExcludeFromRayTracing);
            return entity;
        };
        Entity* body = part(root, habitats[species].name, model.body, position);
        body->SetScaleLocal(Vector3::One * size);
        if (model.tail) part(body, "Tail", model.tail, {0,0,-.40f});
        return body;
    }

    inline void Tick(float dt)
    {
        const Water* water = Renderer::GetActiveOcean();
        Camera* camera = World::GetCamera();
        if (!water || !camera || !water->GetEntity()->GetActive() || water->GetBiome() == Water::None ||
            !water->GetSpeciesMask() || water->GetPopulation() <= 0)
        {
            if (root_id) Clear(true);
            return;
        }
        const uint64_t owner = water->GetEntity()->GetObjectId();
        if (ocean_id != owner) Clear(true);
        Entity* root = root_id ? World::GetEntityById(root_id) : nullptr;
        if (!root)
        {
            Clear(false);
            root = World::CreateEntity();
            root->SetTransient(true);
            root->SetObjectName("Marine life");
            root_id = root->GetObjectId();
            ocean_id = owner;
        }
        const Vector3 viewer = camera->GetEntity()->GetPosition();
        Terrain* terrain = Terrain::FindActive();
        if (terrain && !terrain->HasHeightfield()) return;
        const TerrainGridMapping grid = terrain ? terrain->GetGridMapping() : TerrainGridMapping{};
        const Matrix inverse = terrain ? terrain->GetEntity()->GetMatrix().Inverted() : Matrix::Identity;
        auto sample_bed = [&](const Vector3& position, float& bed)
        {
            if (!terrain) return false;
            // Terrain height queries clamp beyond the last grid cell. Open water has no seabed there.
            const Vector3 local = inverse * Vector3(position.x, 0, position.z);
            return fabsf(local.x) <= grid.offset_x && fabsf(local.z) <= grid.offset_z &&
                terrain->SampleHeight(position.x, position.z, bed);
        };
        const float sea = water->GetSeaLevel();
        const float layer = std::min(0.0f, viewer.y + 2 - sea);
        bool placed_group = false;
        dt = std::clamp(dt, 0.0f, .1f);
        auto random = [](float low, float high) { return std::uniform_real_distribution<float>(low, high)(generator); };

        for (uint32_t index = 0; index < Water::SpeciesCount; ++index)
        {
            auto species = static_cast<Water::Species>(index);
            const Habitat& habitat = habitats[index];
            const bool coastal = water->GetBiome() == Water::Coastal;
            const float abundance = water->GetPopulation();
            const int capacity = coastal ? habitat.coastal_groups : habitat.ocean_groups;
            const size_t count = water->GetSpeciesMask() & (1u << index) ? static_cast<size_t>(ceilf(capacity * abundance)) : 0;
            auto& population = groups[index];
            while (population.size() > count)
            {
                for (uint64_t id : population.back().members)
                    if (id) if (Entity* entity = World::GetEntityById(id)) World::RemoveEntity(entity);
                population.pop_back();
            }
            population.resize(count);
            for (Group& group : population)
            {
                Vector3 offset = group.center - viewer;
                offset.y = 0;
                if (offset.LengthSquared() > 110*110 || fabsf(layer - group.center.y) > 40) group.placed = false;
                if (!group.placed)
                {
                    for (uint64_t id : group.members)
                        if (id) if (Entity* entity = World::GetEntityById(id)) entity->SetActive(false);
                    group.retry -= dt;
                    if (group.retry > 0 || placed_group) continue;
                    group.retry = random(.7f, 1.6f);
                    const float angle = random(0, 2*pi);
                    const float range = random(5, species == Water::Whale ? 80.0f : 48.0f);
                    group.center = viewer + Vector3(cosf(angle)*range, 0, sinf(angle)*range);
                    group.center.y = layer;
                    group.phase = random(0, 2*pi);
                    group.placed = true;
                }
                const bool spawning = group.retry > 0;
                group.phase += dt * habitat.speed / habitat.orbit;
                bool occupied = false;
                for (int i = 0; i < habitat.members; ++i)
                {
                    const float angle = group.phase + .16f*i;
                    const float radius = habitat.orbit + .55f*(i%3);
                    Vector3 pos = group.center + Vector3(sinf(angle)*radius, 0, cosf(angle)*radius);
                    float bed = 0;
                    const bool grounded = sample_bed(pos, bed);
                    const float vertical_travel = species == Water::Whale || species == Water::Shark ? .89f : .48f;
                    const float depth = habitat.depth * (1 + vertical_travel*sinf(angle*2)) + .15f*(i%3);
                    pos.y = sea + group.center.y - depth;
                    if (species == Water::Crab) pos.y = bed + habitat.clearance;
                    Entity* entity = group.members[i] ? World::GetEntityById(group.members[i]) : nullptr;
                    if ((species == Water::Crab && (!grounded || sea-bed > 25 || sea-bed < .25f)) ||
                        (species != Water::Crab && grounded && pos.y < bed + habitat.clearance))
                    {
                        if (entity) entity->SetActive(false);
                        continue;
                    }
                    occupied = true;
                    const float size = habitat.length * (1-.05f*(i%4));
                    if (!entity)
                    {
                        entity = Spawn(species, root, pos, size);
                        group.members[i] = entity->GetObjectId();
                    }
                    entity->SetActive(true);
                    entity->SetPositionLocal(pos);
                    const float yaw = angle*rad_to_deg + (species == Water::Crab ? 0 : 90);
                    entity->SetRotationLocal(Quaternion::FromEulerAngles(0,yaw,0));
                    const float beat = sinf(group.phase * (species == Water::Fish ? 17 : 10) + i);
                    if (species == Water::Jellyfish)
                        entity->SetScaleLocal(Vector3(1+.10f*beat,1-.16f*beat,1+.10f*beat)*size);
                    else if (species != Water::Crab)
                        if (Entity* tail = entity->GetChildByIndex(0))
                            tail->SetRotationLocal(species == Water::Whale ? Quaternion::FromEulerAngles(18*beat,0,0) : Quaternion::FromEulerAngles(0,24*beat,0));
                }
                if (occupied && spawning)
                {
                    placed_group = true;
                    group.retry = 0;
                }
                group.placed = occupied;
                if (!occupied && group.retry <= 0) group.retry = random(.7f, 1.6f);
            }
        }
    }
}
