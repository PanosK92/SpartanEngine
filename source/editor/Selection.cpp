/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#include "pch.h"
#include "Selection.h"
#include "../world/World.h"
#include "../world/Entity.h"
#include "../world/components/Spline.h"
#include "../world/components/Terrain.h"
#include "../world/components/Light.h"
#include "../world/components/AudioSource.h"
#include "../world/components/ParticleSystem.h"
#include "../input/Input.h"
#include "../rendering/Renderer.h"
using namespace std;
using namespace spartan::math;
namespace spartan
{
    namespace
    {
        vector<RayHitResult> m_pick_hits;
        vector<uint32_t> m_pick_indices;
        vector<RHI_Vertex_PosTexNorTan> m_pick_vertices;
        int m_pick_instance = -1;
        uint64_t m_pick_instance_owner_id = 0;
        // tiles carry the mesh, the terrain component lives on the parent
        Entity* resolve_picked_entity(Entity* entity)
        {
            if (!entity)
            {
                return nullptr;
            }

            if (Terrain::ParseTileIndex(entity) >= 0)
            {
                Entity* parent = entity->GetParent();
                if (parent && parent->GetComponent<Terrain>())
                {
                    return parent;
                }
            }

            return entity;
        }

        float ray_hit_mesh(
            const Ray& ray,
            const vector<uint32_t>& indices,
            const vector<RHI_Vertex_PosTexNorTan>& vertices,
            const Matrix& transform,
            float best_depth
        )
        {
            float closest = best_depth;
            for (uint32_t i = 0; i < indices.size(); i += 3)
            {
                const RHI_Vertex_PosTexNorTan& v1 = vertices[indices[i]];
                const RHI_Vertex_PosTexNorTan& v2 = vertices[indices[i + 1]];
                const RHI_Vertex_PosTexNorTan& v3 = vertices[indices[i + 2]];
                Vector3 p1(v1.pos[0], v1.pos[1], v1.pos[2]);
                Vector3 p2(v2.pos[0], v2.pos[1], v2.pos[2]);
                Vector3 p3(v3.pos[0], v3.pos[1], v3.pos[2]);

                p1 = p1 * transform;
                p2 = p2 * transform;
                p3 = p3 * transform;

                const float distance = ray.HitDistance(p1, p2, p3);
                if (distance < closest)
                {
                    closest = distance;
                }
            }

            return closest;
        }

    }

