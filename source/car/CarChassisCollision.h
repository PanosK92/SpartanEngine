#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
#include "physx/cooking/PxCooking.h"

namespace car::chassis_collision
{
    using namespace physx;
    using triangle = std::array<PxVec3, 3>;
    constexpr size_t max_hulls = 6;
    constexpr PxU16 max_vertices = 24;

    struct mesh_release
    {
        void operator()(PxConvexMesh* mesh) const { if (mesh) mesh->release(); }
    };
    using mesh_ptr = std::unique_ptr<PxConvexMesh, mesh_release>;

    inline mesh_ptr cook(const std::vector<PxVec3>& points, const PxCookingParams& params, PxInsertionCallback& insertion)
    {
        if (points.size() < 4) return {};
        PxCookingParams precise_params = params;
        precise_params.planeTolerance = 0.0001f;
        auto cook_points = [&](const std::vector<PxVec3>& input)
        {
            PxConvexMeshDesc desc;
            desc.points.count = static_cast<PxU32>(input.size());
            desc.points.stride = sizeof(PxVec3);
            desc.points.data = input.data();
            desc.flags = PxConvexFlag::eCOMPUTE_CONVEX | PxConvexFlag::eSHIFT_VERTICES;
            desc.vertexLimit = max_vertices;
            PxConvexMeshCookingResult::Enum status;
            mesh_ptr result(PxCreateConvexMesh(precise_params, desc, insertion, &status));
            if (status != PxConvexMeshCookingResult::eSUCCESS) return mesh_ptr{};
            return result;
        };

        // Cached hulls already meet the budget: preserve their planes exactly.
        if (points.size() <= max_vertices)
        {
            auto result = cook_points(points);
            if (result) return result;
        }

        // Start with directional extrema, then add the worst missed support point
        // of every face. Unlike stride sampling, every input point participates.
        // Small cook inputs avoid PhysX's 255 intermediate-polygon limit on curved
        // panels; the default OBB reduction also keeps the final vertex budget.
        std::vector<PxVec3> input;
        for (int x = -1; x <= 1; ++x)
            for (int y = -1; y <= 1; ++y)
                for (int z = -1; z <= 1; ++z)
                {
                    if (x == 0 && y == 0 && z == 0) continue;
                    const PxVec3 direction{float(x), float(y), float(z)};
                    const PxVec3* best = &points.front();
                    float support = best->dot(direction);
                    for (const PxVec3& p : points)
                    {
                        const float distance = p.dot(direction);
                        if (distance > support) { support = distance; best = &p; }
                    }
                    input.push_back(*best);
                }
        for (int iteration = 0; iteration < 8; ++iteration)
        {
            auto result = cook_points(input);
            if (!result) break;
            input.assign(result->getVertices(), result->getVertices() + result->getNbVertices());
            bool covered = true;
            for (PxU32 i = 0; i < result->getNbPolygons(); ++i)
            {
                PxHullPolygon face;
                result->getPolygonData(i, face);
                const PxVec3 normal(face.mPlane[0], face.mPlane[1], face.mPlane[2]);
                float distance = 0.0005f; // sub-millimetre cooking tolerance
                const PxVec3* missed = nullptr;
                for (const PxVec3& p : points)
                {
                    const float outside = normal.dot(p) + face.mPlane[3];
                    if (outside > distance) { distance = outside; missed = &p; }
                }
                if (missed) { covered = false; input.push_back(*missed); }
            }
            if (covered) return result;
        }
        // Numerical/pathological input must not leave an uncovered part of the
        // chassis. A conservative box is also eligible for subsequent splitting.
        PxVec3 lo(PX_MAX_F32), hi(-PX_MAX_F32);
        for (const PxVec3& p : points) { lo = lo.minimum(p); hi = hi.maximum(p); }
        if ((hi - lo).minElement() < 0.0001f) return {};
        input.clear();
        for (int i = 0; i < 8; ++i)
            input.emplace_back(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
        return cook_points(input);
    }

    inline float volume(const PxConvexMesh& mesh)
    {
        PxReal mass;
        PxMat33 inertia;
        PxVec3 center;
        mesh.getMassInformation(mass, inertia, center);
        return mass; // unit-density cooked hull
    }

    inline std::vector<PxVec3> points_of(const std::vector<triangle>& triangles)
    {
        std::vector<PxVec3> points;
        points.reserve(triangles.size() * 3);
        for (const triangle& t : triangles) points.insert(points.end(), t.begin(), t.end());
        // Exact deduplication keeps extrema and makes cooking independent of mesh order.
        std::sort(points.begin(), points.end(), [](const PxVec3& a, const PxVec3& b)
        {
            if (a.x != b.x) return a.x < b.x;
            if (a.y != b.y) return a.y < b.y;
            return a.z < b.z;
        });
        points.erase(std::unique(points.begin(), points.end()), points.end());
        return points;
    }

    // Clip the actual surface, including new vertices on every crossing edge.
    // Selecting only existing vertices would leave holes on sparse, large panels.
    inline void clip(const triangle& t, int axis, float plane, bool lower, std::vector<triangle>& output)
    {
        std::array<PxVec3, 4> polygon;
        size_t count = 0;
        PxVec3 a = t.back();
        bool a_inside = lower ? a[axis] <= plane : a[axis] >= plane;
        for (const PxVec3& b : t)
        {
            const bool b_inside = lower ? b[axis] <= plane : b[axis] >= plane;
            if (a_inside != b_inside)
            {
                PxVec3 intersection = a + (b - a) * ((plane - a[axis]) / (b[axis] - a[axis]));
                intersection[axis] = plane;
                polygon[count++] = intersection;
            }
            if (b_inside) polygon[count++] = b;
            a = b;
            a_inside = b_inside;
        }
        for (size_t i = 1; i + 1 < count; ++i)
        {
            if ((polygon[i] - polygon[0]).cross(polygon[i + 1] - polygon[0]).magnitudeSquared() > 1e-16f)
                output.push_back({polygon[0], polygon[i], polygon[i + 1]});
        }
    }

    struct piece
    {
        std::vector<triangle> triangles;
        mesh_ptr mesh;
        std::array<std::vector<triangle>, 2> split_triangles;
        std::array<mesh_ptr, 2> split_meshes;
        float saving = 0;
        bool evaluated = false;
    };

    inline void find_split(piece& parent, const PxCookingParams& params, PxInsertionCallback& insertion)
    {
        parent.evaluated = true;
        PxVec3 lo(PX_MAX_F32), hi(-PX_MAX_F32);
        for (const triangle& t : parent.triangles)
            for (const PxVec3& p : t) { lo = lo.minimum(p); hi = hi.maximum(p); }
        const float parent_volume = volume(*parent.mesh);
        for (int axis = 0; axis < 3; ++axis)
        {
            // Avoid paper-thin slivers that cost another broadphase shape.
            if (hi[axis] - lo[axis] < 0.3f) continue;
            for (float fraction : {0.25f, 0.5f, 0.75f})
            {
                const float plane = lo[axis] + (hi[axis] - lo[axis]) * fraction;
                std::array<std::vector<triangle>, 2> children;
                for (const triangle& t : parent.triangles)
                {
                    clip(t, axis, plane, true, children[0]);
                    clip(t, axis, plane, false, children[1]);
                }
                std::array<mesh_ptr, 2> meshes;
                for (size_t side = 0; side < 2; ++side)
                    meshes[side] = cook(points_of(children[side]), params, insertion);
                if (!meshes[0] || !meshes[1]) continue; // retain parent if either side fails
                const float saving = parent_volume - volume(*meshes[0]) - volume(*meshes[1]);
                if (saving > parent.saving)
                {
                    parent.saving = saving;
                    parent.split_triangles = std::move(children);
                    parent.split_meshes = std::move(meshes);
                }
            }
        }
    }

    inline std::vector<mesh_ptr> build(const std::vector<triangle>& surface, const PxCookingParams& params, PxInsertionCallback& insertion)
    {
        piece root;
        root.triangles.reserve(surface.size());
        for (const triangle& t : surface)
            if (t[0].isFinite() && t[1].isFinite() && t[2].isFinite()) root.triangles.push_back(t);
        root.mesh = cook(points_of(root.triangles), params, insertion);
        if (!root.mesh) return {};
        // Spend the next shape only on a meaningful reduction in excess volume.
        const float minimum_saving = volume(*root.mesh) * 0.01f;
        std::vector<piece> pieces;
        pieces.push_back(std::move(root));
        while (pieces.size() < max_hulls)
        {
            size_t best = pieces.size();
            float saving = minimum_saving;
            for (size_t i = 0; i < pieces.size(); ++i)
            {
                if (!pieces[i].evaluated) find_split(pieces[i], params, insertion);
                if (pieces[i].saving > saving) { saving = pieces[i].saving; best = i; }
            }
            if (best == pieces.size()) break;
            piece left, right;
            left.triangles = std::move(pieces[best].split_triangles[0]);
            right.triangles = std::move(pieces[best].split_triangles[1]);
            left.mesh = std::move(pieces[best].split_meshes[0]);
            right.mesh = std::move(pieces[best].split_meshes[1]);
            pieces[best] = std::move(left);
            pieces.push_back(std::move(right));
        }
        std::vector<mesh_ptr> result;
        for (piece& p : pieces) result.push_back(std::move(p.mesh));
        return result;
    }

    // Cache plain hull vertices, not SDK objects, so worlds/PhysX can be torn down
    // safely. Caller serializes access. Traffic instances only recook tiny hulls.
    struct cache
    {
        struct entry
        {
            uint64_t hash;
            size_t triangle_count;
            std::vector<std::vector<PxVec3>> hulls;
        };
        std::vector<entry> entries;

        std::vector<mesh_ptr> get(const std::vector<triangle>& triangles, const PxCookingParams& params, PxInsertionCallback& insertion)
        {
            uint64_t hash = 14695981039346656037ull;
            for (const triangle& t : triangles)
                for (const PxVec3& p : t)
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        uint32_t bits;
                        const float value = p[axis] == 0 ? 0.0f : p[axis];
                        std::memcpy(&bits, &value, sizeof(bits));
                        hash = (hash ^ bits) * 1099511628211ull;
                    }
            for (const entry& e : entries)
            {
                if (e.hash != hash || e.triangle_count != triangles.size()) continue;
                std::vector<mesh_ptr> result;
                for (const auto& points : e.hulls)
                {
                    auto mesh = cook(points, params, insertion);
                    if (!mesh) return build(triangles, params, insertion);
                    result.push_back(std::move(mesh));
                }
                return result;
            }
            auto result = build(triangles, params, insertion);
            if (!result.empty())
            {
                entry e{hash, triangles.size(), {}};
                for (const auto& mesh : result)
                    e.hulls.emplace_back(mesh->getVertices(), mesh->getVertices() + mesh->getNbVertices());
                if (entries.size() == 16) entries.erase(entries.begin());
                entries.push_back(std::move(e));
            }
            return result;
        }
    };
}
