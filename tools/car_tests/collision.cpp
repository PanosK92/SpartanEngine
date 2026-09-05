// Standalone geometry regression using the production chassis fitter and bundled PhysX.
#include <new>
#define PX_PHYSX_STATIC_LIB
#include "PxPhysicsAPI.h"
#include "car/CarChassisCollision.h"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <chrono>
#include <cctype>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <limits>

using namespace physx;
namespace cc = car::chassis_collision;
void check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

void box(std::vector<cc::triangle>& surface, PxVec3 lo, PxVec3 hi)
{
    PxVec3 v[8];
    for (int i = 0; i < 8; ++i) v[i] = PxVec3(i & 1 ? hi.x : lo.x, i & 2 ? hi.y : lo.y, i & 4 ? hi.z : lo.z);
    constexpr int faces[][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
    for (const auto& f : faces)
    {
        surface.push_back({v[f[0]], v[f[1]], v[f[2]]});
        surface.push_back({v[f[0]], v[f[2]], v[f[3]]});
    }
}

float outside_distance(const PxVec3& p, const std::vector<cc::mesh_ptr>& hulls)
{
    float distance = PX_MAX_F32;
    for (const auto& hull : hulls)
    {
        float outside = -PX_MAX_F32;
        for (PxU32 i = 0; i < hull->getNbPolygons(); ++i)
        {
            PxHullPolygon face;
            hull->getPolygonData(i, face);
            outside = PxMax(outside, PxVec3(face.mPlane[0], face.mPlane[1], face.mPlane[2]).dot(p) + face.mPlane[3]);
        }
        distance = PxMin(distance, outside);
    }
    return distance;
}

float validate(const std::vector<cc::triangle>& surface, const std::vector<cc::mesh_ptr>& hulls, bool dense)
{
    check(!hulls.empty() && hulls.size() <= cc::max_hulls, "bounded hull count");
    for (const auto& hull : hulls)
    {
        check(hull->getNbVertices() <= cc::max_vertices && hull->getNbPolygons() <= 2 * cc::max_vertices - 4, "bounded hull complexity");
        check(cc::volume(*hull) > 0 && std::isfinite(cc::volume(*hull)), "finite positive volume");
    }
    float worst = 0;
    const int steps = dense ? 8 : 1;
    for (const auto& t : surface)
    {
        for (int i = 0; i <= steps; ++i)
            for (int j = 0; j <= steps - i; ++j)
                worst = PxMax(worst, outside_distance(t[0] + (t[1] - t[0]) * (float(i) / steps) + (t[2] - t[0]) * (float(j) / steps), hulls));
        worst = PxMax(worst, outside_distance((t[0] + t[1] + t[2]) / 3, hulls));
    }
    printf("maximum sampled surface undercoverage: %.6f m\n", worst);
    check(worst < 0.001f, "surface covered within 1 mm, including sparse triangles at split seams");
    return worst;
}

void collect(const aiScene& scene, const aiNode& node, aiMatrix4x4 parent, std::vector<cc::triangle>& surface)
{
    std::string name = node.mName.C_Str();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    for (const char* hidden : {"tire 1", "tire 2", "tire 3", "tire 4", "brakerear"})
        if (name.find(hidden) != std::string::npos) return;
    const aiMatrix4x4 transform = parent * node.mTransformation;
    for (unsigned int i = 0; i < node.mNumMeshes; ++i)
    {
        const aiMesh& mesh = *scene.mMeshes[node.mMeshes[i]];
        for (unsigned int j = 0; j < mesh.mNumFaces; ++j)
        {
            const aiFace& face = mesh.mFaces[j];
            if (face.mNumIndices != 3) continue;
            cc::triangle t;
            for (int k = 0; k < 3; ++k)
            {
                const auto p = transform * mesh.mVertices[face.mIndices[k]];
                t[k] = PxVec3(p.x, p.y, p.z);
            }
            surface.push_back(t);
        }
    }
    for (unsigned int i = 0; i < node.mNumChildren; ++i) collect(scene, *node.mChildren[i], transform, surface);
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    PxDefaultAllocator allocator;
    PxDefaultErrorCallback errors;
    PxFoundation* foundation = PxCreateFoundation(PX_PHYSICS_VERSION, allocator, errors);
    try
    {
        check(foundation != nullptr, "PhysX foundation");
        PxCookingParams params{PxTolerancesScale()};
        auto& insertion = *PxGetStandaloneInsertionCallback();
        {
            std::vector<cc::triangle> surface;
            box(surface, PxVec3(-1, -0.25f, -2.3f), PxVec3(1, 0.25f, 2.3f));
            auto convex = cc::build(surface, params, insertion);
            check(convex.size() == 1, "a convex box must not consume six hulls");
            validate(surface, convex, true);
            box(surface, PxVec3(-0.65f, 0.25f, -0.9f), PxVec3(0.65f, 1, 0.7f));
            auto root = cc::cook(cc::points_of(surface), params, insertion);
            auto hulls = cc::build(surface, params, insertion);
            validate(surface, hulls, true);
            float total = 0;
            for (const auto& hull : hulls) total += cc::volume(*hull);
            printf("sparse car: %zu hulls, volume %.3f vs single hull %.3f\n", hulls.size(), total, cc::volume(*root));
            check(total < cc::volume(*root) * 0.8f, "adaptive splitting removes excess volume");
            std::reverse(surface.begin(), surface.end());
            auto reversed = cc::build(surface, params, insertion);
            validate(surface, reversed, true);
            float reversed_volume = 0;
            for (const auto& hull : reversed) reversed_volume += cc::volume(*hull);
            check(hulls.size() == reversed.size() && fabsf(total - reversed_volume) < 0.0001f, "mesh ordering does not alter fit");
            cc::cache cache;
            auto first = cache.get(surface, params, insertion);
            auto second = cache.get(surface, params, insertion);
            validate(surface, second, true);
            check(first.size() == second.size() && cache.entries.size() == 1, "cache reuse");
            for (size_t i = 0; i < first.size(); ++i)
                check(fabsf(cc::volume(*first[i]) - cc::volume(*second[i])) < 0.0001f, "cache preserves hull volume");
            surface.push_back({PxVec3(std::numeric_limits<float>::quiet_NaN()), PxVec3(0), PxVec3(1)});
            auto sanitized = cc::build(surface, params, insertion);
            check(sanitized.size() == hulls.size(), "invalid triangle ignored");
            check(cc::build({}, params, insertion).empty(), "empty input fails safely");
            surface.pop_back();
            // A planar wing cannot be cooked by itself. A rejected split must keep
            // it attached to its parent, rather than silently dropping the panel.
            surface.push_back({PxVec3(-1.5f, 0.8f, -2.2f), PxVec3(1.5f, 0.8f, -2.2f), PxVec3(0, 0.8f, -1.8f)});
            auto wing = cc::build(surface, params, insertion);
            validate(surface, wing, true);
        }
        if (argc > 1)
        {
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFile(argv[1], aiProcess_Triangulate | aiProcess_JoinIdenticalVertices);
            check(scene && scene->mRootNode, importer.GetErrorString());
            std::vector<cc::triangle> surface;
            collect(*scene, *scene->mRootNode, aiMatrix4x4(), surface);
            PxVec3 lo(PX_MAX_F32), hi(-PX_MAX_F32);
            for (const auto& t : surface) for (const auto& p : t) { lo = lo.minimum(p); hi = hi.maximum(p); }
            const PxVec3 extent = hi - lo, center = (hi + lo) * 0.5f;
            std::array<int, 3> axes{0, 1, 2};
            std::sort(axes.begin(), axes.end(), [&](int a, int b) { return extent[a] < extent[b]; });
            const float scale = 4.702f / extent[axes[2]];
            for (auto& t : surface) for (auto& p : t)
            {
                const PxVec3 v = (p - center) * scale;
                p = PxVec3(v[axes[1]], v[axes[0]], v[axes[2]]);
            }
            cc::cache cache;
            auto start = std::chrono::steady_clock::now();
            auto hulls = cache.get(surface, params, insertion);
            const double build_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            start = std::chrono::steady_clock::now();
            auto cached = cache.get(surface, params, insertion);
            const double cache_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
            float total = 0;
            unsigned int vertices = 0;
            for (const auto& hull : hulls) { total += cc::volume(*hull); vertices += hull->getNbVertices(); }
            auto root = cc::cook(cc::points_of(surface), params, insertion);
            check(root && !hulls.empty(), "asset hull cooking succeeds");
            printf("asset: %zu triangles, %zu hulls, %u vertices, volume %.3f vs single %.3f, build %.1f ms, cached %.1f ms\n",
                surface.size(), hulls.size(), vertices, total, cc::volume(*root), build_ms, cache_ms);
            validate(surface, hulls, false);
            validate(surface, cached, false);
            for (size_t i = 0; i < hulls.size(); ++i)
                check(fabsf(cc::volume(*hulls[i]) - cc::volume(*cached[i])) < 0.0001f, "asset cache preserves hull volume");
        }
        puts("PASS chassis collision geometry");
    }
    catch (const std::exception& e) { fprintf(stderr, "FAIL: %s\n", e.what()); foundation->release(); return 1; }
    foundation->release();
    return 0;
}
