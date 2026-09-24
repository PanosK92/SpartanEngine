/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "NavigationWorld.h"
#include "../core/ThreadPool.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Terrain.h"
#include "../world/components/Render.h"
#include "../world/components/Physics.h"
#include "../world/components/Animator.h"
#include "../world/components/SplineFollower.h"
#include "../rendering/Renderer.h"
#include <future>

using namespace spartan::math;

namespace spartan
{
    namespace
    {
        using TileKey = std::pair<int, int>;
        constexpr int active_radius = 5; // 704 m across, covers population spawn/recycle distances
        constexpr int retain_radius = 7;

        bool overlaps(const BoundingBox& box, float x0, float z0, float x1, float z1)
        {
            return box.GetMax().x >= x0 && box.GetMin().x <= x1 && box.GetMax().z >= z0 && box.GetMin().z <= z1;
        }

        void triangle(navigation::TileInput& input, Vector3 a, Vector3 b, Vector3 c)
        {
            const int base = static_cast<int>(input.vertices.size() / 3);
            for (const Vector3& p : {a, b, c})
            {
                input.vertices.insert(input.vertices.end(), {p.x, p.y, p.z});
                input.min_y = std::min(input.min_y, p.y - 1.0f);
                input.max_y = std::max(input.max_y, p.y + navigation::agent_height + 1.0f);
            }
            input.indices.insert(input.indices.end(), {base, base + 1, base + 2});
        }
    }

    struct NavigationWorld::Impl
    {
        navigation::NavigationMesh mesh;
        std::vector<uint64_t> terrains;
        struct Source { uint64_t entity; BoundingBox bounds; };
        std::vector<Source> sources;
        std::set<TileKey> installed;
        std::set<TileKey> failed;
        struct Build
        {
            std::atomic<bool> done = false;
            navigation::TileData result;
        };
        std::shared_ptr<Build> pending;
        std::vector<navigation::Point> debug_triangles;
        bool debug_dirty = true;

