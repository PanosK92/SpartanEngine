/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Renderer.h"
#include "Material.h"
#include "../rhi/RHI_Texture.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Light.h"
#include "../world/components/Render.h"
#include <unordered_set>
#include <utility>
using namespace std;
using namespace spartan::math;
namespace spartan
{
    namespace
    {
        bool materials_invalidated = true, lights_invalidated = true;
        uint32_t last_global_revision = 0;
        uint64_t resource_poll_frame = 0;
        // material change tracking - things that change the nature of the material for rendering
        unordered_map<uint64_t, size_t> material_state_hashes;

        // light change tracking - things that change the nature of the light for rendering
        unordered_map<uint64_t, size_t> light_state_hashes;

        size_t compute_material_hash(Material* material)
        {
            // revision covers property/texture pointer edits, resource states catch async prep
            size_t hash = 17;
            hash = (hash * 31) ^ static_cast<size_t>(material->GetRevision());
            hash = (hash * 31) ^ static_cast<size_t>(material->GetResourceState());

            for (const auto* texture : material->GetTextures())
            {
                hash = (hash * 31) ^ reinterpret_cast<size_t>(texture);
                if (texture)
                {
                    hash = (hash * 31) ^ static_cast<size_t>(texture->GetResourceState());
                }
            }
            return hash;
        }

        size_t compute_light_hash(Light* light, Entity* entity)
        {
            size_t hash = 17;

            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().r);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().g);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().b);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetColor().a);
            hash = (hash * 31) ^ std::hash<float>{}(light->GetIntensityRadiometric());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetRange());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetAngle());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetAreaWidth());
            hash = (hash * 31) ^ std::hash<float>{}(light->GetAreaHeight());
            hash = (hash * 31) ^ static_cast<size_t>(light->GetLightType());
            hash = (hash * 31) ^ static_cast<size_t>(light->GetFlags());
            hash = (hash * 31) ^ static_cast<size_t>(entity->GetActive());

            const Vector3& pos = entity->GetPosition();
            hash = (hash * 31) ^ std::hash<float>{}(pos.x);
            hash = (hash * 31) ^ std::hash<float>{}(pos.y);
            hash = (hash * 31) ^ std::hash<float>{}(pos.z);
            const Vector3& fwd = entity->GetForward();
            hash = (hash * 31) ^ std::hash<float>{}(fwd.x);
            hash = (hash * 31) ^ std::hash<float>{}(fwd.y);
            hash = (hash * 31) ^ std::hash<float>{}(fwd.z);

            for (uint32_t i = 0; i < light->GetSliceCount(); i++)
            {
                const Matrix& view_projection = light->GetViewProjectionMatrix(i);
                const float* elements         = view_projection.Data();
                for (uint32_t j = 0; j < 16; j++)
                {
                    hash = (hash * 31) ^ std::hash<float>{}(elements[j]);
                }
            }

            return hash;
        }

    }
    void Renderer::ResetSceneChanges()
    {
        material_state_hashes.clear();
        light_state_hashes.clear();
        last_global_revision = 0;
        resource_poll_frame = 0;
        materials_invalidated = lights_invalidated = true;
    }
    bool Renderer::HaveMaterialsChangedThisFrame()
    {

        const uint32_t global_revision = Material::GetGlobalRevision();
        const bool props_changed = global_revision != last_global_revision;
        last_global_revision = global_revision;

        // property/texture pointer edits are covered by the global revision, async resource
        // states still need a periodic poll so bindless updates when gpu prep finishes
        resource_poll_frame++;
        const bool poll_resources = (resource_poll_frame % 8) == 0;
        const bool hashes_empty = material_state_hashes.empty() && !World::GetEntitiesWithRender().empty();
        if (!materials_invalidated && !props_changed && !poll_resources && !hashes_empty)
        {
            return false;
        }

        bool changed = std::exchange(materials_invalidated, false);
        unordered_set<uint64_t> seen;
        seen.reserve(World::GetEntitiesWithRender().size());

        for (Entity* entity : World::GetEntitiesWithRender())
        {
            if (!entity)
            {
                continue;
            }

            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            Material* material = render->GetMaterial();
            if (!material)
            {
                continue;
            }

            const uint64_t id = material->GetObjectId();
            if (!seen.insert(id).second)
            {
                continue;
            }

            size_t current_hash = compute_material_hash(material);
            auto it = material_state_hashes.find(id);
            if (it == material_state_hashes.end())
            {
                material_state_hashes[id] = current_hash;
                changed = true;
            }
            else if (it->second != current_hash)
            {
                it->second = current_hash;
                changed = true;
            }
        }

        changed |= std::erase_if(material_state_hashes, [&](const auto& entry) { return !seen.contains(entry.first); }) != 0;
        return changed;
    }

    bool Renderer::HaveLightsChanged()
    {

        bool changed = std::exchange(lights_invalidated, false);
        unordered_set<uint64_t> seen;
        for (Entity* entity : World::GetEntitiesLights())
        {
            if (Light* light = entity->GetComponent<Light>())
            {
                const uint64_t id   = entity->GetObjectId();
                seen.insert(id);
                size_t current_hash = compute_light_hash(light, entity);
                auto it = light_state_hashes.find(id);
                if (it == light_state_hashes.end())
                {
                    light_state_hashes[id] = current_hash;
                    changed = true;
                }
                else if (it->second != current_hash)
                {
                    it->second = current_hash;
                    changed = true;
                }
            }
        }

        changed |= std::erase_if(light_state_hashes, [&](const auto& entry) { return !seen.contains(entry.first); }) != 0;
        return changed;
    }

}
