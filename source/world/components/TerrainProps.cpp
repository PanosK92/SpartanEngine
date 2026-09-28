/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES =================================
#include "pch.h"
#include <filesystem>
#include <cctype>
#include "../../profiling/Profiler.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <functional>
#include <unordered_set>
#include "Terrain.h"
#include "TerrainCommon.h"
#include "../../rhi/RHI_Buffer.h"
#include "../../rhi/RHI_CommandList.h"
#include "../TerrainHabitat.h"
#include "Render.h"
#include "Physics.h"
#include "Water.h"
#include "Spline.h"
#include "../../geometry/GeneratedCache.h"
#include "RoadEditRegions.h"
#include "Light.h"
#include "Camera.h"
#include "../Entity.h"
#include "../World.h"
#include "../TerrainSystem.h"
#include "../TerrainPlacement.h"
#include "../../rhi/RHI_Texture.h"
#include "../../resource/ResourceCache.h"
#include "../../geometry/Mesh.h"
#include "../../rendering/Material.h"
#include "../../rendering/Color.h"
#include "../../rendering/Renderer.h"
#include "../../geometry/GeometryProcessing.h"
#include "../../core/ThreadPool.h"
#include "../../core/ProgressTracker.h"
#include "../../core/Engine.h"
#include "../../core/Timer.h"
#include "../../file_system/FileSystem.h"
#include "../../physics/PhysicsWorld.h"
#include "../../math/Ray.h"
#include "../../math/BoundingBox.h"
#include "../WorldHelpers.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    using namespace terrain_common;

    namespace
    {
        // cpu twin of noise_perlin in common.hlsl, the same texture read bilinear with wrap and the
        // red channel mapped to -1..1, so the prop mask scores with the exact jitter the surface sees
        struct perlin_sampler
        {
            const uint8_t* bytes = nullptr;
            uint32_t width       = 0;
            uint32_t height      = 0;
            uint32_t stride      = 0;

            bool valid() const
            {
                return bytes != nullptr && width > 0 && height > 0 && stride > 0;
            }

            float texel(int32_t x, int32_t y) const
            {
                x = ((x % static_cast<int32_t>(width))  + static_cast<int32_t>(width))  % static_cast<int32_t>(width);
                y = ((y % static_cast<int32_t>(height)) + static_cast<int32_t>(height)) % static_cast<int32_t>(height);
                return static_cast<float>(bytes[(static_cast<size_t>(y) * width + static_cast<size_t>(x)) * stride]) * (1.0f / 255.0f);
            }

            float sample(float u, float v) const
            {
                if (!valid())
                {
                    return 0.0f;
                }

                const float px = u * static_cast<float>(width) - 0.5f;
                const float py = v * static_cast<float>(height) - 0.5f;
                const int32_t x0 = static_cast<int32_t>(floorf(px));
                const int32_t y0 = static_cast<int32_t>(floorf(py));
                const float fx = px - static_cast<float>(x0);
                const float fy = py - static_cast<float>(y0);

                const float a = texel(x0, y0);
                const float b = texel(x0 + 1, y0);
                const float c = texel(x0, y0 + 1);
                const float d = texel(x0 + 1, y0 + 1);
                const float value = lerp(lerp(a, b, fx), lerp(c, d, fx), fy);
                return value * 2.0f - 1.0f;
            }

            float noise(float x, float y) const
            {
                return sample(x * 0.1f, y * 0.1f);
            }
        };

        perlin_sampler make_perlin_sampler()
        {
            perlin_sampler sampler;
            RHI_Texture* texture = Renderer::GetStandardTexture(Renderer_StandardTexture::Noise_perlin);
            if (!texture || !texture->HasData() || texture->GetBitsPerChannel() != 8)
            {
                return sampler;
            }

            RHI_Texture_Mip* mip = texture->GetMip(0, 0);
            if (!mip || mip->bytes.empty())
            {
                return sampler;
            }

            sampler.bytes  = reinterpret_cast<const uint8_t*>(mip->bytes.data());
            sampler.width  = texture->GetWidth();
            sampler.height = texture->GetHeight();
            sampler.stride = max(texture->GetChannelCount(), 1u);
            if (mip->bytes.size() < static_cast<size_t>(sampler.width) * sampler.height * sampler.stride)
            {
                sampler = perlin_sampler();
            }

            return sampler;
        }

        // terrain_jitter in common_terrain.hlsl, the constants must move together
        float terrain_jitter(const perlin_sampler& perlin, float world_x, float world_z, float seed)
        {
            const float px = world_x * 10.0f;
            const float pz = world_z * 10.0f;
            float n  = perlin.noise(px * (1.0f / 700.0f) + seed * 0.4f, pz * (1.0f / 700.0f) + seed * 0.4f) * 1.1f;
            n       += perlin.noise(px * (1.0f / 250.0f) + seed,        pz * (1.0f / 250.0f) + seed);
            n       += perlin.noise(px * (1.0f / 77.0f)  + seed * 1.7f, pz * (1.0f / 77.0f)  + seed * 1.7f) * 0.5f;
            n       += perlin.noise(px * (1.0f / 24.0f)  + seed * 2.9f, pz * (1.0f / 24.0f)  + seed * 2.9f) * 0.25f;
            return n / 2.85f;
        }
    }

    void Terrain::PunchPropMaskFootprint(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw
    )
    {
        if (m_prop_mask_pixels.empty() || m_map_width < 2 || m_map_height < 2)
        {
            return;
        }

        if (fabsf(m_world_mapping.z) < 1e-12f || fabsf(m_world_mapping.w) < 1e-12f)
        {
            return;
        }

        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(center_x, center_z, half_x, half_z, yaw, min_x, min_z, max_x, max_z);

        auto world_to_pixel = [&](float world, float origin, float inv_size, uint32_t resolution) -> int
        {
            const float u = (world - origin) * inv_size;
            return static_cast<int>(clamp(u, 0.0f, 1.0f) * static_cast<float>(resolution - 1) + 0.5f);
        };

        const Vector4 mapping = GetMappingWorld();
        const int x0 = world_to_pixel(min_x, mapping.x, mapping.z, m_map_width);
        const int x1 = world_to_pixel(max_x, mapping.x, mapping.z, m_map_width);
        const int z0 = world_to_pixel(min_z, mapping.y, mapping.w, m_map_height);
        const int z1 = world_to_pixel(max_z, mapping.y, mapping.w, m_map_height);

        const int x_min = min(x0, x1);
        const int x_max = max(x0, x1);
        const int z_min = min(z0, z1);
        const int z_max = max(z0, z1);

        for (int z = z_min; z <= z_max; z++)
        {
            const float v       = static_cast<float>(z) / static_cast<float>(m_map_height - 1);
            const float world_z = mapping.y + v / mapping.w;
            for (int x = x_min; x <= x_max; x++)
            {
                const float u       = static_cast<float>(x) / static_cast<float>(m_map_width - 1);
                const float world_x = mapping.x + u / mapping.z;
                if (obb_outside_distance(world_x, world_z, center_x, center_z, half_x, half_z, yaw) > 0.0f)
                {
                    continue;
                }

                const size_t offset = (static_cast<size_t>(z) * m_map_width + static_cast<size_t>(x)) * 4;
                m_prop_mask_pixels[offset + 0] = 0;
                m_prop_mask_pixels[offset + 1] = 0;
                m_prop_mask_pixels[offset + 2] = 0;
            }
        }

        MarkPropMaskDirty(x_min, z_min, x_max, z_max);
    }

    void Terrain::RestorePropMaskFootprint(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw
    )
    {
        if (m_prop_mask_pixels.empty() || m_prop_mask_seed.size() != m_prop_mask_pixels.size())
        {
            return;
        }

        if (m_map_width < 2 || m_map_height < 2)
        {
            return;
        }

        if (fabsf(m_world_mapping.z) < 1e-12f || fabsf(m_world_mapping.w) < 1e-12f)
        {
            return;
        }

        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(center_x, center_z, half_x, half_z, yaw, min_x, min_z, max_x, max_z);

        auto world_to_pixel = [&](float world, float origin, float inv_size, uint32_t resolution) -> int
        {
            const float u = (world - origin) * inv_size;
            return static_cast<int>(clamp(u, 0.0f, 1.0f) * static_cast<float>(resolution - 1) + 0.5f);
        };

        const Vector4 mapping = GetMappingWorld();
        const int x0 = world_to_pixel(min_x, mapping.x, mapping.z, m_map_width);
        const int x1 = world_to_pixel(max_x, mapping.x, mapping.z, m_map_width);
        const int z0 = world_to_pixel(min_z, mapping.y, mapping.w, m_map_height);
        const int z1 = world_to_pixel(max_z, mapping.y, mapping.w, m_map_height);

        const int x_min = min(x0, x1);
        const int x_max = max(x0, x1);
        const int z_min = min(z0, z1);
        const int z_max = max(z0, z1);

        for (int z = z_min; z <= z_max; z++)
        {
            const float v       = static_cast<float>(z) / static_cast<float>(m_map_height - 1);
            const float world_z = mapping.y + v / mapping.w;
            for (int x = x_min; x <= x_max; x++)
            {
                const float u       = static_cast<float>(x) / static_cast<float>(m_map_width - 1);
                const float world_x = mapping.x + u / mapping.z;
                if (obb_outside_distance(world_x, world_z, center_x, center_z, half_x, half_z, yaw) > 0.0f)
                {
                    continue;
                }

                const size_t offset = (static_cast<size_t>(z) * m_map_width + static_cast<size_t>(x)) * 4;
                m_prop_mask_pixels[offset + 0] = m_prop_mask_seed[offset + 0];
                m_prop_mask_pixels[offset + 1] = m_prop_mask_seed[offset + 1];
                m_prop_mask_pixels[offset + 2] = m_prop_mask_seed[offset + 2];
            }
        }

        MarkPropMaskDirty(x_min, z_min, x_max, z_max);
    }

    void Terrain::ReapplyPropMaskHoles()
    {
        // a full mask bake starts from the analysis alone, it becomes the new seed and every hole
        // that roads and pads punched has to be stamped on top of it again, the road stamp runs
        // through its own dirty path on the next tick
        m_prop_mask_seed = m_prop_mask_pixels;
        m_spline_carve_dirty     = true;

        for (const TerrainPlatform& pad : m_platforms)
        {
            PunchPropMaskFootprint(pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw);
        }
        if (m_live_pad_active)
        {
            PunchPropMaskFootprint(m_live_pad.center_x, m_live_pad.center_z, m_live_pad.half_x, m_live_pad.half_z, m_live_pad.yaw);
        }
    }

    void Terrain::MarkPropMaskDirty(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
    {
        if (m_map_width == 0 || m_map_height == 0)
        {
            return;
        }

        x0 = clamp(x0, 0, static_cast<int32_t>(m_map_width) - 1);
        x1 = clamp(x1, 0, static_cast<int32_t>(m_map_width) - 1);
        z0 = clamp(z0, 0, static_cast<int32_t>(m_map_height) - 1);
        z1 = clamp(z1, 0, static_cast<int32_t>(m_map_height) - 1);
        m_prop_mask_dirty.Merge(x0, z0, x1, z1);
    }

    bool Terrain::UploadPropMask()
    {
        if (m_prop_mask_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            m_prop_mask_dirty.Clear();
            return false;
        }

        // patch the live texture when only a footprint changed, a full recreate stalls on a big map
        const bool texture_fits =
            m_prop_mask &&
            m_prop_mask->GetWidth() == m_map_width &&
            m_prop_mask->GetHeight() == m_map_height &&
            m_prop_mask->GetRhiResource() != nullptr;
        if (texture_fits)
        {
            if (m_prop_mask_dirty.IsEmpty())
            {
                return false;
            }

            const TerrainDirtyRect rect = m_prop_mask_dirty;
            const uint32_t width  = static_cast<uint32_t>(rect.x1 - rect.x0 + 1);
            const uint32_t height = static_cast<uint32_t>(rect.z1 - rect.z0 + 1);

            vector<uint8_t> pixels(static_cast<size_t>(width) * height * 4);
            for (int32_t z = rect.z0; z <= rect.z1; z++)
            {
                const uint8_t* src = m_prop_mask_pixels.data() + (static_cast<size_t>(z) * m_map_width + rect.x0) * 4;
                uint8_t* dst       = pixels.data() + static_cast<size_t>(z - rect.z0) * width * 4;
                memcpy(dst, src, static_cast<size_t>(width) * 4);
            }

            if (m_prop_mask->UpdateRegion(static_cast<uint32_t>(rect.x0), static_cast<uint32_t>(rect.z0), width, height, pixels.data()))
            {
                m_prop_mask_dirty.Clear();
                return false;
            }
        }

        m_prop_mask_dirty.Clear();
        m_prop_mask_retired = m_prop_mask;
        m_prop_mask = make_shared<RHI_Texture>(
            RHI_Texture_Type::Type2D,
            m_map_width, m_map_height, 1, 1,
            RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Srv,
            "terrain_prop_mask", to_single_mip_slice(m_prop_mask_pixels)
        );
        m_prop_mask->PrepareForGpu();
        return true;
    }

    void Terrain::SnapshotPropInstances()
    {
        m_prop_instance_seed.clear();
        m_prop_entity_seed.clear();
        if (!m_entity_ptr)
        {
            return;
        }

        SnapshotPropSeedsUnder(m_entity_ptr, false);
    }

    void Terrain::SnapshotPropSeedsUnder(Entity* root, bool inside_prop)
    {
        function<void(Entity*, bool)> visit = [&](Entity* entity, bool in_prop)
        {
            if (!entity)
            {
                return;
            }

            const bool prop = in_prop || entity->HasTag("terrain_prop");
            if (prop)
            {
                if (Render* render = entity->GetComponent<Render>())
                {
                    if (render->HasInstancing())
                    {
                        vector<Matrix> transforms;
                        const uint32_t count = render->GetInstanceCount();
                        transforms.reserve(count);
                        for (uint32_t i = 0; i < count; i++)
                        {
                            transforms.push_back(render->GetInstance(i, false));
                        }
                        m_prop_instance_seed[entity->GetObjectId()] = move(transforms);
                    }
                    else if (entity->GetChildrenCount() == 0)
                    {
                        m_prop_entity_seed[entity->GetObjectId()] = entity->IsActive();
                    }
                }
            }

            const uint32_t child_count = entity->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                visit(entity->GetChildByIndex(i), prop);
            }
        };

        visit(root, inside_prop);
    }

    void Terrain::ForgetPropSeeds(Entity* prop_root)
    {
        if (!prop_root)
        {
            return;
        }

        vector<Entity*> parts;
        parts.push_back(prop_root);
        prop_root->GetDescendants(&parts);
        for (Entity* part : parts)
        {
            if (!part)
            {
                continue;
            }

            m_prop_instance_seed.erase(part->GetObjectId());
            m_prop_entity_seed.erase(part->GetObjectId());
        }
    }

    void Terrain::CollectPropRendersInWorldRect(
        float world_min_x,
        float world_min_z,
        float world_max_x,
        float world_max_z,
        bool cull_by_bounds,
        vector<Entity*>& props_out
    )
    {
        if (!m_entity_ptr)
        {
            return;
        }

        // props hang under their tile, so the tile grid is the first cull, then each renderer's own aabb
        // a restore cannot use the aabb, the instances it brings back are not in the current bounds
        unordered_set<uint32_t> tiles;
        CollectTilesInWorldRect(world_min_x, world_min_z, world_max_x, world_max_z, tiles);

        auto overlaps_rect = [&](const BoundingBox& box) -> bool
        {
            const Vector3& lo = box.GetMin();
            const Vector3& hi = box.GetMax();
            return hi.x >= world_min_x && lo.x <= world_max_x && hi.z >= world_min_z && lo.z <= world_max_z;
        };

        function<void(Entity*, bool)> visit = [&](Entity* entity, bool inside_prop)
        {
            if (!entity)
            {
                return;
            }

            const bool prop = inside_prop || entity->HasTag("terrain_prop");
            if (prop)
            {
                if (Render* render = entity->GetComponent<Render>())
                {
                    if (!cull_by_bounds || overlaps_rect(render->GetBoundingBox()))
                    {
                        props_out.push_back(entity);
                    }
                }
            }

            const uint32_t child_count = entity->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                visit(entity->GetChildByIndex(i), prop);
            }
        };

        for (Entity* child : m_entity_ptr->GetChildren())
        {
            const int tile_index = ParseTileIndex(child);
            if (tile_index >= 0)
            {
                if (tiles.find(static_cast<uint32_t>(tile_index)) != tiles.end())
                {
                    visit(child, false);
                }
                continue;
            }

            visit(child, false);
        }
    }

    void Terrain::ClearFootprintProps(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw
    )
    {
        if (!m_entity_ptr)
        {
            return;
        }

        if (m_prop_instance_seed.empty() && m_prop_entity_seed.empty())
        {
            SnapshotPropInstances();
        }

        auto on_pad = [&](float x, float z) -> bool
        {
            return obb_outside_distance(x, z, center_x, center_z, half_x, half_z, yaw) <= 0.0f;
        };

        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(center_x, center_z, half_x, half_z, yaw, min_x, min_z, max_x, max_z);
        vector<Entity*> props;
        CollectPropRendersInWorldRect(min_x, min_z, max_x, max_z, true, props);

        for (Entity* entity : props)
        {
            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            if (render->HasInstancing())
            {
                // one world matrix per renderer, the per instance world decode is what made this crawl
                const Matrix world = entity->GetMatrix();
                vector<Matrix> kept;
                const uint32_t count = render->GetInstanceCount();
                kept.reserve(count);
                for (uint32_t i = 0; i < count; i++)
                {
                    const Matrix local     = render->GetInstance(i, false);
                    const Vector3 position = (local * world).GetTranslation();
                    if (!on_pad(position.x, position.z))
                    {
                        kept.push_back(local);
                    }
                }

                if (kept.size() != count)
                {
                    render->SetInstances(kept);
                }
            }
            else if (entity->GetChildrenCount() == 0 && on_pad(entity->GetPosition().x, entity->GetPosition().z))
            {
                entity->SetActive(false);
            }
        }
    }

    void Terrain::RestoreFootprintProps(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw
    )
    {
        if (!m_entity_ptr)
        {
            return;
        }

        if (m_prop_instance_seed.empty() && m_prop_entity_seed.empty())
        {
            return;
        }

        auto on_pad = [&](float x, float z) -> bool
        {
            return obb_outside_distance(x, z, center_x, center_z, half_x, half_z, yaw) <= 0.0f;
        };

        // the current instances miss what the pad hid, so the cull has to run on the seed bounds too,
        // the tile grid alone is enough for that, every seed instance lives under its tile
        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(center_x, center_z, half_x, half_z, yaw, min_x, min_z, max_x, max_z);
        vector<Entity*> props;
        CollectPropRendersInWorldRect(min_x, min_z, max_x, max_z, false, props);

        for (Entity* entity : props)
        {
            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            const uint64_t id = entity->GetObjectId();
            auto seed = m_prop_instance_seed.find(id);
            if (seed != m_prop_instance_seed.end())
            {
                const Matrix world = entity->GetMatrix();
                bool touches = false;
                for (const Matrix& local : seed->second)
                {
                    const Vector3 position = (local * world).GetTranslation();
                    if (on_pad(position.x, position.z))
                    {
                        touches = true;
                        break;
                    }
                }

                if (!touches)
                {
                    continue;
                }

                vector<Matrix> kept;
                if (render->HasInstancing())
                {
                    const uint32_t count = render->GetInstanceCount();
                    kept.reserve(count + seed->second.size());
                    for (uint32_t i = 0; i < count; i++)
                    {
                        const Matrix local     = render->GetInstance(i, false);
                        const Vector3 position = (local * world).GetTranslation();
                        if (!on_pad(position.x, position.z))
                        {
                            kept.push_back(local);
                        }
                    }
                }
                else
                {
                    kept.reserve(seed->second.size());
                }

                for (const Matrix& local : seed->second)
                {
                    const Vector3 position = (local * world).GetTranslation();
                    // The seed predates road filtering. Restoring a building's old
                    // footprint must not bring vegetation back onto a carriageway.
                    if (on_pad(position.x, position.z) && !IsOnRoad(position.x, position.z))
                    {
                        kept.push_back(local);
                    }
                }

                render->SetInstances(kept);
            }
            else if (entity->GetChildrenCount() == 0)
            {
                auto active = m_prop_entity_seed.find(id);
                if (active != m_prop_entity_seed.end() &&
                    on_pad(entity->GetPosition().x, entity->GetPosition().z))
                {
                    entity->SetActive(active->second && !IsOnRoad(entity->GetPosition().x, entity->GetPosition().z));
                }
            }
        }
    }

    void Terrain::BakePropMask()
    {
        if (m_map_a_pixels.empty() || m_map_b_pixels.empty() || m_positions.empty() ||
            m_map_width == 0 || m_map_height == 0 || m_dense_width < 2 || m_dense_height < 2)
        {
            m_prop_mask_pixels.clear();
            m_layer_dominant.clear();
            return;
        }

        const Stopwatch bake_timer;
        const size_t cell_count = static_cast<size_t>(m_map_width) * m_map_height;
        // Cache the unpunched mask. Road/building holes are reapplied by the
        // caller, and live brush edits retain their existing regional repair path.
        generated_cache::Hash key;
        filesystem::path cache_path;
        if (World::IsLoadingFromFile())
        {
            key.Add(uint32_t{1}); // BakePropMaskCells and terrain habitat algorithm version
            key.Add(m_map_width); key.Add(m_map_height);
            key.Add(m_dense_width); key.Add(m_dense_height);
            key.Add(m_density); key.Add(m_scale); key.Add(m_layer_quality);
            key.Add(GetSeaLevelLocal()); key.Add(GetSnowLevelLocal()); key.Add(m_snow_amount);
            key.Add(World::GetWind());
            key.Add(GetEntity() ? GetEntity()->GetMatrix().GetTranslation() : Vector3::Zero);
            key.Add(generated_cache::HashBytes(m_positions.data(), m_positions.size() * sizeof(Vector3)));
            key.Add(generated_cache::HashBytes(m_map_a_pixels.data(), m_map_a_pixels.size()));
            key.Add(generated_cache::HashBytes(m_map_b_pixels.data(), m_map_b_pixels.size()));
            const perlin_sampler noise = make_perlin_sampler();
            key.Add(noise.width); key.Add(noise.height); key.Add(noise.stride);
            if (noise.valid()) key.Add(generated_cache::HashBytes(noise.bytes, size_t(noise.width) * noise.height * noise.stride));
            for (uint32_t i = 0; i < terrain_layer_max; ++i)
            {
                const auto& rule = m_layer_rules[i];
                key.Add(IsLayerEnabled(i)); key.Add(rule.name); key.Add(rule.flags);
                key.Add(rule.slope_min); key.Add(rule.slope_max); key.Add(rule.height_min); key.Add(rule.height_max);
                key.Add(rule.curvature_influence); key.Add(rule.flow_influence); key.Add(rule.occlusion_influence);
                key.Add(rule.insolation_influence); key.Add(rule.wear_influence); key.Add(rule.deposition_influence);
                key.Add(rule.talus_influence); key.Add(rule.weight_bias);
            }
            cache_path = generated_cache::Path(World::GetResourceDirectory(), "prop_masks", key.value);
        }
        const bool cached = !cache_path.empty() && generated_cache::Load(cache_path, key.value, m_prop_mask_pixels, m_layer_dominant) &&
            m_prop_mask_pixels.size() == cell_count * 4 && m_layer_dominant.size() == cell_count;
        if (!cached)
        {
            m_prop_mask_pixels.resize(cell_count * 4);
            m_layer_dominant.resize(cell_count);
            BakePropMaskCells(0, 0, static_cast<int32_t>(m_map_width) - 1, static_cast<int32_t>(m_map_height) - 1);
            if (!cache_path.empty()) generated_cache::Save(cache_path, key.value, m_prop_mask_pixels, m_layer_dominant);
        }
        SP_LOG_INFO("Vegetation mask: %s, %.2f ms", cached ? "cache hit" : "baked", bake_timer.GetElapsedTimeMs());

        uint32_t grass_hits = 0;
        uint32_t tree_hits  = 0;
        uint32_t rock_hits  = 0;
        for (size_t i = 0; i < cell_count; i++)
        {
            const size_t offset = i * 4;
            if (m_prop_mask_pixels[offset + 0] > 40)
            {
                grass_hits++;
            }
            if (m_prop_mask_pixels[offset + 1] > 20)
            {
                tree_hits++;
            }
            if (m_prop_mask_pixels[offset + 2] > 20)
            {
                rock_hits++;
            }
        }
        const float inv_cells = 100.0f / max(static_cast<float>(cell_count), 1.0f);
        SP_LOG_INFO(
            "prop mask: grass %.1f%%, trees %.1f%%, rocks %.1f%%",
            static_cast<float>(grass_hits) * inv_cells,
            static_cast<float>(tree_hits) * inv_cells,
            static_cast<float>(rock_hits) * inv_cells
        );
    }

    bool Terrain::BakePropMaskCells(int32_t mx0, int32_t mz0, int32_t mx1, int32_t mz1)
    {
        const size_t cell_count = static_cast<size_t>(m_map_width) * m_map_height;
        if (m_map_a_pixels.size() != cell_count * 4 || m_map_b_pixels.size() != cell_count * 4 ||
            m_prop_mask_pixels.size() != cell_count * 4 || m_layer_dominant.size() != cell_count ||
            m_positions.size() != static_cast<size_t>(m_dense_width) * m_dense_height ||
            m_dense_width < 2 || m_dense_height < 2)
        {
            return false;
        }

        mx0 = clamp(mx0, 0, static_cast<int32_t>(m_map_width) - 1);
        mx1 = clamp(mx1, 0, static_cast<int32_t>(m_map_width) - 1);
        mz0 = clamp(mz0, 0, static_cast<int32_t>(m_map_height) - 1);
        mz1 = clamp(mz1, 0, static_cast<int32_t>(m_map_height) - 1);
        if (mx1 < mx0 || mz1 < mz0)
        {
            return false;
        }

        // cpu port of terrain_layer_weight, scores each biome then packs grass/tree/rock channels
        // the constants below mirror common_terrain.hlsl, they must move together or the props end up
        // scattered over ground the surface shader painted as something else
        const float slope_domain_min   = 0.0f;
        const float slope_domain_max   = 1.5698f;
        const float height_domain_min  = -99999.0f;
        const float height_domain_max  = 99999.0f;
        const float slope_feather_max  = 0.35f;
        const float height_feather_max = 6.0f;
        const float blend_width_world  = 14.0f;
        const float gradient_max       = 8.0f;

        auto band = [](float value, float low, float high, float feather_max,
            float domain_min, float domain_max) -> float
        {
            const bool open_low  = low  <= domain_min;
            const bool open_high = high >= domain_max;

            const float span    = (open_low || open_high) ? (feather_max * 2.0f) : (high - low);
            const float feather = max(min(span, feather_max), 1e-4f);

            auto smooth = [](float t) -> float
            {
                return t * t * (3.0f - 2.0f * t);
            };

            const float rise = open_low ?
                1.0f : smooth(saturate((value - (low - feather)) / max(2.0f * feather, 1e-4f)));
            const float fall = open_high ?
                1.0f : smooth(1.0f - saturate((value - (high - feather)) / max(2.0f * feather, 1e-4f)));

            return rise * fall;
        };

        // the same threshold noise the surface shader applies, without it the mask edges trace
        // contour lines while the texture edges grow fingers, and the props end up on the wrong side
        const perlin_sampler perlin = make_perlin_sampler();
        const float slope_jitter    = 0.22f;
        const float height_jitter   = 4.0f;
        const float sea_local       = GetSeaLevelLocal();
        const float snow_local      = GetSnowLevelLocal();
        const Vector3 wind          = World::GetWind();
        const float wind_length     = wind.Length();
        Vector3 translation         = Vector3::Zero;
        if (Entity* entity = GetEntity())
        {
            translation = entity->GetMatrix().GetTranslation();
        }

        auto layer_weight = [&](const TerrainLayerRule& rule, float height, float slope_rad, float world_x, float world_z,
            const Vector3& normal, float jitter,
            float curvature, float flow, float occlusion, float deposition,
            float wear, float insolation, float talus) -> float
        {
            if (rule.weight_bias <= 0.0f)
            {
                return 0.0f;
            }

            const float slope_jittered = slope_rad + jitter * slope_jitter;

            // terrain_snow_weight, the accumulation model capped by the authored slope band
            if (rule.flags & TerrainLayerFlags_Snow)
            {
                if (m_snow_amount <= 0.0f)
                {
                    return 0.0f;
                }

                const float snow_line = snow_local + jitter * 70.0f;
                const float altitude  = height - snow_line;
                float snow = saturate(altitude / 130.0f);
                snow *= powf(saturate(normal.y), 2.5f);
                snow *= lerp(0.55f, 1.15f, curvature);
                snow *= lerp(0.65f, 1.1f, 1.0f - occlusion);
                if (wind_length > 0.001f)
                {
                    snow *= lerp(1.0f, 0.4f, saturate(Vector3::Dot(normal, wind / wind_length)));
                }

                const float slope_band = band(
                    slope_jittered,
                    rule.slope_min * math::deg_to_rad,
                    rule.slope_max * math::deg_to_rad,
                    slope_feather_max,
                    slope_domain_min,
                    slope_domain_max
                );
                return snow * m_snow_amount * slope_band * rule.weight_bias;
            }

            const float height_for_band = (rule.flags & TerrainLayerFlags_BelowSea) ?
                (height - sea_local) : height;
            const float height_jittered = height_for_band + jitter * height_jitter;

            // altitude gained per metre travelled horizontally, a feather written in altitude covers
            // less and less ground as the slope steepens, which is what turns a boundary into a knife
            // edge, scaling by the gradient holds the width along the surface instead
            const float normal_y       = max(normal.y, 0.125f);
            const float gradient       = min(sqrtf(saturate(1.0f - normal_y * normal_y)) / normal_y, gradient_max);
            const float height_feather = max(blend_width_world * gradient, height_feather_max * 0.25f);

            float weight = band(
                slope_jittered,
                rule.slope_min * math::deg_to_rad,
                rule.slope_max * math::deg_to_rad,
                slope_feather_max,
                slope_domain_min,
                slope_domain_max
            );
            weight *= band(
                height_jittered,
                rule.height_min,
                rule.height_max,
                height_feather,
                height_domain_min,
                height_domain_max
            );
            if (weight <= 0.0f)
            {
                return 0.0f;
            }

            float push = 0.0f;
            push += rule.curvature_influence  * (curvature * 2.0f - 1.0f);
            push += rule.flow_influence       * (flow * 2.0f - 1.0f);
            push += rule.occlusion_influence  * (1.0f - occlusion * 2.0f);
            push += rule.insolation_influence * (insolation * 2.0f - 1.0f);
            push += rule.wear_influence       * (wear * 2.0f - 1.0f);
            push += rule.deposition_influence * (deposition * 2.0f - 1.0f);
            push += rule.talus_influence      * (talus * 2.0f - 1.0f);

            if (rule.flags & (TerrainLayerFlags_Woodland | TerrainLayerFlags_Open))
            {
                const float woodland = terrain_habitat_shared::habitat_woodland(world_x, world_z,
                    height - sea_local, slope_rad * math::rad_to_deg, insolation, deposition);
                if (rule.flags & TerrainLayerFlags_Woodland) weight *= woodland;
                if (rule.flags & TerrainLayerFlags_Open) weight *= 1.0f - woodland * 0.8f;
            }
            return weight * exp2f(clamp(push, -1.0f, 1.0f) * 1.25f) * rule.weight_bias;
        };

        // find layer indices by name so a reordered rule table still maps correctly
        int grass_i  = -1;
        int rock_i   = -1;
        int gravel_i = -1;
        int forest_i = -1;
        int sand_i   = -1;
        int dirt_i   = -1;
        int snow_i   = -1;
        int moss_i   = -1;
        for (uint32_t i = 0; i < terrain_layer_max; i++)
        {
            if (m_layer_rules[i].name == "whispy_grass_meadow") grass_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "rock") rock_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "gravel") gravel_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "forest_floor") forest_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "sand") sand_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "dirt") dirt_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "snow") snow_i = static_cast<int>(i);
            if (m_layer_rules[i].name == "moss") moss_i = static_cast<int>(i);
        }

        const uint32_t dense_w = m_dense_width;
        const uint32_t dense_h = m_dense_height;
        const TerrainGridMapping mapping = GetGridMapping();

        const uint32_t span_x = static_cast<uint32_t>(mx1 - mx0 + 1);
        const uint32_t span_z = static_cast<uint32_t>(mz1 - mz0 + 1);

        auto bake = [&](uint32_t start, uint32_t end)
        {
            for (uint32_t k = start; k < end; k++)
            {
                const uint32_t ax = static_cast<uint32_t>(mx0) + k % span_x;
                const uint32_t az = static_cast<uint32_t>(mz0) + k / span_x;
                const uint32_t i  = az * m_map_width + ax;

                // Analysis maps and the shader span n-1 intervals. Texel-centred nearest-cell
                // sampling shifted these masks and flattened the positive terrain edges.
                Vector3 pos;
                pos.x = lerp(m_positions.front().x, m_positions.back().x, static_cast<float>(ax) / max(m_map_width - 1u, 1u));
                pos.z = lerp(m_positions.front().z, m_positions.back().z, static_cast<float>(az) / max(m_map_height - 1u, 1u));
                const float y_c = TerrainSystem::SampleHeight(m_positions, dense_w, dense_h, pos.x, pos.z, mapping);
                pos.y = y_c;
                const Vector3 normal = TerrainSystem::SampleNormal(m_positions, dense_w, dense_h, pos.x, pos.z, mapping);
                const float slope = acosf(clamp(normal.y, -1.0f, 1.0f));

                // the shader jitters in world xz
                const float world_x = pos.x + translation.x;
                const float world_z = pos.z + translation.z;
                const float jitter  = terrain_jitter(perlin, world_x, world_z, 0.0f);

                const size_t offset = static_cast<size_t>(i) * 4;
                const float curvature   = m_map_a_pixels[offset + 0] * (1.0f / 255.0f);
                const float flow        = m_map_a_pixels[offset + 1] * (1.0f / 255.0f);
                const float occlusion   = m_map_a_pixels[offset + 2] * (1.0f / 255.0f);
                const float deposition  = m_map_a_pixels[offset + 3] * (1.0f / 255.0f);
                const float wear        = m_map_b_pixels[offset + 0] * (1.0f / 255.0f);
                const float insolation  = m_map_b_pixels[offset + 1] * (1.0f / 255.0f);
                const float talus       = m_map_b_pixels[offset + 3] * (1.0f / 255.0f);

                auto score = [&](int layer_index) -> float
                {
                    if (layer_index < 0)
                    {
                        return 0.0f;
                    }
                    if (!IsLayerEnabled(static_cast<uint32_t>(layer_index)))
                    {
                        return 0.0f;
                    }
                    return layer_weight(
                        m_layer_rules[static_cast<size_t>(layer_index)],
                        y_c, slope, world_x, world_z, normal, jitter,
                        curvature, flow, occlusion, deposition, wear, insolation, talus
                    );
                };

                // same pick as the surface shader, sand dirt and snow keep their share so grass
                // cannot inherit ground it does not own, the per layer breakup is seeded by the
                // layer's position in the shader block, which only counts the enabled layers
                float scores[terrain_layer_max];
                uint32_t shader_index = 0;
                for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                {
                    scores[layer] = score(static_cast<int>(layer));
                    if (!IsLayerEnabled(layer))
                    {
                        continue;
                    }

                    if (scores[layer] > 0.0f)
                    {
                        const float breakup = terrain_jitter(perlin, world_x, world_z, 3.1f + static_cast<float>(shader_index) * 2.3f);
                        scores[layer]      *= lerp(0.55f, 1.45f, saturate(breakup * 0.5f + 0.5f));
                    }
                    shader_index++;
                }

                const uint32_t keep = clamp(m_layer_quality, 1u, 4u);
                float picked[terrain_layer_max] = {};
                uint32_t dominant_layer = 0;
                for (uint32_t slot = 0; slot < keep; slot++)
                {
                    float best_score = 0.0f;
                    int best_layer   = -1;
                    for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                    {
                        if (scores[layer] > best_score)
                        {
                            best_score = scores[layer];
                            best_layer = static_cast<int>(layer);
                        }
                    }
                    if (best_layer < 0)
                    {
                        break;
                    }
                    if (slot == 0)
                    {
                        dominant_layer = static_cast<uint32_t>(best_layer);
                    }
                    scores[best_layer] = 0.0f;
                    picked[best_layer] = best_score;
                }

                // the rank cut is a step, subtracting the highest rejected score pins a layer to zero
                // as it enters and leaves the set, same as terrain_pick_layers, without it the prop
                // mask steps where the surface fades and grass stops on a hard line
                float rejected = 0.0f;
                for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                {
                    rejected = max(rejected, scores[layer]); // the winners were zeroed as they were consumed
                }

                float total = 0.0f;
                for (uint32_t layer = 0; layer < terrain_layer_max; layer++)
                {
                    picked[layer] = max(picked[layer] - rejected, 0.0f);
                    total += picked[layer];
                }

                float grass = 0.0f;
                float trees = 0.0f;
                float rock  = 0.0f;
                if (total > 1e-6f)
                {
                    const float inv = 1.0f / total;
                    auto share = [&](int layer_index) -> float
                    {
                        return (layer_index >= 0) ? picked[layer_index] * inv : 0.0f;
                    };

                    const float slope_deg  = slope * math::rad_to_deg;
                    const float above_sea  = y_c - sea_local;
                    const float below_snow = snow_local - y_c;
                    const float grass_w    = share(grass_i);
                    const float forest_w   = share(forest_i);
                    const float moss_w     = share(moss_i);
                    const float rock_w     = share(rock_i);
                    const float gravel_w   = share(gravel_i);
                    const float dirt_w     = share(dirt_i);
                    const float sand_w     = share(sand_i);
                    const float snow_w     = share(snow_i);

                    // the surface pick is the biome, props follow the same shares the shader painted
                    const float living  = grass_w + forest_w * 0.9f + moss_w * 0.45f;
                    const float mineral = rock_w + gravel_w * 0.9f + dirt_w * 0.55f;
                    const float barren  = sand_w + snow_w;

                    const float shade = 1.0f - insolation;
                    const float altitude = saturate(
                        (y_c - sea_local) / max(snow_local - sea_local, 1.0f)
                    );
                    const float alpine    = saturate((altitude - 0.55f) / 0.45f);
                    const float tree_line = m_snow_amount > 0.0f ? saturate(1.0f - alpine * 1.35f) : 1.0f;
                    const float slope_soft = saturate(1.0f - slope_deg / 32.0f);

                    // grass only where living ground beats rock, dirt, sand and snow
                    float grass_raw = living - mineral * 1.2f - barren * 1.5f;
                    grass_raw *= slope_soft;
                    if (above_sea < 1.0f || (m_snow_amount > 0.0f && below_snow < 2.0f))
                    {
                        grass_raw = 0.0f;
                    }
                    grass_raw = saturate(grass_raw);
                    // lift the floor so mixed seams stay empty and meadow cores stay full
                    grass = saturate((grass_raw - 0.18f) / 0.82f);
                    grass = grass * grass * (3.0f - 2.0f * grass);

                    // trees fill living ground, forest is dense, meadows get groves
                    float tree_raw = forest_w
                        + grass_w * lerp(0.5f, 0.95f, saturate(shade * 1.15f + deposition * 0.35f))
                        + moss_w * 0.25f;
                    tree_raw *= slope_soft;
                    tree_raw *= tree_line;
                    tree_raw *= saturate(1.0f - mineral * 1.35f);
                    tree_raw *= saturate(1.0f - barren * 2.0f);
                    if (above_sea < 2.0f || (m_snow_amount > 0.0f && below_snow < 6.0f))
                    {
                        tree_raw = 0.0f;
                    }
                    trees = saturate(tree_raw);

                    // rocks on almost all dry ground, denser on mineral and slope
                    float rock_raw = 0.55f + mineral * 0.45f;
                    rock_raw += saturate((slope_deg - 3.0f) / 28.0f) * 0.4f;
                    rock_raw *= saturate(1.0f - living * 0.12f);
                    rock_raw *= saturate(1.0f - sand_w * 0.25f);
                    rock_raw *= saturate(1.0f - snow_w * 0.55f);
                    if (above_sea < -0.5f)
                    {
                        rock_raw = 0.0f;
                    }
                    rock = saturate(rock_raw);
                }

                m_prop_mask_pixels[offset + 0] = static_cast<uint8_t>(grass * 255.0f + 0.5f);
                m_prop_mask_pixels[offset + 1] = static_cast<uint8_t>(trees * 255.0f + 0.5f);
                m_prop_mask_pixels[offset + 2] = static_cast<uint8_t>(rock * 255.0f + 0.5f);
                // which surface layer won this point. the mesh placer reads it off the cpu copy below,
                // the gpu scatter pass reads it out of alpha, which is what lets a ground type gate
                // apply to grass and detail instead of only to mesh layers. it is an index and not a
                // weight, so anything sampling it has to use a point tap
                m_prop_mask_pixels[offset + 3] = static_cast<uint8_t>(dominant_layer);
                m_layer_dominant[i]            = static_cast<uint8_t>(dominant_layer);
            }
        };
        ThreadPool::ParallelLoop(bake, span_x * span_z);
        return true;
    }

    bool Terrain::FlushPendingPropMask()
    {
        const TerrainDirtyRect rect = m_prop_mask_bake_dirty;
        m_prop_mask_bake_dirty.Clear();

        const size_t cell_count = static_cast<size_t>(m_map_width) * m_map_height;
        if (rect.IsEmpty() || cell_count == 0 || m_dense_width < 2 || m_dense_height < 2 ||
            m_prop_mask_pixels.size() != cell_count * 4 || m_map_a_pixels.size() != cell_count * 4)
        {
            return false;
        }

        // the cell to the left and below reads this one as its slope neighbour
        const int32_t gx0 = max(rect.x0 - 1, 0);
        const int32_t gz0 = max(rect.z0 - 1, 0);
        const int32_t gx1 = min(rect.x1, static_cast<int32_t>(m_dense_width) - 1);
        const int32_t gz1 = min(rect.z1, static_cast<int32_t>(m_dense_height) - 1);

        // map cell ax samples dense column floor((ax + 0.5) * dense_w / map_w), invert with slack
        const float to_map_x = static_cast<float>(m_map_width) / static_cast<float>(m_dense_width);
        const float to_map_z = static_cast<float>(m_map_height) / static_cast<float>(m_dense_height);
        const int32_t mx0 = clamp(static_cast<int32_t>(floorf(static_cast<float>(gx0) * to_map_x)) - 1, 0, static_cast<int32_t>(m_map_width) - 1);
        const int32_t mz0 = clamp(static_cast<int32_t>(floorf(static_cast<float>(gz0) * to_map_z)) - 1, 0, static_cast<int32_t>(m_map_height) - 1);
        const int32_t mx1 = clamp(static_cast<int32_t>(ceilf(static_cast<float>(gx1 + 1) * to_map_x)) + 1, 0, static_cast<int32_t>(m_map_width) - 1);
        const int32_t mz1 = clamp(static_cast<int32_t>(ceilf(static_cast<float>(gz1 + 1) * to_map_z)) + 1, 0, static_cast<int32_t>(m_map_height) - 1);
        if (mx1 < mx0 || mz1 < mz0)
        {
            return false;
        }

        const bool has_seed   = m_prop_mask_seed.size() == m_prop_mask_pixels.size();
        const uint32_t span_x = static_cast<uint32_t>(mx1 - mx0 + 1);
        const uint32_t span_z = static_cast<uint32_t>(mz1 - mz0 + 1);

        // a pad hole shows as pixels that differ from the seed, remember them so the rebake
        // does not fill them back in, the alpha channel is the layer index and always refreshes
        vector<uint8_t> punched(static_cast<size_t>(span_x) * span_z, 0);
        if (has_seed)
        {
            for (int32_t z = mz0; z <= mz1; z++)
            {
                for (int32_t x = mx0; x <= mx1; x++)
                {
                    const size_t offset = (static_cast<size_t>(z) * m_map_width + static_cast<size_t>(x)) * 4;
                    const bool hole =
                        m_prop_mask_pixels[offset + 0] != m_prop_mask_seed[offset + 0] ||
                        m_prop_mask_pixels[offset + 1] != m_prop_mask_seed[offset + 1] ||
                        m_prop_mask_pixels[offset + 2] != m_prop_mask_seed[offset + 2];
                    punched[static_cast<size_t>(z - mz0) * span_x + static_cast<size_t>(x - mx0)] = hole ? 1 : 0;
                }
            }
        }

        if (!BakePropMaskCells(mx0, mz0, mx1, mz1))
        {
            return false;
        }

        if (has_seed)
        {
            for (int32_t z = mz0; z <= mz1; z++)
            {
                for (int32_t x = mx0; x <= mx1; x++)
                {
                    const size_t offset = (static_cast<size_t>(z) * m_map_width + static_cast<size_t>(x)) * 4;
                    memcpy(m_prop_mask_seed.data() + offset, m_prop_mask_pixels.data() + offset, 4);
                    if (punched[static_cast<size_t>(z - mz0) * span_x + static_cast<size_t>(x - mx0)])
                    {
                        m_prop_mask_pixels[offset + 0] = 0;
                        m_prop_mask_pixels[offset + 1] = 0;
                        m_prop_mask_pixels[offset + 2] = 0;
                    }
                }
            }
        }

        MarkPropMaskDirty(mx0, mz0, mx1, mz1);
        return UploadPropMask();
    }

    void Terrain::RebuildPropMask()
    {
        BakePropMask();
        if (m_prop_mask_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            m_prop_mask_retired = m_prop_mask;
            m_prop_mask.reset();
            m_prop_mask_dirty.Clear();
            return;
        }

        ReapplyPropMaskHoles();
        m_prop_mask_bake_dirty.Clear();
        m_prop_mask_dirty.Merge(0, 0, static_cast<int32_t>(m_map_width) - 1, static_cast<int32_t>(m_map_height) - 1);
        UploadPropMask();
    }

    Vector3 Terrain::SamplePropMask(float world_x, float world_z) const
    {
        if (IsHeightfieldUnsafe() || m_prop_mask_pixels.empty() || m_map_width == 0 || m_map_height == 0)
        {
            return Vector3::Zero;
        }

        if (IsOnRoad(world_x, world_z)) return Vector3::Zero;

        const Vector4 mapping = GetMappingWorld();
        float u = (world_x - mapping.x) * mapping.z;
        float v = (world_z - mapping.y) * mapping.w;
        u = clamp(u, 0.0f, 1.0f);
        v = clamp(v, 0.0f, 1.0f);

        const float fx = u * static_cast<float>(m_map_width - 1);
        const float fz = v * static_cast<float>(m_map_height - 1);
        const uint32_t x0 = static_cast<uint32_t>(fx);
        const uint32_t z0 = static_cast<uint32_t>(fz);
        const uint32_t x1 = min(x0 + 1u, m_map_width - 1u);
        const uint32_t z1 = min(z0 + 1u, m_map_height - 1u);
        const float tx = fx - static_cast<float>(x0);
        const float tz = fz - static_cast<float>(z0);

        auto fetch = [&](uint32_t x, uint32_t z) -> Vector3
        {
            const size_t offset = (static_cast<size_t>(z) * m_map_width + x) * 4;
            return Vector3(
                m_prop_mask_pixels[offset + 0] * (1.0f / 255.0f),
                m_prop_mask_pixels[offset + 1] * (1.0f / 255.0f),
                m_prop_mask_pixels[offset + 2] * (1.0f / 255.0f)
            );
        };

        const Vector3 c00 = fetch(x0, z0);
        const Vector3 c10 = fetch(x1, z0);
        const Vector3 c01 = fetch(x0, z1);
        const Vector3 c11 = fetch(x1, z1);
        const Vector3 c0 = c00 * (1.0f - tx) + c10 * tx;
        const Vector3 c1 = c01 * (1.0f - tx) + c11 * tx;
        return c0 * (1.0f - tz) + c1 * tz;
    }

    float Terrain::SamplePropMaskChannel(float world_x, float world_z, int channel) const
    {
        const Vector3 mask = SamplePropMask(world_x, world_z);
        if (channel == 0) return mask.x;
        if (channel == 1) return mask.y;
        if (channel == 2) return mask.z;
        return 1.0f;
    }
}