        bool Gather(int x, int z, navigation::TileInput& input)
        {
            input.x = x; input.z = z;
            input.min_y = std::numeric_limits<float>::max();
            input.max_y = std::numeric_limits<float>::lowest();
            const float x0 = x * navigation::tile_size - navigation::tile_border;
            const float z0 = z * navigation::tile_size - navigation::tile_border;
            const float x1 = (x + 1) * navigation::tile_size + navigation::tile_border;
            const float z1 = (z + 1) * navigation::tile_size + navigation::tile_border;

            for (uint64_t id : terrains)
            {
                Entity* entity = World::GetEntityById(id);
                Terrain* terrain = entity ? entity->GetComponent<Terrain>() : nullptr;
                if (!terrain) continue;
                // A terrain worker may be resizing its heightfield. Retry, never publish an empty hole.
                if (!terrain->HasHeightfield() || terrain->IsGenerating() || terrain->IsMeshCommitPending()) return false;
                const Matrix inverse = entity->GetMatrix().Inverted();
                const Vector3 local_min = terrain->GetPositions().front();
                const Vector3 local_max = terrain->GetPositions().back();
                constexpr int width = 69; // 64 m tile plus 2 m border each side, sampled every metre
                std::array<Vector3, width * width> points;
                std::array<bool, width * width> valid{};
                const float sea = terrain->ResolveSeaLevelWorld();
                for (int row = 0; row < width; ++row)
                for (int col = 0; col < width; ++col)
                {
                    Vector3& p = points[row * width + col];
                    p = Vector3(x0 + col, 0, z0 + row);
                    const Vector3 local = inverse * p;
                    // SampleHeight clamps outside the grid; don't extend the island infinitely.
                    valid[row * width + col] = local.x >= local_min.x && local.x <= local_max.x &&
                        local.z >= local_min.z && local.z <= local_max.z &&
                        terrain->SampleHeight(p.x, p.z, p.y) && p.y > sea + 0.1f;
                }
                for (int row = 0; row < width - 1; ++row)
                for (int col = 0; col < width - 1; ++col)
                {
                    const int a = row * width + col, b = a + 1, c = a + width, d = c + 1;
                    if (valid[a] && valid[c] && valid[b]) triangle(input, points[a], points[c], points[b]);
                    if (valid[b] && valid[c] && valid[d]) triangle(input, points[b], points[c], points[d]);
                }
            }

            std::vector<uint32_t> indices;
            std::vector<RHI_Vertex_PosTexNorTan> vertices;
            for (const Source& source : sources)
            {
                if (!overlaps(source.bounds, x0, z0, x1, z1)) continue;
                Entity* entity = World::GetEntityById(source.entity);
                Render* render = entity ? entity->GetComponent<Render>() : nullptr;
                if (!render || !entity->IsActive() || !render->GetMesh()) continue;
                std::vector<uint32_t> instances;
                if (render->HasInstancing() && !render->GetInstanceBoundsGroups().empty())
                {
                    for (const auto& group : render->GetInstanceBoundsGroups())
                    {
                        if (!overlaps(group.bounds, x0, z0, x1, z1)) continue;
                        for (uint32_t i = group.offset; i < group.offset + group.count; ++i)
                        {
                            const uint32_t instance = render->GetGroupedInstanceIndex(i);
                            if (overlaps(render->GetInstanceBounds(instance), x0, z0, x1, z1)) instances.push_back(instance);
                        }
                    }
                }
                else
                {
                    for (uint32_t i = 0; i < render->GetInstanceCount(); ++i) instances.push_back(i);
                }
                if (instances.empty()) continue;
                indices.clear(); vertices.clear();
                render->GetGeometry(&indices, &vertices);
                for (uint32_t instance : instances)
                {
                    const Matrix transform = render->HasInstancing() ? render->GetInstance(instance, true) : entity->GetMatrix();
                    if (!overlaps(render->GetBoundingBoxMesh() * transform, x0, z0, x1, z1)) continue;
                    for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    {
                        if (indices[i] >= vertices.size() || indices[i + 1] >= vertices.size() || indices[i + 2] >= vertices.size()) continue;
                        Vector3 a = transform * vertices[indices[i]].get_position();
                        Vector3 b = transform * vertices[indices[i + 1]].get_position();
                        Vector3 c = transform * vertices[indices[i + 2]].get_position();
                        if (std::max({a.x, b.x, c.x}) < x0 || std::min({a.x, b.x, c.x}) > x1 ||
                            std::max({a.z, b.z, c.z}) < z0 || std::min({a.z, b.z, c.z}) > z1) continue;
                        // Imported and procedural assets can have either winding. Use the authored
                        // normal to orient the triangle for Recast's upward-facing slope test.
                        const Vector3 normal_local = vertices[indices[i]].get_normal();
                        const Vector3 normal = transform * normal_local - transform * Vector3::Zero;
                        if (Vector3::Dot(Vector3::Cross(b - a, c - a), normal) < 0) std::swap(b, c);
                        triangle(input, a, b, c);
                    }
                }
            }
            return true;
        }
    };

    NavigationWorld::NavigationWorld() : m_impl(std::make_unique<Impl>())
    {
        for (Entity* entity : World::GetEntities())
        {
            if (!entity || !entity->IsActive()) continue;
            if (entity->GetComponent<Terrain>()) m_impl->terrains.push_back(entity->GetObjectId());
            Render* render = entity->GetComponent<Render>();
            if (!render || !render->GetMesh() || Terrain::ParseTileIndex(entity) >= 0) continue;
            Physics* physics = nullptr;
            bool moving = false;
            for (Entity* parent = entity; parent; parent = parent->GetParent())
            {
                if (!parent->IsActive() || parent->GetComponent<Animator>() || parent->GetComponent<SplineFollower>()) moving = true;
                if (!physics) physics = parent->GetComponent<Physics>();
            }
            if (moving || entity->IsDynamic() || !physics || !physics->IsStatic() || !physics->IsEnabled() || physics->IsKinematic() ||
                physics->GetBodyType() == BodyType::Heightfield || physics->GetBodyType() == BodyType::Controller ||
                physics->GetBodyType() == BodyType::Vehicle || physics->GetBodyType() == BodyType::Cloth) continue;
            m_impl->sources.push_back({entity->GetObjectId(), render->GetBoundingBox()});
        }
        if (!m_impl->mesh.IsReady()) SP_LOG_ERROR("Navigation: could not initialize Detour");
        SP_LOG_INFO("Navigation: streaming island tiles, %u terrains and %u static mesh sources",
            static_cast<uint32_t>(m_impl->terrains.size()), static_cast<uint32_t>(m_impl->sources.size()));
    }

