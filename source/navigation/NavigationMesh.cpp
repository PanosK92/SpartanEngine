/*
Copyright(c) 2015-2026 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions :

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "NavigationMesh.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include "Recast.h"
#include "DetourNavMesh.h"
#include "DetourNavMeshBuilder.h"
#include "DetourNavMeshQuery.h"
#include "DetourCrowd.h"

namespace spartan::navigation
{
    struct NavigationMesh::Impl
    {
        dtNavMesh* mesh = dtAllocNavMesh();
        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        dtCrowd* crowd = dtAllocCrowd();
        bool ready = false;
        ~Impl() { dtFreeCrowd(crowd); dtFreeNavMeshQuery(query); dtFreeNavMesh(mesh); }
    };

    NavigationMesh::NavigationMesh() : m_impl(std::make_unique<Impl>())
    {
        auto& s = *m_impl;
        dtNavMeshParams params{};
        params.tileWidth = params.tileHeight = tile_size;
        params.maxTiles = 1024;
        params.maxPolys = 4096;
        s.ready = s.mesh && s.query && s.crowd &&
            dtStatusSucceed(s.mesh->init(&params)) &&
            dtStatusSucceed(s.query->init(s.mesh, 8192)) && s.crowd->init(256, agent_radius, s.mesh);
    }

    NavigationMesh::~NavigationMesh() = default;
    bool NavigationMesh::IsReady() const { return m_impl->ready; }

    TileData NavigationMesh::BuildTile(const TileInput& input)
    {
        TileData result;
        result.x = input.x;
        result.z = input.z;
        if (input.indices.empty()) { result.succeeded = true; return result; }
        if (input.vertices.size() % 3 || input.indices.size() % 3 || input.max_y <= input.min_y) return result;
        const int vertex_count = static_cast<int>(input.vertices.size() / 3);
        const int triangle_count = static_cast<int>(input.indices.size() / 3);
        for (int index : input.indices) if (index < 0 || index >= vertex_count) return result;
        for (float value : input.vertices) if (!std::isfinite(value)) return result;

        rcContext context;
        rcConfig cfg{};
        cfg.cs = cell_size;
        cfg.ch = cell_height;
        cfg.walkableSlopeAngle = 45.0f;
        cfg.walkableHeight = static_cast<int>(std::ceil(agent_height / cfg.ch));
        cfg.walkableClimb = static_cast<int>(std::floor(agent_climb / cfg.ch));
        cfg.walkableRadius = static_cast<int>(std::ceil(agent_radius / cfg.cs));
        cfg.tileSize = static_cast<int>(std::round(tile_size / cfg.cs));
        cfg.borderSize = cfg.walkableRadius + 3;
        cfg.width = cfg.height = cfg.tileSize + cfg.borderSize * 2;
        cfg.maxEdgeLen = static_cast<int>(12.0f / cfg.cs);
        cfg.maxSimplificationError = 1.3f;
        cfg.minRegionArea = 8 * 8;
        cfg.mergeRegionArea = 20 * 20;
        cfg.maxVertsPerPoly = DT_VERTS_PER_POLYGON;
        cfg.detailSampleDist = cfg.cs * 6;
        cfg.detailSampleMaxError = cfg.ch;
        cfg.bmin[0] = input.x * tile_size - cfg.borderSize * cfg.cs;
        cfg.bmin[1] = input.min_y;
        cfg.bmin[2] = input.z * tile_size - cfg.borderSize * cfg.cs;
        cfg.bmax[0] = (input.x + 1) * tile_size + cfg.borderSize * cfg.cs;
        cfg.bmax[1] = input.max_y;
        cfg.bmax[2] = (input.z + 1) * tile_size + cfg.borderSize * cfg.cs;

        std::unique_ptr<rcHeightfield, decltype(&rcFreeHeightField)> solid(rcAllocHeightfield(), rcFreeHeightField);
        std::unique_ptr<rcCompactHeightfield, decltype(&rcFreeCompactHeightfield)> compact(rcAllocCompactHeightfield(), rcFreeCompactHeightfield);
        std::unique_ptr<rcContourSet, decltype(&rcFreeContourSet)> contours(rcAllocContourSet(), rcFreeContourSet);
        std::unique_ptr<rcPolyMesh, decltype(&rcFreePolyMesh)> poly(rcAllocPolyMesh(), rcFreePolyMesh);
        std::unique_ptr<rcPolyMeshDetail, decltype(&rcFreePolyMeshDetail)> detail(rcAllocPolyMeshDetail(), rcFreePolyMeshDetail);
        if (!solid || !compact || !contours || !poly || !detail) return result;
        if (!rcCreateHeightfield(&context, *solid, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch)) return result;
        std::vector<unsigned char> areas(triangle_count, RC_NULL_AREA);
        rcMarkWalkableTriangles(&context, cfg.walkableSlopeAngle, input.vertices.data(), vertex_count,
            input.indices.data(), triangle_count, areas.data());
        if (!rcRasterizeTriangles(&context, input.vertices.data(), vertex_count, input.indices.data(),
            areas.data(), triangle_count, *solid, cfg.walkableClimb)) return result;
        rcFilterLowHangingWalkableObstacles(&context, cfg.walkableClimb, *solid);
        rcFilterLedgeSpans(&context, cfg.walkableHeight, cfg.walkableClimb, *solid);
        rcFilterWalkableLowHeightSpans(&context, cfg.walkableHeight, *solid);
        if (!rcBuildCompactHeightfield(&context, cfg.walkableHeight, cfg.walkableClimb, *solid, *compact)) return result;
        solid.reset();
        if (!rcErodeWalkableArea(&context, cfg.walkableRadius, *compact) ||
            !rcBuildDistanceField(&context, *compact) ||
            !rcBuildRegions(&context, *compact, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea) ||
            !rcBuildContours(&context, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours) ||
            !rcBuildPolyMesh(&context, *contours, cfg.maxVertsPerPoly, *poly)) return result;
        if (!poly->npolys) { result.succeeded = true; return result; }
        if (poly->npolys > 4096 || poly->nverts >= 65535) return result;
        if (!rcBuildPolyMeshDetail(&context, *poly, *compact, cfg.detailSampleDist, cfg.detailSampleMaxError, *detail)) return result;
        for (int i = 0; i < poly->npolys; ++i) { poly->areas[i] = 0; poly->flags[i] = 1; }
        dtNavMeshCreateParams params{};
        params.verts = poly->verts; params.vertCount = poly->nverts;
        params.polys = poly->polys; params.polyAreas = poly->areas; params.polyFlags = poly->flags;
        params.polyCount = poly->npolys; params.nvp = poly->nvp;
        params.detailMeshes = detail->meshes; params.detailVerts = detail->verts;
        params.detailVertsCount = detail->nverts; params.detailTris = detail->tris;
        params.detailTriCount = detail->ntris;
        params.walkableHeight = agent_height; params.walkableRadius = agent_radius; params.walkableClimb = agent_climb;
        params.tileX = input.x; params.tileY = input.z;
        std::memcpy(params.bmin, poly->bmin, sizeof(params.bmin));
        std::memcpy(params.bmax, poly->bmax, sizeof(params.bmax));
        params.cs = cfg.cs; params.ch = cfg.ch; params.buildBvTree = true;
        unsigned char* data = nullptr;
        int size = 0;
        if (!dtCreateNavMeshData(&params, &data, &size)) return result;
        result.bytes.assign(data, data + size);
        dtFree(data);
        result.succeeded = true;
        return result;
    }

    bool NavigationMesh::Install(const TileData& tile)
    {
        if (!IsReady() || !tile.succeeded) return false;
        if (tile.bytes.empty()) return true;
        auto* data = static_cast<unsigned char*>(dtAlloc(tile.bytes.size(), DT_ALLOC_PERM));
        if (!data) return false;
        std::memcpy(data, tile.bytes.data(), tile.bytes.size());
        if (dtStatusFailed(m_impl->mesh->addTile(data, static_cast<int>(tile.bytes.size()), DT_TILE_FREE_DATA, 0, nullptr)))
        { dtFree(data); return false; }
        return true;
    }

    void NavigationMesh::Remove(int x, int z)
    {
        if (!IsReady()) return;
        const dtTileRef ref = m_impl->mesh->getTileRefAt(x, z, 0);
        if (ref) m_impl->mesh->removeTile(ref, nullptr, nullptr);
    }

    bool NavigationMesh::Project(const Point& position, Point& projected) const
    {
        if (!IsReady()) return false;
        const float extents[] = {2.0f, 4.0f, 2.0f};
        dtPolyRef ref = 0;
        dtQueryFilter filter;
        return dtStatusSucceed(m_impl->query->findNearestPoly(position.data(), extents, &filter, &ref, projected.data())) && ref;
    }

    int NavigationMesh::AddAgent(const Point& position, float speed)
    {
        Point projected;
        if (!Project(position, projected)) return -1;
        dtCrowdAgentParams params{};
        params.radius = agent_radius; params.height = agent_height;
        params.maxAcceleration = 5.0f; params.maxSpeed = std::clamp(speed, 0.0f, 8.0f);
        params.collisionQueryRange = agent_radius * 12;
        params.pathOptimizationRange = agent_radius * 30;
        params.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OBSTACLE_AVOIDANCE |
            DT_CROWD_SEPARATION | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO;
        params.separationWeight = 2.0f;
        return m_impl->crowd->addAgent(projected.data(), &params);
    }

    void NavigationMesh::RemoveAgent(int agent)
    {
        if (IsReady() && agent >= 0 && agent < m_impl->crowd->getAgentCount()) m_impl->crowd->removeAgent(agent);
    }

    bool NavigationMesh::GetAgent(int agent, Point& position, Point& velocity) const
    {
        if (!IsReady() || agent < 0 || agent >= m_impl->crowd->getAgentCount()) return false;
        const dtCrowdAgent* a = m_impl->crowd->getAgent(agent);
        if (!a || !a->active || a->state == DT_CROWDAGENT_STATE_INVALID) return false;
        std::copy(a->npos, a->npos + 3, position.begin());
        std::copy(a->vel, a->vel + 3, velocity.begin());
        return true;
    }

    bool NavigationMesh::Wander(int agent, float radius)
    {
        Point position, velocity;
        if (!GetAgent(agent, position, velocity)) return false;
        const dtCrowdAgent* a = m_impl->crowd->getAgent(agent);
        const dtQueryFilter* filter = m_impl->crowd->getFilter(0);
        dtPolyRef ref = 0;
        Point goal{};
        // Detour's callback has no user data; this RNG is private to main-thread navigation.
        auto random = []() -> float
        {
            static uint32_t state = 0x71e53a19;
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            return static_cast<float>(state & 0xffffffu) / 16777216.0f;
        };
        const dtStatus status = m_impl->query->findRandomPointAroundCircle(a->corridor.getFirstPoly(),
            position.data(), radius, filter, random, &ref, goal.data());
        // Never accept a truncated search as a successful route.
        if (dtStatusFailed(status) || dtStatusDetail(status, DT_OUT_OF_NODES) || !ref) return false;
        return MoveTo(agent, goal);
    }

    bool NavigationMesh::MoveTo(int agent, const Point& destination)
    {
        Point position, velocity;
        if (!GetAgent(agent, position, velocity)) return false;
        const dtCrowdAgent* a = m_impl->crowd->getAgent(agent);
        const dtQueryFilter* filter = m_impl->crowd->getFilter(0);
        const float extents[] = {2.0f, 4.0f, 2.0f};
        dtPolyRef target = 0;
        Point goal;
        if (dtStatusFailed(m_impl->query->findNearestPoly(destination.data(), extents, filter, &target, goal.data())) || !target) return false;
        dtPolyRef path[256];
        int count = 0;
        const dtStatus status = m_impl->query->findPath(a->corridor.getFirstPoly(), target, position.data(), goal.data(), filter, path, &count, 256);
        if (dtStatusFailed(status) || dtStatusDetail(status, DT_PARTIAL_RESULT) || dtStatusDetail(status, DT_BUFFER_TOO_SMALL) ||
            dtStatusDetail(status, DT_OUT_OF_NODES) || count == 0 || path[count - 1] != target) return false;
        return m_impl->crowd->requestMoveTarget(agent, target, goal.data());
    }

    void NavigationMesh::Update(float delta_time)
    {
        if (IsReady()) m_impl->crowd->update(std::clamp(delta_time, 0.0f, 0.1f), nullptr);
    }

    void NavigationMesh::GetDebugTriangles(std::vector<Point>& triangles) const
    {
        triangles.clear();
        if (!IsReady()) return;
        const dtNavMesh* mesh = m_impl->mesh;
        for (int i = 0; i < mesh->getMaxTiles(); ++i)
        {
            const dtMeshTile* tile = mesh->getTile(i);
            if (!tile || !tile->header) continue;
            for (int p = 0; p < tile->header->polyCount; ++p)
            {
                const dtPoly& poly = tile->polys[p];
                if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION) continue;
                const dtPolyDetail& detail = tile->detailMeshes[p];
                for (unsigned int j = 0; j < detail.triCount; ++j)
                {
                    const unsigned char* tri = &tile->detailTris[(detail.triBase + j) * 4];
                    for (int k = 0; k < 3; ++k)
                    {
                        const unsigned int index = tri[k];
                        const float* v = index < poly.vertCount ? &tile->verts[poly.verts[index] * 3] :
                            &tile->detailVerts[(detail.vertBase + index - poly.vertCount) * 3];
                        // The detail mesh follows terrain relief; lift only the debug copy by 15 cm.
                        triangles.push_back({v[0], v[1] + 0.15f, v[2]});
                    }
                }
            }
        }
    }
}
