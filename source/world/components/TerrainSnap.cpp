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
    namespace { vector<uint64_t> edit_targets; }
    void Terrain::SetEditTargets(const vector<uint64_t>& entities) { edit_targets = entities; }

    using namespace terrain_common;

    namespace
    {
        bool is_entity_or_descendant(Entity* candidate, Entity* root)
        {
            if (!candidate || !root)
            {
                return false;
            }

            return candidate == root || candidate->IsDescendantOf(root);
        }

        bool is_terrain_tile_or_water(Entity* entity)
        {
            if (!entity)
            {
                return false;
            }

            if (entity->GetComponent<Water>())
            {
                return true;
            }

            for (Entity* current = entity; current; current = current->GetParent())
            {
                if (current->GetComponent<Terrain>())
                {
                    return true;
                }
            }

            return false;
        }

        // control points and spawned props are created and placed by the spline itself, moving
        // them from the outside would only be undone on the next regeneration
        bool is_spline_owned(Entity* entity)
        {
            const string& name = entity->GetObjectName();
            return name.find("spline_point_") == 0 || name.find("spline_instance_") == 0;
        }

        bool has_spline_component(Entity* entity)
        {
            return entity && entity->GetComponent<Spline>();
        }

        bool has_spline_ancestor(Entity* entity)
        {
            for (Entity* current = entity; current; current = current->GetParent())
            {
                if (has_spline_component(current))
                {
                    return true;
                }
            }

            return false;
        }

        bool entity_has_spline(Entity* entity)
        {
            if (!entity)
            {
                return false;
            }

            if (has_spline_component(entity))
            {
                return true;
            }

            vector<Entity*> descendants;
            entity->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                if (has_spline_component(descendant))
                {
                    return true;
                }
            }

            return false;
        }

        bool selection_has_spline(const vector<Entity*>& entities)
        {
            for (Entity* entity : entities)
            {
                if (entity_has_spline(entity))
                {
                    return true;
                }
            }

            return false;
        }

        bool entity_has_snappable_mesh(Entity* entity)
        {
            if (!entity)
            {
                return false;
            }

            if (entity->GetComponent<Terrain>() || entity->GetComponent<Water>())
            {
                return false;
            }

            // roads and other splines snap via control points, not as a rigid mesh, and whatever
            // the spline spawned is placed by the spline itself, but a hand placed wall parented
            // to a road is ordinary geometry and has to land like everything else
            if (has_spline_component(entity))
            {
                return false;
            }

            for (Entity* current = entity; current; current = current->GetParent())
            {
                if (is_spline_owned(current))
                {
                    return false;
                }
            }

            Render* render = entity->GetComponent<Render>();
            return render && render->GetMesh();
        }

        void collect_spline_entities(Entity* root, vector<Entity*>& out)
        {
            if (!root)
            {
                return;
            }

            if (root->GetComponent<Spline>())
            {
                out.push_back(root);
            }

            vector<Entity*> descendants;
            root->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                if (descendant && descendant->GetComponent<Spline>())
                {
                    out.push_back(descendant);
                }
            }
        }

        void collect_snappable_entities(Entity* root, vector<Entity*>& out)
        {
            if (!root)
            {
                return;
            }

            if (entity_has_snappable_mesh(root))
            {
                out.push_back(root);
            }

            vector<Entity*> descendants;
            root->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                if (entity_has_snappable_mesh(descendant))
                {
                    out.push_back(descendant);
                }
            }
        }

        // topmost entities whose whole subtree missed the snap, moving one of these cannot
        // disturb anything that has already been placed
        void collect_unsnapped_roots(
            Entity* root,
            const unordered_set<Entity*>& snapped,
            const unordered_set<Entity*>& has_snapped_below,
            vector<Entity*>& out
        )
        {
            if (!root || is_terrain_tile_or_water(root))
            {
                return;
            }

            // a spline rides its own path, but a camera or marker parented to it is just cargo
            // and still has to come down with everything else
            if (root->GetComponent<Spline>())
            {
                const uint32_t spline_child_count = root->GetChildrenCount();
                for (uint32_t i = 0; i < spline_child_count; i++)
                {
                    Entity* child = root->GetChildByIndex(i);
                    if (!child || is_spline_owned(child))
                    {
                        continue;
                    }

                    collect_unsnapped_roots(child, snapped, has_snapped_below, out);
                }

                return;
            }

            // everything under something that snapped already came along for the ride
            if (snapped.count(root) > 0)
            {
                return;
            }

            if (has_snapped_below.count(root) == 0)
            {
                out.push_back(root);
                return;
            }

            const uint32_t child_count = root->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                collect_unsnapped_roots(root->GetChildByIndex(i), snapped, has_snapped_below, out);
            }
        }

        void apply_surface_alignment(Entity* entity, const Vector3& normal)
        {
            Vector3 forward = entity->GetForward();
            forward.y = 0.0f;
            if (forward.LengthSquared() < epsilon)
            {
                forward = Vector3::Forward;
            }
            else
            {
                forward.Normalize();
            }

            const Quaternion yaw = Quaternion::FromLookRotation(forward, Vector3::Up);
            const Quaternion align = Quaternion::FromRotation(Vector3::Up, normal.Normalized());
            entity->SetRotation(align * yaw);
        }

        // world aabb recomputed here, the cached one lags a tick behind transform changes
        bool get_world_aabb(Entity* entity, BoundingBox& aabb_out)
        {
            Render* render = entity ? entity->GetComponent<Render>() : nullptr;
            if (!render || !render->GetMesh())
            {
                return false;
            }

            aabb_out = render->HasInstancing()
                ? render->GetBoundingBox()
                : render->GetBoundingBoxMesh() * entity->GetMatrix();

            return true;
        }

        // a probe grid coarser than the heightfield steps straight over ridges, and the mesh that
        // lands between two probes ends up under the ground, so follow the grid rather than a
        // fixed spacing, the cap only bites on the kilometre wide ground slabs
        uint32_t footprint_samples(float span, float grid_step)
        {
            const float step     = max(grid_step, 0.5f);
            const uint32_t count = static_cast<uint32_t>(max(span, 0.0f) / step) + 2;
            return min(count, 128u);
        }

        // lowest point of the entity in world space, its origin when it carries no mesh
        float entity_bottom(Entity* entity)
        {
            BoundingBox aabb;
            if (entity && get_world_aabb(entity, aabb))
            {
                return aabb.GetMin().y;
            }

            return entity ? entity->GetPosition().y : 0.0f;
        }

        struct MeshFootprint
        {
            Entity* entity    = nullptr;
            BoundingBox aabb;
            float area        = 0.0f;
            float span        = 0.0f;
            float height      = 0.0f;
            float bottom      = 0.0f;
        };

        struct SnapPlatformDesc
        {
            uint64_t entity_id  = 0;
            float min_x         = 0.0f;
            float min_z         = 0.0f;
            float max_x         = 0.0f;
            float max_z         = 0.0f;
            float center_x      = 0.0f;
            float center_z      = 0.0f;
            float half_x        = 0.0f;
            float half_z        = 0.0f;
            float yaw           = 0.0f;
            float height        = 0.0f;
            float margin        = 0.0f;
            uint32_t mesh_count = 0;
            vector<Entity*> floors;
        };

        bool pads_match(const TerrainPlatform& a, const TerrainPlatform& b)
        {
            const float pos_eps = 0.05f;
            const float ang_eps = 0.01f;
            return a.entity_id == b.entity_id &&
                fabsf(a.center_x - b.center_x) < pos_eps &&
                fabsf(a.center_z - b.center_z) < pos_eps &&
                fabsf(a.half_x - b.half_x) < pos_eps &&
                fabsf(a.half_z - b.half_z) < pos_eps &&
                fabsf(a.height - b.height) < pos_eps &&
                fabsf(a.yaw - b.yaw) < ang_eps;
        }

        bool pads_same_place(const TerrainPlatform& a, const TerrainPlatform& b)
        {
            const float pos_eps = 0.5f;
            const float ang_eps = 0.05f;
            return a.entity_id != 0 &&
                a.entity_id == b.entity_id &&
                fabsf(a.center_x - b.center_x) < pos_eps &&
                fabsf(a.center_z - b.center_z) < pos_eps &&
                fabsf(a.half_x - b.half_x) < pos_eps &&
                fabsf(a.half_z - b.half_z) < pos_eps &&
                fabsf(a.yaw - b.yaw) < ang_eps;
        }

        string pad_refine_name(uint64_t entity_id)
        {
            return "terrain_refine_" + to_string(entity_id);
        }

        bool is_pad_refine_name(const string& name)
        {
            return name.rfind("terrain_refine", 0) == 0;
        }

        // entity delete is deferred, drop the raw mesh pointer before the shared mesh is freed
        void detach_render_mesh(Entity* entity)
        {
            if (!entity)
            {
                return;
            }

            if (Render* render = entity->GetComponent<Render>())
            {
                render->ClearMesh();
            }
        }

        bool fit_floor_obb(const vector<MeshFootprint*>& used, SnapPlatformDesc& out)
        {
            if (used.empty() || !used[0] || !used[0]->entity)
            {
                return false;
            }

            Entity* seed = used[0]->entity;
            Vector3 axis_x = seed->GetRight();
            axis_x.y = 0.0f;
            if (axis_x.LengthSquared() < 1e-8f)
            {
                axis_x = Vector3::Right;
            }
            axis_x.Normalize();
            const float yaw = atan2f(axis_x.z, axis_x.x);
            axis_x = Vector3(cosf(yaw), 0.0f, sinf(yaw));
            const Vector3 axis_z(-sinf(yaw), 0.0f, cosf(yaw));

            float min_u = numeric_limits<float>::max();
            float max_u = -numeric_limits<float>::max();
            float min_v = numeric_limits<float>::max();
            float max_v = -numeric_limits<float>::max();
            uint32_t corner_count = 0;

            for (MeshFootprint* mesh : used)
            {
                if (!mesh || !mesh->entity)
                {
                    continue;
                }

                Render* render = mesh->entity->GetComponent<Render>();
                if (!render || !render->GetMesh())
                {
                    continue;
                }

                array<Vector3, 8> local_corners;
                render->GetBoundingBoxMesh().GetCorners(&local_corners);
                const Matrix world = mesh->entity->GetMatrix();
                for (const Vector3& local : local_corners)
                {
                    const Vector3 world_p = world * local;
                    const float u = world_p.x * axis_x.x + world_p.z * axis_x.z;
                    const float v = world_p.x * axis_z.x + world_p.z * axis_z.z;
                    min_u = min(min_u, u);
                    max_u = max(max_u, u);
                    min_v = min(min_v, v);
                    max_v = max(max_v, v);
                    corner_count++;
                }
            }

            if (corner_count == 0 || max_u <= min_u || max_v <= min_v)
            {
                return false;
            }

            const float span = max(max_u - min_u, max_v - min_v);
            const float overhang = min(max(span * 0.02f, 1.0f), 4.0f);
            min_u -= overhang;
            max_u += overhang;
            min_v -= overhang;
            max_v += overhang;

            const float mid_u = (min_u + max_u) * 0.5f;
            const float mid_v = (min_v + max_v) * 0.5f;
            out.yaw      = yaw;
            out.half_x   = (max_u - min_u) * 0.5f;
            out.half_z   = (max_v - min_v) * 0.5f;
            out.center_x = axis_x.x * mid_u + axis_z.x * mid_v;
            out.center_z = axis_x.z * mid_u + axis_z.z * mid_v;
            out.margin   = min(max(span * 0.06f, 2.0f), 6.0f);
            obb_write_aabb(
                out.center_x,
                out.center_z,
                out.half_x,
                out.half_z,
                out.yaw,
                out.min_x,
                out.min_z,
                out.max_x,
                out.max_z
            );

            return out.half_x > 0.05f && out.half_z > 0.05f;
        }

        bool name_looks_like_floor(const string& name)
        {
            string lower;
            lower.reserve(name.size());
            for (char c : name)
            {
                lower.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
            }

            static const char* keys[] =
            {
                "floor", "apron", "tarmac", "pad", "slab", "asphalt",
                "plaza", "foundation", "courtyard"
            };

            for (const char* key : keys)
            {
                if (lower.find(key) != string::npos)
                {
                    return true;
                }
            }

            return false;
        }

        bool name_looks_like_prop(const string& name)
        {
            string lower;
            lower.reserve(name.size());
            for (char c : name)
            {
                lower.push_back(static_cast<char>(tolower(static_cast<unsigned char>(c))));
            }

            static const char* keys[] =
            {
                "barrier", "fence", "wall", "pillar", "pole", "curb", "lamp"
            };

            for (const char* key : keys)
            {
                if (lower.find(key) != string::npos)
                {
                    return true;
                }
            }

            return false;
        }

        bool is_floor_like(const MeshFootprint& mesh)
        {
            if (has_spline_ancestor(mesh.entity))
            {
                return false;
            }

            if (mesh.entity && name_looks_like_prop(mesh.entity->GetObjectName()))
            {
                return false;
            }

            const bool named     = mesh.entity && name_looks_like_floor(mesh.entity->GetObjectName());
            const float min_span = named ? 8.0f : 20.0f;
            if (mesh.span < min_span)
            {
                return false;
            }

            // km-scale ground planes and roads are not a pad under a building
            if (mesh.span > 1200.0f || (mesh.height < 1.25f && mesh.span > 250.0f))
            {
                return false;
            }

            const float extent_x   = mesh.aabb.GetMax().x - mesh.aabb.GetMin().x;
            const float extent_z   = mesh.aabb.GetMax().z - mesh.aabb.GetMin().z;
            const float short_side = min(extent_x, extent_z);
            const float long_side  = max(extent_x, extent_z);
            const bool compact     = short_side > 0.1f && (long_side / short_side) <= 4.0f;

            // a winding road aabb is a long strip, flattening it plazas the hillside
            if (!named && !compact)
            {
                return false;
            }

            // hangars and houses are thick, aprons and roads are thin slabs
            // a compact city or station can be tall, the pad is still the aabb bottom
            if (!compact && mesh.height > 8.0f && mesh.height > mesh.span * 0.2f)
            {
                return false;
            }

            return true;
        }

        float live_pad_span_cap(float half_x, float half_z)
        {
            const float span       = max(half_x, half_z) * 2.0f;
            const float short_side = min(half_x, half_z) * 2.0f;
            if (short_side < 0.1f)
            {
                return 400.0f;
            }

            const float aspect = span / short_side;
            return aspect <= 3.0f ? 1200.0f : 400.0f;
        }

        bool occupant_skippable(Entity* entity)
        {
            if (!entity || !entity->GetActive())
            {
                return true;
            }

            if (entity->HasTag("terrain_prop") ||
                entity->GetObjectName() == "live_pad" ||
                entity->GetObjectName() == "pad_overlay" ||
                is_pad_refine_name(entity->GetObjectName()))
            {
                return true;
            }

            if (is_terrain_tile_or_water(entity))
            {
                return true;
            }

            if (entity->GetComponent<Camera>() || entity->GetComponent<Light>())
            {
                return true;
            }

            return false;
        }

        bool mesh_sits_on_pad(Entity* entity, const TerrainPlatform& pad)
        {
            BoundingBox aabb;
            if (!get_world_aabb(entity, aabb))
            {
                return false;
            }

            const Vector3 mn = aabb.GetMin();
            const Vector3 mx = aabb.GetMax();
            if (mn.y > pad.height + 12.0f || mx.y < pad.height - 4.0f)
            {
                return false;
            }

            const float xs[3] = { mn.x, (mn.x + mx.x) * 0.5f, mx.x };
            const float zs[3] = { mn.z, (mn.z + mx.z) * 0.5f, mx.z };
            for (float x : xs)
            {
                for (float z : zs)
                {
                    if (obb_outside_distance(x, z, pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw) <= 0.0f)
                    {
                        return true;
                    }
                }
            }

            return false;
        }

        Entity* platform_find_occupant(const TerrainPlatform& pad)
        {
            for (Entity* entity : World::GetEntitiesWithRender())
            {
                if (occupant_skippable(entity))
                {
                    continue;
                }

                if (mesh_sits_on_pad(entity, pad))
                {
                    return entity;
                }
            }

            return nullptr;
        }

        bool platform_occupant_alive(TerrainPlatform& pad, Entity* known_occupant = nullptr)
        {
            if (pad.entity_id != 0)
            {
                Entity* entity = known_occupant ? known_occupant : World::GetEntityById(pad.entity_id);
                if (entity)
                {
                    return entity->GetActive();
                }
            }

            // no id or a stale one, a duplicated or reimported building keeps its pad by sitting on
            // it, the pad rebinds to whatever it finds so the walk does not repeat every tick
            Entity* occupant = platform_find_occupant(pad);
            if (occupant)
            {
                pad.entity_id = occupant->GetObjectId();
                pad.anchored  = false;
                return true;
            }

            return false;
        }

        bool aabb_xz_near(const BoundingBox& a, const BoundingBox& b, float gap)
        {
            const bool overlap_x =
                a.GetMin().x - gap <= b.GetMax().x &&
                b.GetMin().x - gap <= a.GetMax().x;
            const bool overlap_z =
                a.GetMin().z - gap <= b.GetMax().z &&
                b.GetMin().z - gap <= a.GetMax().z;

            return overlap_x && overlap_z;
        }

        bool collect_mesh_footprints(const vector<Entity*>& entities, vector<MeshFootprint>& out)
        {
            unordered_set<Entity*> unique;
            for (Entity* entity : entities)
            {
                vector<Entity*> meshes;
                collect_snappable_entities(entity, meshes);
                for (Entity* part : meshes)
                {
                    if (!part || !unique.insert(part).second)
                    {
                        continue;
                    }

                    BoundingBox aabb;
                    if (!get_world_aabb(part, aabb))
                    {
                        continue;
                    }

                    const Vector3 box_min = aabb.GetMin();
                    const Vector3 box_max = aabb.GetMax();
                    const bool usable =
                        isfinite(box_min.x) && isfinite(box_min.z) &&
                        isfinite(box_max.x) && isfinite(box_max.z) &&
                        box_min.x <= box_max.x && box_min.z <= box_max.z;

                    if (!usable)
                    {
                        continue;
                    }

                    MeshFootprint mesh;
                    mesh.entity = part;
                    mesh.aabb   = aabb;
                    mesh.span   = max(box_max.x - box_min.x, box_max.z - box_min.z);
                    mesh.height = box_max.y - box_min.y;
                    mesh.area   = max(box_max.x - box_min.x, 0.0f) * max(box_max.z - box_min.z, 0.0f);
                    mesh.bottom = box_min.y;
                    out.push_back(mesh);
                }
            }

            return !out.empty();
        }

        bool find_snap_platform(
            const vector<Entity*>& entities,
            bool require_floor,
            SnapPlatformDesc& out
        )
        {
            vector<MeshFootprint> meshes;
            if (!collect_mesh_footprints(entities, meshes))
            {
                return false;
            }

            vector<MeshFootprint*> floors;
            for (MeshFootprint& mesh : meshes)
            {
                if (is_floor_like(mesh))
                {
                    floors.push_back(&mesh);
                }
            }

            vector<MeshFootprint*> used;
            if (!floors.empty())
            {
                float lowest = numeric_limits<float>::max();
                for (MeshFootprint* mesh : floors)
                {
                    lowest = min(lowest, mesh->bottom);
                }

                vector<MeshFootprint*> band;
                for (MeshFootprint* mesh : floors)
                {
                    if (mesh->bottom <= lowest + 2.0f)
                    {
                        band.push_back(mesh);
                    }
                }

                MeshFootprint* seed = nullptr;
                float best_area     = -1.0f;
                for (MeshFootprint* mesh : band)
                {
                    if (mesh->area > best_area)
                    {
                        best_area = mesh->area;
                        seed      = mesh;
                    }
                }

                if (!seed)
                {
                    return false;
                }

                used.push_back(seed);
                const float gap = max(2.0f, min(seed->span * 0.08f, 40.0f));
                bool added      = true;
                while (added)
                {
                    added = false;
                    for (MeshFootprint* mesh : band)
                    {
                        if (find(used.begin(), used.end(), mesh) != used.end())
                        {
                            continue;
                        }

                        bool near = false;
                        for (MeshFootprint* member : used)
                        {
                            if (aabb_xz_near(member->aabb, mesh->aabb, gap))
                            {
                                near = true;
                                break;
                            }
                        }

                        if (near)
                        {
                            used.push_back(mesh);
                            added = true;
                        }
                    }
                }

                out.entity_id = seed->entity ? seed->entity->GetObjectId() : 0;
                out.height    = seed->bottom;
            }
            else
            {
                if (require_floor)
                {
                    return false;
                }

                for (MeshFootprint& mesh : meshes)
                {
                    used.push_back(&mesh);
                }

                out.entity_id = (!entities.empty() && entities[0]) ? entities[0]->GetObjectId() : 0;
                out.height    = numeric_limits<float>::max();
                for (MeshFootprint* mesh : used)
                {
                    out.height = min(out.height, mesh->bottom);
                }
            }

            if (used.empty())
            {
                return false;
            }

            if (!fit_floor_obb(used, out))
            {
                return false;
            }

            out.mesh_count = static_cast<uint32_t>(used.size());
            out.floors.clear();
            out.floors.reserve(used.size());
            for (MeshFootprint* mesh : used)
            {
                if (mesh && mesh->entity)
                {
                    out.floors.push_back(mesh->entity);
                }
            }

            return true;
        }

        void level_selection(const vector<Entity*>& entities)
        {
            unordered_set<Entity*> unique;
            auto level = [&](Entity* entity)
            {
                if (!entity || !unique.insert(entity).second)
                {
                    return;
                }

                if (entity->GetComponent<Spline>())
                {
                    return;
                }

                apply_surface_alignment(entity, Vector3::Up);
            };

            for (Entity* entity : entities)
            {
                if (!entity || is_terrain_tile_or_water(entity))
                {
                    continue;
                }

                level(entity);

                vector<Entity*> descendants;
                entity->GetDescendants(&descendants);
                for (Entity* descendant : descendants)
                {
                    if (!descendant || is_spline_owned(descendant) || is_terrain_tile_or_water(descendant))
                    {
                        continue;
                    }

                    level(descendant);
                }
            }
        }

        bool entity_in_selection_tree(Entity* entity, const vector<Entity*>& selection)
        {
            if (!entity)
            {
                return false;
            }

            for (Entity* root : selection)
            {
                if (is_entity_or_descendant(entity, root))
                {
                    return true;
                }
            }

            return false;
        }

        bool sibling_branch_has_spline(Entity* parent, Entity* child_branch)
        {
            if (!parent)
            {
                return false;
            }

            if (has_spline_component(parent))
            {
                return true;
            }

            const uint32_t child_count = parent->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                Entity* child = parent->GetChildByIndex(i);
                if (!child || child == child_branch)
                {
                    continue;
                }

                if (entity_has_spline(child))
                {
                    return true;
                }
            }

            return false;
        }

        Entity* floor_movable_root(Entity* floor, const vector<Entity*>& selection)
        {
            if (!floor || has_spline_component(floor) || has_spline_ancestor(floor))
            {
                return nullptr;
            }

            Entity* current = floor;
            while (Entity* parent = current->GetParent())
            {
                if (!entity_in_selection_tree(parent, selection))
                {
                    break;
                }

                if (has_spline_component(parent) || sibling_branch_has_spline(parent, current))
                {
                    break;
                }

                current = parent;
            }

            if (has_spline_component(current))
            {
                return nullptr;
            }

            return current;
        }

        void translate_floor_cluster(
            const vector<Entity*>& floors,
            const vector<Entity*>& selection,
            float delta_y
        )
        {
            if (fabsf(delta_y) < 0.0001f)
            {
                return;
            }

            vector<Entity*> roots;
            unordered_set<Entity*> unique;
            for (Entity* floor : floors)
            {
                Entity* root = floor_movable_root(floor, selection);
                if (!root || !unique.insert(root).second)
                {
                    continue;
                }

                roots.push_back(root);
            }

            for (Entity* entity : roots)
            {
                bool nested = false;
                for (Entity* other : roots)
                {
                    if (other && other != entity && entity->IsDescendantOf(other))
                    {
                        nested = true;
                        break;
                    }
                }

                if (nested)
                {
                    continue;
                }

                const Vector3 position = entity->GetPosition();
                entity->SetPosition(Vector3(position.x, position.y + delta_y, position.z));
            }
        }

        bool sample_terrain_max(
            Terrain* terrain,
            float center_x,
            float center_z,
            float half_x,
            float half_z,
            float yaw,
            float& max_out
        )
        {
            if (!terrain)
            {
                return false;
            }

            const TerrainGridMapping mapping = terrain->GetGridMapping();
            const uint32_t samples_x = footprint_samples(half_x * 2.0f, mapping.scale_x);
            const uint32_t samples_z = footprint_samples(half_z * 2.0f, mapping.scale_z);
            const Vector3 axis_x(cosf(yaw), 0.0f, sinf(yaw));
            const Vector3 axis_z(-sinf(yaw), 0.0f, cosf(yaw));

            float highest  = -numeric_limits<float>::max();
            uint32_t count = 0;

            for (uint32_t iz = 0; iz < samples_z; iz++)
            {
                for (uint32_t ix = 0; ix < samples_x; ix++)
                {
                    const float u = (samples_x > 1) ? (static_cast<float>(ix) / static_cast<float>(samples_x - 1)) * 2.0f - 1.0f : 0.0f;
                    const float v = (samples_z > 1) ? (static_cast<float>(iz) / static_cast<float>(samples_z - 1)) * 2.0f - 1.0f : 0.0f;
                    const float x = center_x + axis_x.x * (u * half_x) + axis_z.x * (v * half_z);
                    const float z = center_z + axis_x.z * (u * half_x) + axis_z.z * (v * half_z);

                    float height = 0.0f;
                    if (terrain->SampleHeight(x, z, height))
                    {
                        highest = max(highest, height);
                        count++;
                    }
                }
            }

            if (count == 0)
            {
                return false;
            }

            max_out = highest;
            return true;
        }

        // drop a vertical ray from the top of the entity, the heightfield is always the floor
        bool find_snap_surface(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            Vector3& position_out,
            Vector3& normal_out
        )
        {
            const Vector3 position = entity->GetPosition();

            float start_y = position.y + 1.0f;
            BoundingBox entity_aabb;
            const bool has_aabb = get_world_aabb(entity, entity_aabb);
            if (has_aabb)
            {
                start_y = max(start_y, entity_aabb.GetMax().y + 0.05f);
            }

            const Vector3 origin(position.x, start_y, position.z);
            const float max_distance = max(start_y - position.y, 1.0f) + 100000.0f;

            bool found = false;
            float best_y = -numeric_limits<float>::max();
            Vector3 best_normal = Vector3::Up;

            // keep the highest surface that is still at or below the entity
            auto consider = [&start_y, &found, &best_y, &best_normal](float hit_y, const Vector3& hit_normal)
            {
                if (hit_y > start_y || hit_y <= best_y)
                {
                    return;
                }

                found       = true;
                best_y      = hit_y;
                best_normal = hit_normal.LengthSquared() > epsilon
                    ? hit_normal.Normalized()
                    : Vector3::Up;
            };

            // static physics first, buildings and props with colliders
            {
                PhysicsRaycastHit physics_hit;
                if (PhysicsWorld::RaycastStatic(
                    origin,
                    Vector3::Down,
                    max_distance,
                    physics_hit,
                    entity
                ))
                {
                    // never rest on a member of the same snap batch, it may still be floating,
                    // and let the heightfield speak for the terrain, its collider can be a rebuild behind
                    const bool usable =
                        ignored.find(physics_hit.entity) == ignored.end() &&
                        !is_terrain_tile_or_water(physics_hit.entity);

                    if (usable)
                    {
                        consider(physics_hit.position.y, physics_hit.normal);
                    }
                }
            }

            // render meshes without physics, same idea as viewport picking
            {
                const Ray ray(origin, Vector3::Down);
                vector<uint32_t> indices;
                vector<RHI_Vertex_PosTexNorTan> vertices;

                for (Entity* candidate : World::GetEntities())
                {
                    if (!candidate || is_entity_or_descendant(candidate, entity))
                    {
                        continue;
                    }

                    // terrain uses the heightfield path below, skip huge tile meshes
                    if (is_terrain_tile_or_water(candidate))
                    {
                        continue;
                    }

                    if (ignored.find(candidate) != ignored.end())
                    {
                        continue;
                    }

                    Render* render = candidate->GetComponent<Render>();
                    if (!render)
                    {
                        continue;
                    }

                    BoundingBox candidate_aabb;
                    if (!get_world_aabb(candidate, candidate_aabb))
                    {
                        continue;
                    }

                    // cheap reject, the box must sit under the ray and reach above the current best
                    if (ray.HitDistance(candidate_aabb) == numeric_limits<float>::infinity() ||
                        candidate_aabb.GetMax().y <= best_y)
                    {
                        continue;
                    }

                    indices.clear();
                    vertices.clear();
                    render->GetGeometry(&indices, &vertices);
                    if (indices.size() < 3 || vertices.empty())
                    {
                        continue;
                    }

                    const Matrix& transform = candidate->GetMatrix();
                    for (size_t i = 0; i + 2 < indices.size(); i += 3)
                    {
                        Vector3 p1(vertices[indices[i]].pos);
                        Vector3 p2(vertices[indices[i + 1]].pos);
                        Vector3 p3(vertices[indices[i + 2]].pos);
                        p1 = p1 * transform;
                        p2 = p2 * transform;
                        p3 = p3 * transform;

                        Vector3 triangle_normal;
                        const float distance = ray.HitDistance(
                            p1, p2, p3, &triangle_normal
                        );
                        if (distance == numeric_limits<float>::infinity())
                        {
                            continue;
                        }

                        consider(start_y - distance, triangle_normal);
                    }
                }
            }

            // the heightfield is the floor, it also lifts entities that ended up buried, probed
            // across the whole footprint so a wide flat mesh never gets half swallowed by a rise
            if (terrain)
            {
                float span_min_x = position.x;
                float span_max_x = position.x;
                float span_min_z = position.z;
                float span_max_z = position.z;
                if (has_aabb)
                {
                    span_min_x = entity_aabb.GetMin().x;
                    span_max_x = entity_aabb.GetMax().x;
                    span_min_z = entity_aabb.GetMin().z;
                    span_max_z = entity_aabb.GetMax().z;
                }

                const TerrainGridMapping mapping = terrain->GetGridMapping();
                const uint32_t samples_x         = footprint_samples(span_max_x - span_min_x, mapping.scale_x);
                const uint32_t samples_z         = footprint_samples(span_max_z - span_min_z, mapping.scale_z);

                float terrain_height = -numeric_limits<float>::max();
                bool has_terrain     = false;

                for (uint32_t iz = 0; iz < samples_z; iz++)
                {
                    for (uint32_t ix = 0; ix < samples_x; ix++)
                    {
                        const float u = (samples_x > 1) ? static_cast<float>(ix) / static_cast<float>(samples_x - 1) : 0.5f;
                        const float v = (samples_z > 1) ? static_cast<float>(iz) / static_cast<float>(samples_z - 1) : 0.5f;
                        const float x = span_min_x + (span_max_x - span_min_x) * u;
                        const float z = span_min_z + (span_max_z - span_min_z) * v;

                        float sampled = 0.0f;
                        if (terrain->SampleHeight(x, z, sampled) && sampled > terrain_height)
                        {
                            terrain_height = sampled;
                            has_terrain    = true;
                        }
                    }
                }

                if (has_terrain && (!found || terrain_height > best_y))
                {
                    found  = true;
                    best_y = terrain_height;

                    // the normal still comes from under the pivot, the highest corner is a poor guide
                    Vector3 terrain_normal = Vector3::Up;
                    if (terrain->SampleNormal(position.x, position.z, terrain_normal))
                    {
                        best_normal = terrain_normal;
                    }
                }
            }

            if (!found)
            {
                return false;
            }

            position_out = Vector3(position.x, best_y, position.z);
            normal_out   = best_normal;
            return true;
        }

        bool snap_mesh_entity(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            float offset
        )
        {
            if (!entity || !entity_has_snappable_mesh(entity))
            {
                return false;
            }

            Vector3 snap_position;
            Vector3 snap_normal = Vector3::Up;
            if (!find_snap_surface(entity, terrain, ignored, snap_position, snap_normal))
            {
                return false;
            }

            // a wide slab has to stay level, tilting a three kilometre plane by a single degree
            // swings its far corner twenty six metres, so only small props follow the slope
            float span = 0.0f;
            BoundingBox pre_alignment_aabb;
            if (get_world_aabb(entity, pre_alignment_aabb))
            {
                const float span_x = pre_alignment_aabb.GetMax().x - pre_alignment_aabb.GetMin().x;
                const float span_z = pre_alignment_aabb.GetMax().z - pre_alignment_aabb.GetMin().z;
                span               = max(span_x, span_z);
                if (span > 50.0f)
                {
                    snap_normal = Vector3::Up;
                }
            }

            // rotate first, tilting moves the lowest point of the mesh
            apply_surface_alignment(entity, snap_normal);

            // rest the base of the mesh on the surface, the pivot is rarely at the bottom
            float pivot_to_bottom = 0.0f;
            BoundingBox aabb;
            if (get_world_aabb(entity, aabb))
            {
                pivot_to_bottom = entity->GetPosition().y - aabb.GetMin().y;
            }

            // a surface resting exactly on the ground loses the depth fight and the terrain shows
            // through it, and the bigger the slab the further away it is seen from, so the gap has
            // to grow with it or the far end starts flickering through
            const float clearance = min(max(span * 0.001f, 0.05f), 0.5f);

            entity->SetPosition(Vector3(
                snap_position.x,
                snap_position.y + pivot_to_bottom + offset + clearance,
                snap_position.z
            ));

            return true;
        }

        bool snap_entity_position(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            float offset
        )
        {
            if (!entity)
            {
                return false;
            }

            Vector3 snap_position;
            Vector3 snap_normal = Vector3::Up;
            if (!find_snap_surface(entity, terrain, ignored, snap_position, snap_normal))
            {
                return false;
            }

            snap_position.y += offset;
            entity->SetPosition(snap_position);
            return true;
        }

        bool snap_spline_to_terrain(
            Entity* entity,
            Terrain* terrain,
            const unordered_set<Entity*>& ignored,
            float offset
        )
        {
            if (!entity)
            {
                return false;
            }

            Spline* spline = entity->GetComponent<Spline>();
            if (!spline)
            {
                return false;
            }

            // dropping only the control points leaves the pivot where it was authored, and every
            // wall, light and camera parented to the road keeps riding it, so land the pivot too,
            // the control points are set in world space right after and do not care where it sits
            const float clearance = 0.05f;
            if (terrain)
            {
                const Vector3 pivot = entity->GetPosition();
                float pivot_ground  = 0.0f;
                if (terrain->SampleHeight(pivot.x, pivot.z, pivot_ground))
                {
                    entity->SetPosition(Vector3(pivot.x, pivot_ground + offset + clearance, pivot.z));
                }
            }

            const uint32_t child_count = entity->GetChildrenCount();
            for (uint32_t i = 0; i < child_count; i++)
            {
                Entity* child = entity->GetChildByIndex(i);
                if (!child)
                {
                    continue;
                }

                if (child->GetObjectName().find("spline_point_") != 0)
                {
                    continue;
                }

                const Vector3 point = child->GetPosition();
                float ground        = 0.0f;
                if (terrain && terrain->SampleHeight(point.x, point.z, ground))
                {
                    child->SetPosition(Vector3(point.x, ground + offset + clearance, point.z));
                }
                else
                {
                    snap_entity_position(child, terrain, ignored, offset + clearance);
                }
            }

            // denser samples between points follow the heightfield
            spline->SetConformToTerrain(true);
            if (spline->GetMeshEnabled() || spline->HasRoadMesh())
            {
                spline->GenerateRoadMesh();
            }

            // props and lights ride the spline frames, rebuild them onto the new path
            if (spline->HasSpawnedInstances())
            {
                spline->SpawnInstances();
            }

            return true;
        }

        uint32_t snap_splines_in_selection(const vector<Entity*>& entities, Terrain* terrain, float offset)
        {
            unordered_set<Entity*> unique;
            uint32_t count = 0;

            for (Entity* entity : entities)
            {
                vector<Entity*> splines;
                collect_spline_entities(entity, splines);
                for (Entity* spline_entity : splines)
                {
                    if (!spline_entity || !unique.insert(spline_entity).second)
                    {
                        continue;
                    }

                    unordered_set<Entity*> ignored;
                    if (snap_spline_to_terrain(spline_entity, terrain, ignored, offset))
                    {
                        count++;
                    }
                }
            }

            return count;
        }

        bool mesh_is_buried(Entity* entity, Terrain* terrain)
        {
            if (!entity || !terrain)
            {
                return false;
            }

            BoundingBox aabb;
            if (!get_world_aabb(entity, aabb))
            {
                return false;
            }

            float ground = 0.0f;
            if (!terrain->SampleHeight(entity->GetPosition().x, entity->GetPosition().z, ground))
            {
                return false;
            }

            return aabb.GetMin().y < (ground - 0.05f);
        }

        uint32_t snap_cargo_after_platform(
            const vector<Entity*>& entities,
            const vector<Entity*>& floors,
            Terrain* terrain,
            float offset
        )
        {
            unordered_set<Entity*> floor_tree;
            for (Entity* floor : floors)
            {
                Entity* root = floor_movable_root(floor, entities);
                if (!root)
                {
                    continue;
                }

                floor_tree.insert(root);
                vector<Entity*> descendants;
                root->GetDescendants(&descendants);
                for (Entity* descendant : descendants)
                {
                    if (descendant)
                    {
                        floor_tree.insert(descendant);
                    }
                }
            }

            unordered_set<Entity*> unique;
            uint32_t count = 0;
            for (Entity* entity : entities)
            {
                vector<Entity*> targets;
                collect_snappable_entities(entity, targets);
                for (Entity* target : targets)
                {
                    if (!target || !unique.insert(target).second)
                    {
                        continue;
                    }

                    if (floor_tree.count(target) > 0)
                    {
                        continue;
                    }

                    if (!has_spline_ancestor(target) && !mesh_is_buried(target, terrain))
                    {
                        continue;
                    }

                    unordered_set<Entity*> ignored;
                    if (snap_mesh_entity(target, terrain, ignored, offset))
                    {
                        count++;
                    }
                }
            }

            return count;
        }
    }

    bool Terrain::SnapEntityToTerrain(Entity* entity, float offset)
    {
        return SnapEntitiesToTerrain({ entity }, offset) > 0;
    }

    bool Terrain::SnapEntityToFlatTerrain(Entity* entity, float offset)
    {
        return SnapEntitiesToFlatTerrain({ entity }, offset) > 0;
    }

    uint32_t Terrain::SnapEntitiesToFlatTerrain(const vector<Entity*>& entities, float offset)
    {
        Terrain* terrain = FindActive();
        if (!terrain)
        {
            SP_LOG_WARNING("no terrain with a heightfield to flatten");
            return 0;
        }

        // a compact building floor gets a tight pad, a road drapes on the hills instead of being
        // dropped as a rigid slab and buried
        const bool had_platform = terrain->BuildSnapPlatform(entities, true);
        if (had_platform)
        {
            snap_splines_in_selection(entities, terrain, offset);
            snap_cargo_after_platform(entities, {}, terrain, offset);
            return 1;
        }

        if (selection_has_spline(entities))
        {
            return SnapEntitiesToTerrain(entities, offset, false);
        }

        if (!terrain->BuildSnapPlatform(entities, false))
        {
            SP_LOG_WARNING("nothing snappable in the selection, no footprint to flatten");
            return 0;
        }

        snap_splines_in_selection(entities, terrain, offset);
        snap_cargo_after_platform(entities, {}, terrain, offset);
        return 1;
    }


    uint32_t Terrain::SnapEntitiesToTerrain(const vector<Entity*>& entities, float offset, bool build_platform)
    {
        Terrain* terrain = FindActive();
        if (build_platform && terrain && terrain->BuildSnapPlatform(entities, true))
        {
            snap_splines_in_selection(entities, terrain, offset);
            snap_cargo_after_platform(entities, {}, terrain, offset);
            return 1;
        }

        unordered_set<Entity*> unique_splines;
        unordered_set<Entity*> unique_targets;
        vector<Entity*> splines;
        vector<Entity*> targets;

        for (Entity* entity : entities)
        {
            vector<Entity*> collected_splines;
            collect_spline_entities(entity, collected_splines);
            for (Entity* spline_entity : collected_splines)
            {
                if (unique_splines.insert(spline_entity).second)
                {
                    splines.push_back(spline_entity);
                }
            }

            vector<Entity*> collected;
            collect_snappable_entities(entity, collected);
            for (Entity* target : collected)
            {
                if (unique_targets.insert(target).second)
                {
                    targets.push_back(target);
                }
            }
        }

        // an entity that has not been snapped yet may still be floating, so it cannot serve as
        // ground, once it lands it becomes a valid surface for whatever sits on top of it
        unordered_set<Entity*> pending = unique_targets;
        for (Entity* spline_entity : splines)
        {
            pending.insert(spline_entity);

            vector<Entity*> spline_descendants;
            spline_entity->GetDescendants(&spline_descendants);
            for (Entity* descendant : spline_descendants)
            {
                pending.insert(descendant);
            }
        }

        uint32_t count = 0;
        for (Entity* spline_entity : splines)
        {
            if (snap_spline_to_terrain(spline_entity, terrain, pending, offset))
            {
                count++;
            }
        }

        // work from the ground up, the lowest entity settles first and then counts as a surface,
        // so a platform lands on the terrain and everything standing on it stacks onto the platform
        struct snap_order
        {
            Entity* entity;
            float bottom;
            uint32_t depth;
        };

        vector<snap_order> ordered;
        ordered.reserve(targets.size());

        for (Entity* target : targets)
        {
            uint32_t depth = 0;
            for (Entity* parent = target->GetParent(); parent; parent = parent->GetParent())
            {
                depth++;
            }

            // a parent sorts with its lowest batch descendant, it must never move after they settle
            float bottom = entity_bottom(target);
            vector<Entity*> descendants;
            target->GetDescendants(&descendants);
            for (Entity* descendant : descendants)
            {
                if (unique_targets.count(descendant) > 0)
                {
                    bottom = min(bottom, entity_bottom(descendant));
                }
            }

            ordered.push_back({ target, bottom, depth });
        }

        sort(ordered.begin(), ordered.end(), [](const snap_order& a, const snap_order& b)
        {
            if (a.bottom != b.bottom)
            {
                return a.bottom < b.bottom;
            }

            return a.depth < b.depth;
        });

        struct snap_shift
        {
            Vector3 position;
            float delta_y;
        };

        vector<snap_shift> shifts;
        unordered_set<Entity*> snapped;

        for (const snap_order& item : ordered)
        {
            // it has had its turn, from now on it is ground for whatever sits above it
            pending.erase(item.entity);

            const float y_before = item.entity->GetPosition().y;
            if (snap_mesh_entity(item.entity, terrain, pending, offset))
            {
                const Vector3 landed = item.entity->GetPosition();
                shifts.push_back({ landed, landed.y - y_before });
                snapped.insert(item.entity);
                count++;
            }
        }

        // lights, markers and other mesh free entities cannot be raycast against anything, so they
        // take the vertical shift of the nearest thing that did move and keep their local layout
        if (!shifts.empty())
        {
            unordered_set<Entity*> has_snapped_below;
            for (Entity* entity : snapped)
            {
                for (Entity* ancestor = entity; ancestor; ancestor = ancestor->GetParent())
                {
                    if (!has_snapped_below.insert(ancestor).second)
                    {
                        break;
                    }
                }
            }

            vector<Entity*> orphans;
            for (Entity* entity : entities)
            {
                collect_unsnapped_roots(entity, snapped, has_snapped_below, orphans);
            }

            for (Entity* orphan : orphans)
            {
                const Vector3 position = orphan->GetPosition();

                float best_distance = numeric_limits<float>::max();
                float best_delta    = 0.0f;
                for (const snap_shift& shift : shifts)
                {
                    const float delta_x  = shift.position.x - position.x;
                    const float delta_z  = shift.position.z - position.z;
                    const float distance = delta_x * delta_x + delta_z * delta_z;

                    if (distance < best_distance)
                    {
                        best_distance = distance;
                        best_delta    = shift.delta_y;
                    }
                }

                orphan->SetPosition(Vector3(position.x, position.y + best_delta, position.z));
                count++;
            }

            SP_LOG_INFO(
                "snap: %zu entities snapped, %zu mesh free entities carried by their neighbours",
                shifts.size(),
                orphans.size()
            );
        }

        // anything visible left hanging in the air, or swallowed by the ground, is a bug, name the
        // worst offenders rather than leaving them to be spotted by eye
        {
            struct offender
            {
                Entity* entity;
                float amount;
            };

            vector<offender> floating;
            vector<offender> buried;

            for (Entity* entity : entities)
            {
                vector<Entity*> parts;
                parts.push_back(entity);
                entity->GetDescendants(&parts);

                for (Entity* part : parts)
                {
                    if (!part || is_terrain_tile_or_water(part))
                    {
                        continue;
                    }

                    // a bare group node has no business on the ground, only things you can see
                    Render* render    = part->GetComponent<Render>();
                    const bool meshed = render && render->GetMesh();
                    const bool visible =
                        meshed ||
                        part->GetComponent<Light>() ||
                        part->GetComponent<Camera>();

                    if (!visible)
                    {
                        continue;
                    }

                    const Vector3 position = part->GetPosition();
                    float ground           = 0.0f;
                    if (!terrain->SampleHeight(position.x, position.z, ground))
                    {
                        continue;
                    }

                    // a spline pivot sits far from its own road mesh, so geometry is judged by its
                    // box and only a light or a camera has to fall back on the pivot
                    BoundingBox aabb;
                    const bool has_box = meshed && get_world_aabb(part, aabb);
                    const float bottom = has_box ? aabb.GetMin().y : position.y;

                    if (bottom - ground > 50.0f)
                    {
                        floating.push_back({ part, bottom - ground });
                    }

                    if (!has_box)
                    {
                        continue;
                    }

                    const Vector3 box_min            = aabb.GetMin();
                    const Vector3 box_max            = aabb.GetMax();
                    const TerrainGridMapping mapping = terrain->GetGridMapping();
                    const uint32_t samples_x         = footprint_samples(box_max.x - box_min.x, mapping.scale_x);
                    const uint32_t samples_z         = footprint_samples(box_max.z - box_min.z, mapping.scale_z);

                    float highest = ground;
                    for (uint32_t iz = 0; iz < samples_z; iz++)
                    {
                        for (uint32_t ix = 0; ix < samples_x; ix++)
                        {
                            const float u = (samples_x > 1) ? static_cast<float>(ix) / static_cast<float>(samples_x - 1) : 0.5f;
                            const float v = (samples_z > 1) ? static_cast<float>(iz) / static_cast<float>(samples_z - 1) : 0.5f;

                            float sampled = 0.0f;
                            if (terrain->SampleHeight(
                                box_min.x + (box_max.x - box_min.x) * u,
                                box_min.z + (box_max.z - box_min.z) * v,
                                sampled
                            ))
                            {
                                highest = max(highest, sampled);
                            }
                        }
                    }

                    if (box_max.y < highest - 0.5f)
                    {
                        buried.push_back({ part, highest - box_max.y });
                    }
                }
            }

            auto worst_first = [](vector<offender>& list)
            {
                sort(list.begin(), list.end(), [](const offender& a, const offender& b)
                {
                    return a.amount > b.amount;
                });

                return min<size_t>(list.size(), 12);
            };

            if (!floating.empty())
            {
                const size_t reported = worst_first(floating);
                SP_LOG_WARNING("snap: %zu visible entities are still over 50 m above the ground", floating.size());

                for (size_t i = 0; i < reported; i++)
                {
                    Entity* parent = floating[i].entity->GetParent();
                    SP_LOG_WARNING(
                        "snap: '%s' floats %.0f m, parent '%s'",
                        floating[i].entity->GetObjectName().c_str(),
                        floating[i].amount,
                        parent ? parent->GetObjectName().c_str() : "none"
                    );
                }
            }

            if (!buried.empty())
            {
                const size_t reported = worst_first(buried);
                SP_LOG_WARNING("snap: %zu meshes are completely under the terrain", buried.size());

                for (size_t i = 0; i < reported; i++)
                {
                    Entity* parent = buried[i].entity->GetParent();
                    SP_LOG_WARNING(
                        "snap: '%s' is buried %.1f m, parent '%s'",
                        buried[i].entity->GetObjectName().c_str(),
                        buried[i].amount,
                        parent ? parent->GetObjectName().c_str() : "none"
                    );
                }
            }
        }

        return count;
    }

    void Terrain::RememberPlatform(const TerrainPlatform& platform_in)
    {
        // a pad is stamped for where its owner stands right now
        TerrainPlatform platform = platform_in;
        if (!platform.anchored)
        {
            if (Entity* owner = World::GetEntityById(platform.entity_id))
            {
                platform.anchored        = true;
                platform.anchor_position = owner->GetPosition();
                platform.anchor_rotation = owner->GetRotation();
                platform.seen_position   = platform.anchor_position;
                platform.seen_rotation   = platform.anchor_rotation;
            }
        }

        auto area = [](const TerrainPlatform& pad) -> float
        {
            if (pad.half_x > 0.0f && pad.half_z > 0.0f)
            {
                return pad.half_x * pad.half_z * 4.0f;
            }

            return max(pad.max_x - pad.min_x, 0.0f) * max(pad.max_z - pad.min_z, 0.0f);
        };

        auto iou = [&](const TerrainPlatform& a, const TerrainPlatform& b) -> float
        {
            const float inter_x = max(0.0f, min(a.max_x, b.max_x) - max(a.min_x, b.min_x));
            const float inter_z = max(0.0f, min(a.max_z, b.max_z) - max(a.min_z, b.min_z));
            const float inter   = inter_x * inter_z;
            const float uni     = area(a) + area(b) - inter;
            return uni > 0.0f ? inter / uni : 0.0f;
        };

        const float span = max(platform.half_x, platform.half_z) * 2.0f;
        if (span > 1200.0f)
        {
            return;
        }

        for (TerrainPlatform& existing : m_platforms)
        {
            if ((platform.entity_id != 0 && existing.entity_id == platform.entity_id) ||
                iou(existing, platform) > 0.5f)
            {
                existing = platform;
                return;
            }
        }

        m_platforms.push_back(platform);
    }

    void Terrain::PruneOrphanPlatforms()
    {
        m_platforms.erase(
            remove_if(
                m_platforms.begin(),
                m_platforms.end(),
                [](TerrainPlatform& pad)
                {
                    // an anchored pad whose owner is alive is carried along by FollowPlatformOwners
                    if (pad.anchored && pad.entity_id != 0 && World::GetEntityById(pad.entity_id))
                    {
                        return false;
                    }

                    // geometry only, a pad whose building walked away is an orphan even if it is alive
                    Entity* occupant = platform_find_occupant(pad);
                    if (occupant && occupant->GetObjectId() != pad.entity_id)
                    {
                        pad.entity_id = occupant->GetObjectId();
                        pad.anchored  = false;
                    }

                    return occupant == nullptr;
                }
            ),
            m_platforms.end()
        );
    }

    void Terrain::ApplyPlatformsToProps()
    {
        if (m_platforms.empty())
        {
            return;
        }

        for (const TerrainPlatform& platform : m_platforms)
        {
            RestampPropsForPad(platform, false);
        }

        if (UploadPropMask())
        {
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }
    }

    void Terrain::OnBiomePropsPopulated()
    {
        if (!ProgressTracker::IsLoading(ProgressType::World))
        {
            PruneOrphanPlatforms();
        }

        SnapshotPropInstances();
        ApplyPlatformsToProps();
        MarkSplinePropCarvesDirty();
    }

    void Terrain::OnBiomePropsRepopulated(const vector<uint32_t>& tile_indices)
    {
        if (!m_entity_ptr || tile_indices.empty())
        {
            return;
        }

        // the seeds of the removed props are gone already, the fresh ones come from the tile subtrees
        unordered_set<uint32_t> touched(tile_indices.begin(), tile_indices.end());
        for (Entity* child : m_entity_ptr->GetChildren())
        {
            const int index = ParseTileIndex(child);
            if (index < 0 || touched.find(static_cast<uint32_t>(index)) == touched.end())
            {
                continue;
            }

            SnapshotPropSeedsUnder(child, false);
        }

        // the pads standing on these tiles have to hide the new props the same way they hid the old
        bool mask_changed = false;
        for (const TerrainPlatform& platform : m_platforms)
        {
            unordered_set<uint32_t> pad_tiles;
            CollectTilesInWorldRect(platform.min_x, platform.min_z, platform.max_x, platform.max_z, pad_tiles);

            bool overlaps = false;
            for (uint32_t tile_index : pad_tiles)
            {
                if (touched.find(tile_index) != touched.end())
                {
                    overlaps = true;
                    break;
                }
            }

            if (overlaps)
            {
                RestampPropsForPad(platform, false);
                mask_changed = true;
            }
        }

        if (mask_changed && UploadPropMask())
        {
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }

        MarkSplinePropCarvesDirty();
    }

    void Terrain::ForgetPlatform(uint64_t entity_id)
    {
        if (entity_id == 0)
        {
            return;
        }

        m_platforms.erase(
            remove_if(
                m_platforms.begin(),
                m_platforms.end(),
                [entity_id](const TerrainPlatform& pad)
                {
                    return pad.entity_id == entity_id;
                }
            ),
            m_platforms.end()
        );
    }

    void Terrain::SnapshotSeed()
    {
        m_positions_seed = m_positions;
    }

    void Terrain::PaintPadFromSeed(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw,
        float world_height,
        float blend_margin,
        bool restore_only
    )
    {
        if (!HasHeightfield() || m_positions_seed.size() != m_positions.size())
        {
            return;
        }

        float local_cx     = center_x;
        float local_cz     = center_z;
        float local_hx     = half_x;
        float local_hz     = half_z;
        float local_yaw    = yaw;
        float local_height = world_height;

        if (Entity* entity = GetEntity())
        {
            const Matrix inverse = entity->GetMatrix().Inverted();
            const Vector3 local_center = inverse * Vector3(center_x, world_height, center_z);
            local_cx     = local_center.x;
            local_cz     = local_center.z;
            local_height = local_center.y;

            const Vector3 world_axis(center_x + cosf(yaw), world_height, center_z + sinf(yaw));
            const Vector3 local_axis = inverse * world_axis - local_center;
            local_yaw = atan2f(local_axis.z, local_axis.x);

            const Vector3 world_x(center_x + cosf(yaw) * half_x, world_height, center_z + sinf(yaw) * half_x);
            const Vector3 world_z(center_x - sinf(yaw) * half_z, world_height, center_z + cosf(yaw) * half_z);
            local_hx = (inverse * world_x - local_center).Length();
            local_hz = (inverse * world_z - local_center).Length();
        }

        const TerrainGridMapping mapping = GetGridMapping();
        const float step_x = max(mapping.scale_x, 0.001f);
        const float step_z = max(mapping.scale_z, 0.001f);
        const float cell   = max(step_x, step_z);
        const float margin = max(max(blend_margin, 0.0f), cell);
        const float inner  = min(cell * 0.51f, margin);
        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(local_cx, local_cz, local_hx, local_hz, local_yaw, min_x, min_z, max_x, max_z);
        min_x -= margin;
        max_x += margin;
        min_z -= margin;
        max_z += margin;
        const int x0 = max(static_cast<int>(floorf((min_x + mapping.offset_x) / step_x)), 0);
        const int z0 = max(static_cast<int>(floorf((min_z + mapping.offset_z) / step_z)), 0);
        const int x1 = min(static_cast<int>(ceilf((max_x + mapping.offset_x) / step_x)), static_cast<int>(m_dense_width) - 1);
        const int z1 = min(static_cast<int>(ceilf((max_z + mapping.offset_z) / step_z)), static_cast<int>(m_dense_height) - 1);

        // Remove only this region's old road contribution before repainting. Reapply
        // cached road envelopes below, without invalidating or rebuilding whole roads.
        const bool has_road_delta = m_road_carve_delta.size() == m_positions.size();
        if (has_road_delta)
        {
            for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x)
            {
                const size_t index = static_cast<size_t>(z) * m_dense_width + x;
                m_positions[index].y -= m_road_carve_delta[index];
                m_road_carve_delta[index] = 0.0f;
            }
        }

        for (int z = z0; z <= z1; z++)
        {
            for (int x = x0; x <= x1; x++)
            {
                const uint32_t index = static_cast<uint32_t>(z) * m_dense_width + static_cast<uint32_t>(x);
                Vector3& position    = m_positions[index];
                const float distance = obb_outside_distance(
                    position.x,
                    position.z,
                    local_cx,
                    local_cz,
                    local_hx,
                    local_hz,
                    local_yaw
                );

                if (distance > margin)
                {
                    continue;
                }

                const float seed_y = m_positions_seed[index].y;
                if (restore_only)
                {
                    position.y = seed_y;
                    continue;
                }

                float weight = 1.0f;
                if (distance > inner && margin > inner)
                {
                    const float t = (distance - inner) / (margin - inner);
                    weight        = 1.0f - (t * t * (3.0f - 2.0f * t));
                }

                position.y = seed_y + (local_height - seed_y) * weight;
            }
        }

        ApplyRoadHeightConstraints(m_positions, m_dense_width, mapping,
            x0, z0, x1, z1, 0.75f * cell, has_road_delta ? &m_road_carve_delta : nullptr);

        // the flush repairs mesh, height texture and the flat height mirror for this rect only
        MarkHeightsDirty(x0, z0, x1, z1);
    }

    void Terrain::MarkRoadCarvesDirtyInGridRect(int32_t x0, int32_t z0, int32_t x1, int32_t z1)
    {
        // only the roads whose footprint overlaps the rect re-carve, the rest keep their delta
        for (const auto& [id, bounds] : m_road_carve_bounds)
        {
            if (bounds[1] < x0 || bounds[0] > x1 || bounds[3] < z0 || bounds[2] > z1)
            {
                continue;
            }

            MarkSplineHeightCarvesDirty(id);
            // Terrain painting changes the base even if the deck samples are identical.
            m_road_carve_jobs.erase(id);
        }
    }

    bool Terrain::SampleSeedMax(
        float center_x,
        float center_z,
        float half_x,
        float half_z,
        float yaw,
        float& out_max
    )
    {
        out_max = 0.0f;
        if (m_positions_seed.size() != m_positions.size() || !HasHeightfield())
        {
            return false;
        }

        float local_cx  = center_x;
        float local_cz  = center_z;
        float local_hx  = half_x;
        float local_hz  = half_z;
        float local_yaw = yaw;
        if (Entity* entity = GetEntity())
        {
            const Matrix inverse = entity->GetMatrix().Inverted();
            const Vector3 local_center = inverse * Vector3(center_x, 0.0f, center_z);
            local_cx = local_center.x;
            local_cz = local_center.z;
            const Vector3 local_axis = inverse * Vector3(center_x + cosf(yaw), 0.0f, center_z + sinf(yaw)) - local_center;
            local_yaw = atan2f(local_axis.z, local_axis.x);
            local_hx = (inverse * Vector3(center_x + cosf(yaw) * half_x, 0.0f, center_z + sinf(yaw) * half_x) - local_center).Length();
            local_hz = (inverse * Vector3(center_x - sinf(yaw) * half_z, 0.0f, center_z + cosf(yaw) * half_z) - local_center).Length();
        }

        float highest = -numeric_limits<float>::max();
        uint32_t count = 0;
        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(local_cx, local_cz, local_hx, local_hz, local_yaw, min_x, min_z, max_x, max_z);

        const TerrainGridMapping mapping = GetGridMapping();
        const float step_x = max(mapping.scale_x, 0.001f);
        const float step_z = max(mapping.scale_z, 0.001f);
        const int x0 = max(static_cast<int>(floorf((min_x + mapping.offset_x) / step_x)), 0);
        const int z0 = max(static_cast<int>(floorf((min_z + mapping.offset_z) / step_z)), 0);
        const int x1 = min(static_cast<int>(ceilf((max_x + mapping.offset_x) / step_x)), static_cast<int>(m_dense_width) - 1);
        const int z1 = min(static_cast<int>(ceilf((max_z + mapping.offset_z) / step_z)), static_cast<int>(m_dense_height) - 1);

        for (int z = z0; z <= z1; z++)
        {
            for (int x = x0; x <= x1; x++)
            {
                const uint32_t index = static_cast<uint32_t>(z) * m_dense_width + static_cast<uint32_t>(x);
                const Vector3& local = m_positions_seed[index];
                if (obb_outside_distance(local.x, local.z, local_cx, local_cz, local_hx, local_hz, local_yaw) > 0.0f)
                {
                    continue;
                }

                float world_y = local.y;
                if (Entity* entity = GetEntity())
                {
                    world_y = (entity->GetMatrix() * Vector3(local.x, local.y, local.z)).y;
                }

                highest = max(highest, world_y);
                count++;
            }
        }

        if (count == 0)
        {
            return false;
        }

        out_max = highest;
        return true;
    }

    void Terrain::DestroyPadOverlays()
    {
        if (!m_entity_ptr)
        {
            return;
        }

        if (Entity* pad = m_entity_ptr->GetChildByName("live_pad"))
        {
            detach_render_mesh(pad);
            World::RemoveEntity(pad);
        }

        if (Entity* pad = m_entity_ptr->GetChildByName("pad_overlay"))
        {
            detach_render_mesh(pad);
            World::RemoveEntity(pad);
        }
    }

    void Terrain::DestroyPadRefine(uint64_t entity_id)
    {
        if (!m_entity_ptr || entity_id == 0)
        {
            m_pad_refine_meshes.erase(entity_id);
            m_pad_refine_grids.erase(entity_id);
            return;
        }

        if (Entity* child = m_entity_ptr->GetChildByName(pad_refine_name(entity_id)))
        {
            detach_render_mesh(child);
            World::RemoveEntity(child);
        }

        m_pad_refine_meshes.erase(entity_id);
        m_pad_refine_grids.erase(entity_id);
    }

    void Terrain::DestroyAllPadRefines()
    {
        if (m_entity_ptr)
        {
            vector<Entity*> children = m_entity_ptr->GetChildren();
            for (Entity* child : children)
            {
                if (child && is_pad_refine_name(child->GetObjectName()))
                {
                    detach_render_mesh(child);
                    World::RemoveEntity(child);
                }
            }
        }

        m_pad_refine_meshes.clear();
        m_pad_refine_grids.clear();
    }

    void Terrain::SyncPadRefine(const TerrainPlatform& pad, bool cook_physics)
    {
        SP_PROFILE_CPU();
        if (!m_entity_ptr || pad.entity_id == 0 || !HasHeightfield())
        {
            return;
        }

        if (m_positions_seed.size() != m_positions.size())
        {
            return;
        }

        float local_cx     = 0.0f;
        float local_cz     = 0.0f;
        float local_hx     = 0.0f;
        float local_hz     = 0.0f;
        float local_yaw    = 0.0f;
        float local_height = 0.0f;
        platform_to_local(
            GetEntity(),
            pad,
            local_cx,
            local_cz,
            local_hx,
            local_hz,
            local_yaw,
            local_height
        );

        const TerrainGridMapping mapping = GetGridMapping();
        const float cell   = max(max(mapping.scale_x, mapping.scale_z), 1.0f);
        const float margin = pad_deform_margin(pad, cell);
        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(local_cx, local_cz, local_hx, local_hz, local_yaw, min_x, min_z, max_x, max_z);
        min_x -= margin;
        max_x += margin;
        min_z -= margin;
        max_z += margin;

        const float width  = max(max_x - min_x, 0.5f);
        const float depth  = max(max_z - min_z, 0.5f);
        const float target = 0.75f;
        const uint32_t segs_x = clamp(static_cast<uint32_t>(ceilf(width / target)), 16u, 96u);
        const uint32_t segs_z = clamp(static_cast<uint32_t>(ceilf(depth / target)), 16u, 96u);
        const uint32_t verts_x = segs_x + 1;
        const uint32_t verts_z = segs_z + 1;

        vector<Vector3> positions(static_cast<size_t>(verts_x) * verts_z);
        for (uint32_t z = 0; z < verts_z; z++)
        {
            const float tz = static_cast<float>(z) / static_cast<float>(segs_z);
            const float lz = min_z + (max_z - min_z) * tz;
            for (uint32_t x = 0; x < verts_x; x++)
            {
                const float tx = static_cast<float>(x) / static_cast<float>(segs_x);
                const float lx = min_x + (max_x - min_x) * tx;
                const float distance = obb_outside_distance(
                    lx,
                    lz,
                    local_cx,
                    local_cz,
                    local_hx,
                    local_hz,
                    local_yaw
                );

                const float seed_y = TerrainSystem::SampleHeight(
                    m_positions_seed,
                    m_dense_width,
                    m_dense_height,
                    lx,
                    lz,
                    mapping
                );

                float y = seed_y;
                if (distance <= 0.0f)
                {
                    y = local_height;
                }
                else if (margin > 0.0f && distance < margin)
                {
                    const float t = distance / margin;
                    const float w = 1.0f - (t * t * (3.0f - 2.0f * t));
                    y = seed_y + (local_height - seed_y) * w;
                }

                positions[static_cast<size_t>(z) * verts_x + x] = Vector3(lx, y + 0.02f, lz);
            }
        }

        TerrainGridMapping refine_mapping;
        refine_mapping.scale_x = width / segs_x;
        refine_mapping.scale_z = depth / segs_z;
        refine_mapping.offset_x = -min_x;
        refine_mapping.offset_z = -min_z;
        // Apply the road ceiling AFTER the overlay offset too. A triangle touching
        // the deck must have its neighbouring vertices constrained beneath it.
        const float refine_plateau = sqrtf(refine_mapping.scale_x * refine_mapping.scale_x +
                                           refine_mapping.scale_z * refine_mapping.scale_z);
        ApplyRoadHeightConstraints(positions, verts_x, refine_mapping,
            0, 0, segs_x, segs_z, refine_plateau, nullptr);

        vector<RHI_Vertex_PosTexNorTan> vertices(positions.size());
        vector<uint32_t> indices((verts_x - 1) * (verts_z - 1) * 6);
        TerrainSystem::GenerateVerticesAndIndices(vertices, indices, positions, verts_x, verts_z);
        TerrainSystem::GenerateNormals(vertices, verts_x, verts_z);

        const float extent_x = max(mapping.extent_x, 0.001f);
        const float extent_z = max(mapping.extent_z, 0.001f);
        for (RHI_Vertex_PosTexNorTan& vertex : vertices)
        {
            const float u = (vertex.pos[0] + mapping.offset_x) / extent_x;
            const float v = (vertex.pos[2] + mapping.offset_z) / extent_z;
            vertex.set_uv(u, v);
        }

        const string name = pad_refine_name(pad.entity_id);
        Entity* child = m_entity_ptr->GetChildByName(name);
        if (!child)
        {
            child = World::CreateEntity();
            child->SetObjectName(name);
            child->SetTransient(true);
            child->SetParent(m_entity_ptr);
            child->SetPositionLocal(Vector3::Zero);
            World::ProcessPendingAdditions();
        }

        shared_ptr<Mesh>& mesh = m_pad_refine_meshes[pad.entity_id];
        Render* render = child->GetComponent<Render>();
        if (!render)
        {
            render = child->AddComponent<Render>();
        }

        bool updated = false;
        if (mesh && render->GetMesh() == mesh.get())
        {
            const auto grid = m_pad_refine_grids.find(pad.entity_id);
            if (grid != m_pad_refine_grids.end() && grid->second == array<uint32_t, 2>{verts_x, verts_z})
            {
                updated = mesh->UpdateVertices(vertices);
            }
            if (!updated) updated = mesh->UpdateGeometry(vertices, indices);
            if (updated) render->SetMesh(mesh.get());
        }

        if (!updated)
        {
            if (render->GetMesh() == mesh.get())
            {
                render->ClearMesh();
            }

            mesh = make_shared<Mesh>();
            mesh->SetObjectName(name);
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale), false);
            mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessGenerateLods), false);
            mesh->SetDynamic(true);
            mesh->SetPersistent(false);
            mesh->AddGeometry(vertices, indices, false);
            mesh->CreateGpuBuffers();
            render->SetMesh(mesh.get());
        }

        if (m_material && render->GetMaterial() != m_material.get())
        {
            render->SetMaterial(m_material);
        }

        m_pad_refine_grids[pad.entity_id] = {verts_x, verts_z};
        if (cook_physics)
        {
            CookPadRefine(pad.entity_id);
        }
    }

    void Terrain::CookPadRefine(uint64_t entity_id)
    {
        SP_PROFILE_CPU();
        Entity* child = m_entity_ptr ? m_entity_ptr->GetChildByName(pad_refine_name(entity_id)) : nullptr;
        if (!child) return;
        Physics* physics = child->GetComponent<Physics>();
        if (!physics)
        {
            physics = child->AddComponent<Physics>();
            physics->SetUseConvexHull(false);
            physics->SetBodyType(BodyType::Mesh);
        }
        else
        {
            physics->Rebuild();
        }
    }

    void Terrain::RebuildCommittedRefines()
    {
        for (const TerrainPlatform& pad : m_platforms)
        {
            SyncPadRefine(pad, true);
        }
    }

    void Terrain::SnapPropsToSurface(
        float center_x,
        float center_z,
        float deform_hx,
        float deform_hz,
        float object_hx,
        float object_hz,
        float yaw
    )
    {
        if (!m_entity_ptr)
        {
            return;
        }

        auto in_obb = [&](float x, float z, float half_x, float half_z) -> bool
        {
            return obb_outside_distance(x, z, center_x, center_z, half_x, half_z, yaw) <= 0.0f;
        };

        float min_x = 0.0f;
        float min_z = 0.0f;
        float max_x = 0.0f;
        float max_z = 0.0f;
        obb_write_aabb(center_x, center_z, deform_hx, deform_hz, yaw, min_x, min_z, max_x, max_z);
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
                vector<Matrix> next;
                const uint32_t count = render->GetInstanceCount();
                next.reserve(count);
                bool changed         = false;
                const Matrix world   = entity->GetMatrix();
                const Matrix inverse = world.Inverted();
                for (uint32_t i = 0; i < count; i++)
                {
                    Matrix local           = render->GetInstance(i, false);
                    const Vector3 position = (local * world).GetTranslation();
                    if (!in_obb(position.x, position.z, deform_hx, deform_hz) ||
                        in_obb(position.x, position.z, object_hx, object_hz))
                    {
                        next.push_back(local);
                        continue;
                    }

                    float height = position.y;
                    if (SampleHeight(position.x, position.z, height))
                    {
                        const Vector3 local_pos = inverse * Vector3(position.x, height, position.z);
                        local.m30 = local_pos.x;
                        local.m31 = local_pos.y;
                        local.m32 = local_pos.z;
                        changed = true;
                    }

                    next.push_back(local);
                }

                if (changed)
                {
                    render->SetInstances(next);
                }
            }
            else if (entity->GetChildrenCount() == 0)
            {
                const Vector3 world = entity->GetPosition();
                if (in_obb(world.x, world.z, deform_hx, deform_hz) &&
                    !in_obb(world.x, world.z, object_hx, object_hz))
                {
                    float height = world.y;
                    if (SampleHeight(world.x, world.z, height))
                    {
                        entity->SetPosition(Vector3(world.x, height, world.z));
                    }
                }
            }
        }
    }

    void Terrain::RestampPropsForPad(const TerrainPlatform& pad, bool restore, bool update_instances)
    {
        SP_PROFILE_CPU();
        const TerrainGridMapping mapping = GetGridMapping();
        const float cell = max(max(mapping.scale_x, mapping.scale_z), 1.0f);
        float deform_hx = 0.0f;
        float deform_hz = 0.0f;
        pad_deform_extents(pad, cell, deform_hx, deform_hz);

        RestorePropMaskFootprint(pad.center_x, pad.center_z, deform_hx, deform_hz, pad.yaw);
        if (update_instances)
            RestoreFootprintProps(pad.center_x, pad.center_z, deform_hx, deform_hz, pad.yaw);
        if (restore) return;

        PunchPropMaskFootprint(pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw);
        if (update_instances)
        {
            ClearFootprintProps(pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw);
            SnapPropsToSurface(pad.center_x, pad.center_z, deform_hx, deform_hz, pad.half_x, pad.half_z, pad.yaw);
        }
    }

    void Terrain::SyncLivePadVisuals(const TerrainPlatform* restore, const TerrainPlatform* paint)
    {
        SP_PROFILE_CPU();
        // heights were already painted by the caller and sit in the dirty rect, everything here is
        // region work, collision waits for the debounce so a drag never cooks a heightfield per frame
        // Keep the last committed prop footprint; intermediate drag positions never
        // changed CPU instances and therefore do not need restoring at commit.
        if (!m_live_pad_props_dirty && (restore || paint))
        {
            m_live_pad_props_previous = restore ? *restore : *paint;
            m_live_pad_props_dirty = true;
        }
        if (restore)
        {
            if (!paint || paint->entity_id != restore->entity_id)
            {
                DestroyPadRefine(restore->entity_id);
            }
            RestampPropsForPad(*restore, true, false);
        }

        if (paint)
        {
            RestampPropsForPad(*paint, false, false);
        }
        else
        {
            if (m_live_pad_props_dirty) RestampPropsForPad(m_live_pad_props_previous, true);
            m_live_pad_props_dirty = false;
            DestroyPadOverlays();
        }

        FlushHeightEdits(false);
        const bool mask_recreated = UploadPropMask();

        // the refine reads the patched grid, so it comes after the flush
        if (paint)
        {
            SyncPadRefine(*paint, false);
        }

        if (mask_recreated)
        {
            PushToRenderer();
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }
    }

    void Terrain::RestorePlatform(const TerrainPlatform& pad)
    {
        PaintPadFromSeed(
            pad.center_x,
            pad.center_z,
            pad.half_x,
            pad.half_z,
            pad.yaw,
            pad.height,
            pad.margin,
            true
        );
        DestroyPadRefine(pad.entity_id);
        ForgetPlatform(pad.entity_id);
        RestampPropsForPad(pad, true);

        FlushHeightEdits(true);
        if (UploadPropMask())
        {
            PushToRenderer();
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }
    }

    void Terrain::RestoreLivePad()
    {
        if (!m_live_pad_active)
        {
            return;
        }

        if (m_live_pad_props_dirty) RestampPropsForPad(m_live_pad_props_previous, true);
        m_live_pad_props_dirty = false;
        RestorePlatform(m_live_pad);
        m_live_pad_active   = false;
        m_live_pad_dirty    = false;
        m_live_track_entity = 0;
        m_live_pad          = {};
    }

    void Terrain::PruneVanishedPlatforms()
    {
        if (ProgressTracker::IsLoading(ProgressType::World))
        {
            return;
        }

        if (m_platforms.empty()) return;

        // World resolves IDs through its index; there is no need to scan every entity each frame.
        vector<TerrainPlatform> gone;
        gone.reserve(m_platforms.size());
        for (TerrainPlatform& pad : m_platforms)
        {
            if (!platform_occupant_alive(pad))
            {
                gone.push_back(pad);
            }
        }

        for (const TerrainPlatform& pad : gone)
        {
            RestorePlatform(pad);
        }
    }

    void Terrain::FollowPlatformOwners()
    {
        if (ProgressTracker::IsLoading(ProgressType::World) || m_platforms.empty())
        {
            return;
        }

        // any move counts, the editor drag, a parent, undo, scripts, mcp or a rewritten world file
        const double now = Timer::GetTimeMs();
        vector<pair<TerrainPlatform, TerrainPlatform>> moves;
        for (TerrainPlatform& pad : m_platforms)
        {
            if (pad.entity_id == 0 || (m_live_pad_active && pad.entity_id == m_live_pad.entity_id))
            {
                continue;
            }

            Entity* owner = World::GetEntityById(pad.entity_id);
            if (!owner || !owner->GetActive())
            {
                continue;
            }

            const Vector3 position    = owner->GetPosition();
            const Quaternion rotation = owner->GetRotation();
            if (!pad.anchored)
            {
                pad.anchored        = true;
                pad.anchor_position = position;
                pad.anchor_rotation = rotation;
                pad.seen_position   = position;
                pad.seen_rotation   = rotation;
                continue;
            }

            auto same_transform = [&](const Vector3& p, const Quaternion& q)
            {
                return (position - p).LengthSquared() < 0.0001f && fabsf(Quaternion::Dot(rotation, q)) > 0.99999f;
            };

            if (same_transform(pad.anchor_position, pad.anchor_rotation))
            {
                pad.seen_position = position;
                pad.seen_rotation = rotation;
                continue;
            }

            // wait for the owner to settle so an animated or scripted move does not recut the ground every frame
            if (!same_transform(pad.seen_position, pad.seen_rotation))
            {
                pad.seen_position   = position;
                pad.seen_rotation   = rotation;
                pad.seen_changed_ms = now;
                continue;
            }

            if (now - pad.seen_changed_ms < 250.0)
            {
                continue;
            }

            const Quaternion delta = rotation * pad.anchor_rotation.Inverse();
            const Vector3 offset   = delta * Vector3(pad.center_x - pad.anchor_position.x, 0.0f, pad.center_z - pad.anchor_position.z);
            const Vector3 axis     = delta * Vector3(cosf(pad.yaw), 0.0f, sinf(pad.yaw));

            TerrainPlatform moved = pad;
            moved.center_x        = position.x + offset.x;
            moved.center_z        = position.z + offset.z;
            moved.yaw             = atan2f(axis.z, axis.x);
            moved.height          = pad.height + (position.y - pad.anchor_position.y);
            moved.anchor_position = position;
            moved.anchor_rotation = rotation;

            // keep whatever slack the stored bounds had around the oriented box
            float old_min_x, old_min_z, old_max_x, old_max_z;
            obb_write_aabb(pad.center_x, pad.center_z, pad.half_x, pad.half_z, pad.yaw, old_min_x, old_min_z, old_max_x, old_max_z);
            const float slack_x = max(((pad.max_x - pad.min_x) - (old_max_x - old_min_x)) * 0.5f, 0.0f);
            const float slack_z = max(((pad.max_z - pad.min_z) - (old_max_z - old_min_z)) * 0.5f, 0.0f);
            obb_write_aabb(moved.center_x, moved.center_z, moved.half_x, moved.half_z, moved.yaw, moved.min_x, moved.min_z, moved.max_x, moved.max_z);
            moved.min_x -= slack_x;
            moved.max_x += slack_x;
            moved.min_z -= slack_z;
            moved.max_z += slack_z;

            moves.emplace_back(pad, moved);
        }

        if (moves.empty())
        {
            return;
        }

        for (const auto& [previous, moved] : moves)
        {
            PaintPadFromSeed(previous.center_x, previous.center_z, previous.half_x, previous.half_z, previous.yaw, previous.height, previous.margin, true);
            DestroyPadRefine(previous.entity_id);
            ForgetPlatform(previous.entity_id);
            RestampPropsForPad(previous, true);
        }

        // restoring goes back to the seed, so neighbours overlapping a vacated footprint are stamped again
        auto overlaps = [](const TerrainPlatform& a, const TerrainPlatform& b)
        {
            const float reach = max(a.margin, 0.0f) + max(b.margin, 0.0f);
            return a.min_x - reach <= b.max_x && b.min_x - reach <= a.max_x && a.min_z - reach <= b.max_z && b.min_z - reach <= a.max_z;
        };
        for (const TerrainPlatform& other : m_platforms)
        {
            for (const auto& [previous, moved] : moves)
            {
                if (overlaps(other, previous))
                {
                    PaintPadFromSeed(other.center_x, other.center_z, other.half_x, other.half_z, other.yaw, other.height, other.margin, false);
                    break;
                }
            }
        }

        for (const auto& [previous, moved] : moves)
        {
            PaintPadFromSeed(moved.center_x, moved.center_z, moved.half_x, moved.half_z, moved.yaw, moved.height, moved.margin, false);
            RememberPlatform(moved);
            RestampPropsForPad(moved, false);
            SP_LOG_INFO("terrain pad of entity %llu followed its owner to %.1f, %.1f", static_cast<unsigned long long>(moved.entity_id), moved.center_x, moved.center_z);
        }

        FlushHeightEdits(true);
        const bool mask_recreated = UploadPropMask();
        for (const auto& [previous, moved] : moves)
        {
            CookPadRefine(moved.entity_id);
        }

        if (mask_recreated)
        {
            PushToRenderer();
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }
    }

    void Terrain::CommitLivePad(bool punch)
    {
        SP_PROFILE_CPU();
        if (!m_live_pad_dirty && !m_live_pad_props_dirty && m_height_dirty.IsEmpty() &&
            m_physics_dirty.IsEmpty() && m_prop_mask_bake_dirty.IsEmpty()) return;
        if (m_live_pad_props_dirty) RestampPropsForPad(m_live_pad_props_previous, true);
        m_live_pad_props_dirty = false;
        if (punch)
        {
            RestampPropsForPad(m_live_pad, false);
        }

        // whatever the drag left unflushed, plus the deferred collision and mask for everything it touched
        FlushHeightEdits(true);
        const bool mask_recreated = UploadPropMask();
        CookPadRefine(m_live_pad.entity_id);
        DestroyPadOverlays();

        if (mask_recreated)
        {
            PushToRenderer();
            WorldHelpers::RefreshTerrainGpuScatter(this);
        }
        m_live_pad_dirty = false;
    }

    void Terrain::UpdateLivePads()
    {
        if (!HasHeightfield())
        {
            return;
        }

        DestroyPadOverlays();
        PruneVanishedPlatforms();

        if (m_positions_seed.size() != m_positions.size())
        {
            if (!m_platforms.empty())
            {
                return;
            }

            SnapshotSeed();
        }

        if (m_positions_seed.size() != m_positions.size())
        {
            return;
        }

        FollowPlatformOwners();

        vector<Entity*> selected;
        for (uint64_t id : edit_targets)
            if (Entity* entity = World::GetEntityById(id)) selected.push_back(entity);

        vector<Entity*> candidates;
        candidates.reserve(selected.size());
        for (Entity* entity : selected)
        {
            if (!entity || is_terrain_tile_or_water(entity) || entity->GetComponent<Terrain>())
            {
                continue;
            }

            candidates.push_back(entity);
        }

        if (candidates.empty())
        {
            if (m_live_pad_active)
            {
                if (platform_occupant_alive(m_live_pad))
                {
                    CommitLivePad(true);
                }
                else
                {
                    RestoreLivePad();
                }

                m_live_pad_active   = false;
                m_live_track_entity = 0;
            }

            PruneVanishedPlatforms();
            return;
        }

        Entity* track = candidates[0];
        if (m_live_pad_active &&
            m_live_track_entity == track->GetObjectId() &&
            (track->GetPosition() - m_live_track_position).LengthSquared() < 0.0001f &&
            fabsf(Quaternion::Dot(track->GetRotation(), m_live_track_rotation)) > 0.99999f &&
            (track->GetScale() - m_live_track_scale).LengthSquared() < 0.0001f)
        {
            if (m_live_pad_dirty && (Timer::GetTimeMs() - m_live_pad_changed_ms) > 250.0)
            {
                CommitLivePad(true);
            }
            return;
        }

        SnapPlatformDesc desc;
        const bool has_floor  = find_snap_platform(candidates, true, desc);
        const float clearance = 0.08f;

        TerrainPlatform wanted;
        bool want_pad = false;
        if (has_floor && desc.half_x > 0.05f && desc.half_z > 0.05f)
        {
            const float span = max(desc.half_x, desc.half_z) * 2.0f;
            if (span < live_pad_span_cap(desc.half_x, desc.half_z))
            {
                float seed_max = 0.0f;
                if (SampleSeedMax(
                    desc.center_x,
                    desc.center_z,
                    desc.half_x,
                    desc.half_z,
                    desc.yaw,
                    seed_max))
                {
                    const float engage = max(6.0f, min(span * 0.04f, 48.0f));
                    if ((desc.height - seed_max) < engage)
                    {
                        want_pad           = true;
                        wanted.entity_id   = desc.entity_id;
                        wanted.center_x    = desc.center_x;
                        wanted.center_z    = desc.center_z;
                        wanted.half_x      = desc.half_x;
                        wanted.half_z      = desc.half_z;
                        wanted.yaw         = desc.yaw;
                        wanted.height      = desc.height - clearance;
                        wanted.margin      = desc.margin;
                        wanted.min_x       = desc.min_x;
                        wanted.min_z       = desc.min_z;
                        wanted.max_x       = desc.max_x;
                        wanted.max_z       = desc.max_z;
                    }
                }
            }
        }

        auto remember_track = [this, track]()
        {
            m_live_track_entity   = track->GetObjectId();
            m_live_track_position = track->GetPosition();
            m_live_track_rotation = track->GetRotation();
            m_live_track_scale    = track->GetScale();
        };

        if (m_live_pad_active && want_pad && wanted.entity_id == m_live_pad.entity_id)
        {
            if (pads_match(m_live_pad, wanted))
            {
                if (m_live_pad_dirty && (Timer::GetTimeMs() - m_live_pad_changed_ms) > 250.0)
                {
                    CommitLivePad(true);
                }

                remember_track();
                return;
            }

            const TerrainPlatform previous = m_live_pad;
            PaintPadFromSeed(
                previous.center_x,
                previous.center_z,
                previous.half_x,
                previous.half_z,
                previous.yaw,
                previous.height,
                previous.margin,
                true
            );
            ForgetPlatform(previous.entity_id);
            PaintPadFromSeed(
                wanted.center_x,
                wanted.center_z,
                wanted.half_x,
                wanted.half_z,
                wanted.yaw,
                wanted.height,
                wanted.margin,
                false
            );
            RememberPlatform(wanted);
            m_live_pad            = wanted;
            m_live_pad_active     = true;
            m_live_pad_dirty      = true;
            m_live_pad_changed_ms = Timer::GetTimeMs();
            SyncLivePadVisuals(&previous, &wanted);
            remember_track();
            return;
        }

        if (m_live_pad_active && want_pad)
        {
            CommitLivePad(true);
            DestroyPadOverlays();
            m_live_pad_active = false;
        }

        if (m_live_pad_active && !want_pad)
        {
            const bool still_holding = m_live_track_entity == track->GetObjectId();
            if (!still_holding)
            {
                CommitLivePad(true);
                DestroyPadOverlays();
                m_live_pad_active   = false;
                m_live_track_entity = 0;
                remember_track();
                return;
            }

            const TerrainPlatform previous = m_live_pad;
            PaintPadFromSeed(
                previous.center_x,
                previous.center_z,
                previous.half_x,
                previous.half_z,
                previous.yaw,
                previous.height,
                previous.margin,
                true
            );
            ForgetPlatform(previous.entity_id);
            m_live_pad_active = false;
            SyncLivePadVisuals(&previous, nullptr);
            FlushHeightEdits(true);
            DestroyPadOverlays();
            m_live_pad_dirty    = false;
            m_live_track_entity = 0;
            return;
        }

        if (want_pad)
        {
            bool already_there = false;
            for (const TerrainPlatform& existing : m_platforms)
            {
                if (pads_same_place(existing, wanted) && fabsf(existing.height - wanted.height) < 0.2f)
                {
                    already_there = true;
                    break;
                }
            }

            m_live_pad            = wanted;
            m_live_pad_active     = true;
            m_live_pad_changed_ms = Timer::GetTimeMs();
            m_live_pad_dirty      = !already_there;
            if (already_there && m_pad_refine_meshes.find(wanted.entity_id) != m_pad_refine_meshes.end())
            {
                remember_track();
                return;
            }

            PaintPadFromSeed(
                wanted.center_x,
                wanted.center_z,
                wanted.half_x,
                wanted.half_z,
                wanted.yaw,
                wanted.height,
                wanted.margin,
                false
            );
            RememberPlatform(wanted);
            SyncLivePadVisuals(nullptr, &wanted);
            // first contact cooks collision once so the object can rest on the pad, later moves debounce it
            CommitLivePad(true);
            remember_track();
        }
    }

    bool Terrain::ApplyPlatformsToHeightfield()
    {
        if (m_platforms.empty() || !HasHeightfield())
        {
            return false;
        }

        for (const TerrainPlatform& platform : m_platforms)
        {
            float center_x = platform.center_x;
            float center_z = platform.center_z;
            float half_x   = platform.half_x;
            float half_z   = platform.half_z;
            float yaw      = platform.yaw;
            const float span = max(
                half_x > 0.05f ? half_x * 2.0f : (platform.max_x - platform.min_x),
                half_z > 0.05f ? half_z * 2.0f : (platform.max_z - platform.min_z)
            );
            if (span > 1200.0f)
            {
                continue;
            }
            if (half_x <= 0.05f || half_z <= 0.05f)
            {
                center_x = (platform.min_x + platform.max_x) * 0.5f;
                center_z = (platform.min_z + platform.max_z) * 0.5f;
                half_x   = (platform.max_x - platform.min_x) * 0.5f;
                half_z   = (platform.max_z - platform.min_z) * 0.5f;
                yaw      = 0.0f;
            }

            ApplyFlattenToPositions(
                center_x,
                center_z,
                half_x,
                half_z,
                yaw,
                platform.height,
                max(platform.margin, 0.0f)
            );
        }

        SP_LOG_INFO("applied %zu terrain platforms", m_platforms.size());
        return true;
    }

    bool Terrain::BuildSnapPlatform(const vector<Entity*>& entities, bool require_floor)
    {
        if (!HasHeightfield())
        {
            return false;
        }

        // stand the group up first, a tilted floor makes a lying aabb and the pad follows that lie
        level_selection(entities);

        SnapPlatformDesc desc;
        if (!find_snap_platform(entities, require_floor, desc))
        {
            return false;
        }

        const float floor_bottom = desc.height;

        float terrain_max = 0.0f;
        if (!sample_terrain_max(
                this,
                desc.center_x,
                desc.center_z,
                desc.half_x,
                desc.half_z,
                desc.yaw,
                terrain_max))
        {
            SP_LOG_WARNING("the selection footprint does not overlap the terrain");
            return false;
        }

        // sit on the highest ground under the object so nothing is buried, then fill the rest of
        // the pad up to that plane
        desc.height = terrain_max;

        TerrainPlatform platform;
        platform.entity_id = desc.entity_id;
        platform.min_x     = desc.min_x;
        platform.min_z     = desc.min_z;
        platform.max_x     = desc.max_x;
        platform.max_z     = desc.max_z;
        platform.center_x  = desc.center_x;
        platform.center_z  = desc.center_z;
        platform.half_x    = desc.half_x;
        platform.half_z    = desc.half_z;
        platform.yaw       = desc.yaw;
        platform.height    = desc.height;
        platform.margin    = desc.margin;
        RememberPlatform(platform);

        const float span      = max(desc.half_x, desc.half_z) * 2.0f;
        const float clearance = min(max(span * 0.001f, 0.05f), 0.5f);

        SP_LOG_INFO(
            "snap platform: %u floors, obb %.0f x %.0f yaw %.0f, pad at %.1f, drop %.1f, ramp %.0f",
            desc.mesh_count,
            desc.half_x * 2.0f,
            desc.half_z * 2.0f,
            desc.yaw * (180.0f / 3.14159265f),
            desc.height,
            floor_bottom - (desc.height + clearance),
            desc.margin
        );

        if (!FlattenRegion(
                desc.center_x,
                desc.center_z,
                desc.half_x,
                desc.half_z,
                desc.yaw,
                desc.height,
                desc.margin))
        {
            return false;
        }

        // RebuildSurface used to rebuild every committed refine, now only this pad gets one
        SyncPadRefine(platform, true);
        translate_floor_cluster(desc.floors, entities, (desc.height + clearance) - floor_bottom);
        return true;
    }
}