    NavigationWorld::~NavigationWorld() = default; // pending workers own their input/result, never this world
    navigation::NavigationMesh& NavigationWorld::GetMesh() { return m_impl->mesh; }

    bool NavigationWorld::Project(Vector3& position) const
    {
        navigation::Point projected;
        if (!m_impl->mesh.Project({position.x, position.y, position.z}, projected)) return false;
        position = Vector3(projected[0], projected[1], projected[2]);
        return true;
    }

    void NavigationWorld::Tick(const Vector3& focus, float delta_time, bool debug_draw)
    {
        auto& s = *m_impl;
        if (!s.mesh.IsReady()) return;
        const int cx = static_cast<int>(std::floor(focus.x / navigation::tile_size));
        const int cz = static_cast<int>(std::floor(focus.z / navigation::tile_size));
        if (s.pending && s.pending->done.load(std::memory_order_acquire))
        {
            const auto& tile = s.pending->result;
            if (std::abs(tile.x - cx) <= retain_radius && std::abs(tile.z - cz) <= retain_radius)
            {
                if (s.mesh.Install(tile)) s.installed.insert({tile.x, tile.z});
                else
                {
                    s.failed.insert({tile.x, tile.z});
                    SP_LOG_ERROR("Navigation: tile %d, %d failed to build/install", tile.x, tile.z);
                }
                s.debug_dirty = true;
            }
            s.pending.reset();
        }
        for (auto it = s.installed.begin(); it != s.installed.end();)
        {
            if (std::abs(it->first - cx) > retain_radius || std::abs(it->second - cz) > retain_radius)
            {
                s.mesh.Remove(it->first, it->second);
                it = s.installed.erase(it);
                s.debug_dirty = true;
            }
            else ++it;
        }
        // Closest missing tile first. One immutable snapshot/build in flight bounds both memory
        // and worker pressure even if the camera teleports to the other side of the island.
        if (!s.pending)
        {
            TileKey next{};
            int best = std::numeric_limits<int>::max();
            for (int z = cz - active_radius; z <= cz + active_radius; ++z)
            for (int x = cx - active_radius; x <= cx + active_radius; ++x)
            {
                TileKey key{x, z};
                const int distance = (x - cx) * (x - cx) + (z - cz) * (z - cz);
                if (!s.installed.count(key) && !s.failed.count(key) && distance < best) { next = key; best = distance; }
            }
            if (best != std::numeric_limits<int>::max())
            {
                navigation::TileInput input;
                if (s.Gather(next.first, next.second, input))
                {
                    s.pending = std::make_shared<Impl::Build>();
                    const auto build = s.pending;
                    ThreadPool::AddTask([build, input = std::move(input)]()
                    {
                        try { build->result = navigation::NavigationMesh::BuildTile(input); }
                        catch (...) { build->result.x = input.x; build->result.z = input.z; }
                        build->done.store(true, std::memory_order_release);
                    });
                }
            }
        }
        if (delta_time > 0.0f) s.mesh.Update(delta_time);
        if (debug_draw)
        {
            if (s.debug_dirty) { s.mesh.GetDebugTriangles(s.debug_triangles); s.debug_dirty = false; }
            // Match the editor's default light-blue accent, at 35% opacity.
            const Color color(0.439f, 0.831f, 1.0f, 0.35f);
            for (size_t i = 0; i + 2 < s.debug_triangles.size(); i += 3)
            {
                const auto& a = s.debug_triangles[i]; const auto& b = s.debug_triangles[i + 1]; const auto& c = s.debug_triangles[i + 2];
                Renderer::DrawTriangleFilled(Vector3(a[0], a[1], a[2]), Vector3(b[0], b[1], b[2]), Vector3(c[0], c[1], c[2]), color);
            }
        }
    }
}