    void Selection::Initialize()
    {
        SP_SUBSCRIBE_TO_EVENT(EventType::EntityRemoving, [](const sp_variant& data) {
            RemoveFromSelection(static_cast<Entity*>(get<void*>(data)));
        });
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldUnloading, [](const sp_variant&) { ClearSelection(); });
    }

    void Selection::Publish()
    {
        vector<uint64_t> ids;
        for (Entity* entity : m_selected_entities) ids.push_back(entity->GetObjectId());
        Renderer::SetEditorSelection(ids, m_selected_instance);
        Terrain::SetEditTargets(ids);
    }

    const Ray& Selection::ComputePickingRay(Camera& camera)
    {
        static Ray ray;

        ray.m_origin    = camera.GetEntity()->GetPosition();
        ray.m_direction = camera.ScreenToWorldCoordinates(Input::GetMousePositionRelativeToEditorViewport(), 1.0f);

        return ray;
    }
    
    Entity* Selection::FindEntityUnderCursor(Camera& camera)
    {
        // the picking ray is expensive, so the last result is reused while the cursor is steady, with a staleness budget
        static Vector2  s_cached_cursor    = Vector2(numeric_limits<float>::infinity(), numeric_limits<float>::infinity());
        static uint64_t s_cached_entity_id = 0;
        static uint64_t s_cached_frame     = 0;
        static int      s_cached_instance  = -1;
        const  float    cursor_epsilon_px  = 0.5f;
        const  uint64_t max_cache_age      = 6;

        Vector2  cursor = Input::GetMousePosition();
        uint64_t frame  = Renderer::GetFrameNumber();
        if ((cursor - s_cached_cursor).LengthSquared() < cursor_epsilon_px * cursor_epsilon_px &&
            (frame - s_cached_frame) < max_cache_age)
        {
            m_pick_instance           = s_cached_instance;
            m_pick_instance_owner_id  = s_cached_entity_id;
            return World::GetEntityById(s_cached_entity_id);
        }

        // ComputePickingRay puts a far plane position in the direction field, every bounding box
        // test needs a real direction, with a position in there the ray tilts by the camera offset
        // from the world origin and nothing small enough to matter is ever hit
        const Ray& pick_ray = ComputePickingRay(camera);
        const Ray ray(pick_ray.GetStart(), pick_ray.GetDirection() - pick_ray.GetStart());

        m_pick_hits.clear();

        const vector<Entity*>& entities = World::GetEntities();
        for (Entity* entity : entities)
        {
            if (!entity || !entity->GetActive())
            {
                continue;
            }

            Render* render = entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            const BoundingBox& aabb = render->GetBoundingBox();
            float distance          = ray.HitDistance(aabb);
            if (distance == numeric_limits<float>::infinity())
            {
                continue;
            }

            m_pick_hits.emplace_back(entity, Vector3::Zero, distance, distance == 0.0f);
        }

        Entity* best_entity   = nullptr;
        float   best_depth    = numeric_limits<float>::max();
        int     best_instance = -1;

        // sort broadphase hits by aabb distance so we can early-out once the front-most triangle is closer
        // than any remaining candidate's bounding box (the camera is inside an aabb -> distance == 0, those go first)
        std::sort(m_pick_hits.begin(), m_pick_hits.end(),
            [](const RayHitResult& a, const RayHitResult& b) { return a.m_distance < b.m_distance; });

        // rank by ray distance, every candidate lands on the cursor so screen distance ranking is float noise
        for (RayHitResult& broad_hit : m_pick_hits)
        {
            if (broad_hit.m_distance >= best_depth)
            {
                break;
            }

            Render* render = broad_hit.m_entity->GetComponent<Render>();
            if (!render)
            {
                continue;
            }

            // reserve exact capacity needed to avoid heap allocations in GetGeometry::resize()
            // only reserve if current capacity is insufficient
            if (m_pick_indices.capacity() < render->GetIndexCount())
            {
                m_pick_indices.reserve(render->GetIndexCount());
            }
            if (m_pick_vertices.capacity() < render->GetVertexCount())
            {
                m_pick_vertices.reserve(render->GetVertexCount());
            }

            // instanced foliage is thousands of copies of one mesh, a triangle test per instance
            // freezes the editor, so the boxes rank the instances and only the nearest handful get
            // the exact test, that is what makes clicking one tree out of a forest land on that tree
            if (render->HasInstancing())
            {
                const BoundingBox& mesh_aabb  = render->GetBoundingBoxMesh();
                const uint32_t instance_count = render->GetInstanceCount();

                constexpr uint32_t candidate_max = 8;
                float candidate_depth[candidate_max];
                uint32_t candidate_index[candidate_max];
                uint32_t candidate_count = 0;

                for (uint32_t i = 0; i < instance_count; i++)
                {
                    const float box_dist = ray.HitDistance(mesh_aabb * render->GetInstance(i, true));
                    if (box_dist >= best_depth)
                    {
                        continue;
                    }

                    // keep the list sorted by depth, an insertion sort over eight entries is nothing
                    uint32_t slot = candidate_count < candidate_max ? candidate_count : candidate_max - 1;
                    if (candidate_count == candidate_max && box_dist >= candidate_depth[slot])
                    {
                        continue;
                    }

                    while (slot > 0 && candidate_depth[slot - 1] > box_dist)
                    {
                        candidate_depth[slot] = candidate_depth[slot - 1];
                        candidate_index[slot] = candidate_index[slot - 1];
                        slot--;
                    }

                    candidate_depth[slot] = box_dist;
                    candidate_index[slot] = i;
                    candidate_count       = min(candidate_count + 1, candidate_max);
                }

                if (candidate_count == 0)
                {
                    continue;
                }

                m_pick_indices.clear();
                m_pick_vertices.clear();
                render->GetGeometry(&m_pick_indices, &m_pick_vertices);

                for (uint32_t c = 0; c < candidate_count; c++)
                {
                    // a leaf card is one alpha masked quad, its triangles are the silhouette, so the
                    // geometry test is what stops a click in the gap between branches from selecting
                    const float hit = m_pick_indices.empty() || m_pick_vertices.empty()
                        ? candidate_depth[c]
                        : ray_hit_mesh(
                            ray,
                            m_pick_indices,
                            m_pick_vertices,
                            render->GetInstance(candidate_index[c], true),
                            best_depth
                        );

                    if (hit < best_depth)
                    {
                        best_depth    = hit;
                        best_entity   = broad_hit.m_entity;
                        best_instance = static_cast<int>(candidate_index[c]);
                    }
                }

                continue;
            }

            // clear and reuse pre-allocated buffers
            m_pick_indices.clear();
            m_pick_vertices.clear();

            render->GetGeometry(&m_pick_indices, &m_pick_vertices);
            if (m_pick_indices.empty() || m_pick_vertices.empty())
            {
                continue;
            }

            const float hit = ray_hit_mesh(
                ray,
                m_pick_indices,
                m_pick_vertices,
                broad_hit.m_entity->GetMatrix(),
                best_depth
            );
            if (hit < best_depth)
            {
                best_depth    = hit;
                best_entity   = broad_hit.m_entity;
                best_instance = -1;
            }
        }

        m_pick_instance          = best_instance;
        m_pick_instance_owner_id = best_entity ? best_entity->GetObjectId() : 0;

        s_cached_cursor    = cursor;
        s_cached_entity_id = m_pick_instance_owner_id;
        s_cached_instance  = best_instance;
        s_cached_frame     = frame;
        return best_entity;
    }

    Entity* Selection::FindIconUnderCursor(Camera& camera)
    {
        // overlay icons are only drawn in the editor, not while playing
        if (Engine::IsFlagSet(EngineMode::Playing) || !cvar_entity_icons.GetValueAs<bool>())
        {
            return nullptr;
        }

        const Vector2 mouse      = Input::GetMousePositionRelativeToEditorViewport();
        const Vector3 camera_pos = camera.GetEntity()->GetPosition();
        const Vector3 camera_fwd = camera.GetEntity()->GetForward();

        const float icon_pick_padding_px = 6.0f;
        const float icon_half_px         = static_cast<float>(renderer_editor_icon_size_px) * 0.5f + icon_pick_padding_px;

        Entity* best     = nullptr;
        float   best_dist = numeric_limits<float>::max();

        for (Entity* entity : World::GetEntities())
        {
            if (!entity || !entity->GetActive())
            {
                continue;
            }

            // only entities that draw an icon, matches the icon pass
            // renderable meshes are skipped, they are already visible, physics on a mesh
            // must not steal clicks from the terrain surface
            bool draws_icon =
                entity->GetComponent<Light>() != nullptr ||
                entity->GetComponent<Camera>() != nullptr ||
                entity->GetComponent<AudioSource>() != nullptr ||
                entity->GetComponent<ParticleSystem>() != nullptr ||
                entity->GetComponentByType(ComponentType::Volume) != nullptr ||
                entity->GetComponentByType(ComponentType::SpawnPoint) != nullptr ||
                entity->GetComponentByType(ComponentType::Terrain) != nullptr ||
                entity->GetComponentByType(ComponentType::Water) != nullptr ||
                (entity->GetComponentByType(ComponentType::Physics) != nullptr &&
                 entity->GetComponent<Render>() == nullptr) ||
                entity->GetComponentByType(ComponentType::Spline) != nullptr ||
                entity->GetComponentByType(ComponentType::SplineFollower) != nullptr ||
                entity->GetComponentByType(ComponentType::Traffic) != nullptr ||
                entity->GetComponentByType(ComponentType::Pedestrians) != nullptr ||
                entity->GetComponentByType(ComponentType::Animator) != nullptr ||
                entity->GetComponentByType(ComponentType::Ragdoll) != nullptr ||
                entity->GetComponentByType(ComponentType::SkidMarks) != nullptr ||
                entity->GetComponentByType(ComponentType::CarReset) != nullptr ||
                entity->GetComponentByType(ComponentType::Text3D) != nullptr ||
                entity->GetComponentByType(ComponentType::Script) != nullptr;
            if (!draws_icon)
            {
                continue;
            }

            const Vector3 to_entity = entity->GetPosition() - camera_pos;

            // skip icons too close to the camera, matches the icon pass
            if (to_entity.LengthSquared() <= 0.01f)
            {
                continue;
            }

            // skip icons behind or far to the side, matches the icon pass cull (v_dot_l > 0.5)
            if (Vector3::Dot(camera_fwd, to_entity.Normalized()) <= 0.5f)
            {
                continue;
            }

            Vector2 screen;
            camera.WorldToScreenCoordinates(entity->GetPosition(), screen);

            // hit test the mouse against the icon's screen rect
            if (mouse.x < screen.x - icon_half_px || mouse.x > screen.x + icon_half_px ||
                mouse.y < screen.y - icon_half_px || mouse.y > screen.y + icon_half_px)
            {
                continue;
            }

            // when icons overlap, pick the one closest to the camera
            const float dist = to_entity.LengthSquared();
            if (dist < best_dist)
            {
                best_dist = dist;
                best      = entity;
            }
        }

        return best;
    }

    void Selection::Pick(Camera& camera)
    {
        struct PublishOnExit { ~PublishOnExit() { Selection::Publish(); } } publish;
        if (!Input::GetMouseIsInViewport())
        {
            ClearSelection();
            return;
        }

        // editor overlay icons (lights, audio sources, particles) are a 2d projection on top of the scene, so
        // they take priority over geometry picking, clicking one selects its entity in the hierarchy
        if (Entity* icon_entity = FindIconUnderCursor(camera))
        {
            icon_entity = resolve_picked_entity(icon_entity);
            if (Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right))
            {
                ToggleSelection(icon_entity);
            }
            else
            {
                SetSelectedEntity(icon_entity);
            }

            return;
        }

        Entity* best_entity = resolve_picked_entity(FindEntityUnderCursor(camera));

        // spline picking uses its own ray, recompute it here since pick() no longer owns the broadphase
        const Ray& ray                  = ComputePickingRay(camera);
        const vector<Entity*>& entities = World::GetEntities();

        // spline control point picking
        {
            const float pick_radius_px = 20.0f;
            float best_spline_dist     = numeric_limits<float>::max();
            Entity* best_spline_entity = nullptr;

            // ray.m_direction is set to a world-space position (from ScreenToWorldCoordinates),
            // not a normalized direction, so compute the actual direction ourselves
            Vector3 ray_origin = ray.GetStart();
            Vector3 ray_dir    = ray.GetDirection() - ray_origin;
            ray_dir.Normalize();

            for (Entity* entity : entities)
            {
                Spline* spline = entity->GetComponent<Spline>();
                if (!spline)
                {
                    continue;
                }

                // the spline caches its deck handles, snapping each one here would raycast per click
                const vector<Vector3>& handles = spline->GetEditorHandlePositions();
                size_t handle_index            = 0;
                for (uint32_t i = 0; i < entity->GetChildrenCount(); i++)
                {
                    Entity* point_entity = entity->GetChildByIndex(i);
                    if (!point_entity)
                    {
                        continue;
                    }

                    if (point_entity->GetObjectName().find("spline_point_") != 0)
                    {
                        continue;
                    }

                    const size_t current_index = handle_index++;
                    Vector3 world_pos = current_index < handles.size()
                        ? handles[current_index]
                        : point_entity->GetPosition();

                    // depth along the ray direction
                    float depth = (world_pos - ray_origin).Dot(ray_dir);
                    if (depth <= 0.0f)
                    {
                        continue;
                    }

                    // perpendicular distance from the ray to this point: ||(P - O) x D||
                    Vector3 to_point        = world_pos - ray_origin;
                    float distance_from_ray = to_point.Cross(ray_dir).Length();

                    // convert pick radius from screen pixels to world-space at this depth
                    float viewport_width  = Renderer::GetViewport().width;
                    float meters_per_pixel = (2.0f * depth * tanf(camera.GetFovHorizontalRad() * 0.5f)) / viewport_width;
                    float pick_threshold   = pick_radius_px * meters_per_pixel;

                    if (distance_from_ray > pick_threshold)
                    {
                        continue;
                    }
                    if (distance_from_ray < best_spline_dist)
                    {
                        best_spline_dist   = distance_from_ray;
                        best_spline_entity = point_entity;
                    }
                }
            }

            if (best_spline_entity)
            {
                best_entity = best_spline_entity;
            }
        }

        // handle ctrl for multi-select
        if (best_entity)
        {
            if (Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right))
            {
                ToggleSelection(best_entity);
            }
            else
            {
                SetSelectedEntity(best_entity);
            }

            // the selection is the renderable that owns the instances, remember which one was under
            // the cursor so the outline is that one prop and not the whole tile worth of them
            if (best_entity->GetObjectId() == m_pick_instance_owner_id)
            {
                m_selected_instance = m_pick_instance;
            }
        }
        else
        {
            if (!(Input::GetKey(KeyCode::Ctrl_Left) || Input::GetKey(KeyCode::Ctrl_Right)))
            {
                ClearSelection();
            }
        }
    }
    
    vector<Entity*> Selection::m_selected_entities;
    int Selection::m_selected_instance = -1;

    void Selection::SetSelectedEntity(Entity* entity)
    {
        struct PublishOnExit { ~PublishOnExit() { Selection::Publish(); } } publish;
        // any selection that does not come from clicking an instance is the whole renderable, pick()
        // sets this again right after it selects
        m_selected_instance = -1;

        m_selected_entities.clear();
        if (entity)
        {
            m_selected_entities.push_back(entity);
        }
    }
    
    Entity* Selection::GetSelectedEntity()
    {
        return m_selected_entities.empty() ? nullptr : m_selected_entities[0];
    }
    
    void Selection::AddToSelection(Entity* entity)
    {
        struct PublishOnExit { ~PublishOnExit() { Selection::Publish(); } } publish;
        if (!entity)
        {
            return;
        }

        m_selected_instance = -1;
        
        // check if already selected
        for (Entity* e : m_selected_entities)
        {
            if (e && e->GetObjectId() == entity->GetObjectId())
            {
                return;
            }
        }
        
        m_selected_entities.push_back(entity);
    }
    
    void Selection::RemoveFromSelection(Entity* entity)
    {
        struct PublishOnExit { ~PublishOnExit() { Selection::Publish(); } } publish;
        if (!entity)
        {
            return;
        }
        
        m_selected_entities.erase(
            remove_if(m_selected_entities.begin(), m_selected_entities.end(),
                [entity](Entity* e) { return e && e->GetObjectId() == entity->GetObjectId(); }),
            m_selected_entities.end()
        );
    }
    
    void Selection::ToggleSelection(Entity* entity)
    {
        struct PublishOnExit { ~PublishOnExit() { Selection::Publish(); } } publish;
        if (!entity)
        {
            return;
        }
        
        if (IsSelected(entity))
        {
            RemoveFromSelection(entity);
        }
        else
        {
            AddToSelection(entity);
        }
    }
    
    void Selection::ClearSelection()
    {
        struct PublishOnExit { ~PublishOnExit() { Selection::Publish(); } } publish;
        m_selected_entities.clear();
        m_selected_instance = -1;
    }
    
    bool Selection::IsSelected(Entity* entity)
    {
        if (!entity)
        {
            return false;
        }
        
        for (Entity* e : m_selected_entities)
        {
            if (e && e->GetObjectId() == entity->GetObjectId())
            {
                return true;
            }
        }
        return false;
    }

}
