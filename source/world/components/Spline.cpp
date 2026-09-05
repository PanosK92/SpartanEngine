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

//= INCLUDES ============================
#include "pch.h"
#include "Spline.h"
#include "Physics.h"
#include "Render.h"
#include "Terrain.h"
#include "Water.h"
#include "../Entity.h"
#include "../World.h"
#include "../../rendering/Renderer.h"
#include "../../resource/ResourceCache.h"
#include "../../physics/PhysicsWorld.h"
#include "../../core/ProgressTracker.h"
SP_WARNINGS_OFF
#include "../../io/pugixml.hpp"
#include <sol/sol.hpp>
SP_WARNINGS_ON
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    // prefix used to identify control point child entities
    static const string prefix_control_point = "spline_point_";
    static const string prefix_instance      = "spline_instance_";

    static const float min_ground_clearance = 0.25f;
    static const float conform_sag          = 0.05f;

    static float applied_terrain_offset(float offset)
    {
        return max(offset, min_ground_clearance);
    }

    static bool entity_is_terrain(Entity* entity)
    {
        for (Entity* current = entity; current != nullptr; current = current->GetParent())
        {
            if (current->GetComponent<Spline>())
            {
                return false;
            }
        }

        for (Entity* current = entity; current != nullptr; current = current->GetParent())
        {
            if (current->GetComponent<Terrain>())
            {
                return true;
            }
        }

        return false;
    }

    static Water* find_active_water()
    {
        for (Entity* entity : World::GetEntities())
        {
            if (!entity)
            {
                continue;
            }

            if (Water* water = entity->GetComponent<Water>())
            {
                return water;
            }
        }

        return nullptr;
    }

    struct GroundQuery
    {
        Terrain* terrain      = nullptr;
        Entity* ignored       = nullptr;
        float water_surface_y = 0.0f;
        bool has_water        = false;
    };

    static GroundQuery make_ground_query(Entity* ignored)
    {
        GroundQuery query;
        query.ignored = ignored;
        query.terrain = Terrain::FindActive();

        if (Water* water = find_active_water())
        {
            query.has_water        = true;
            query.water_surface_y  = water->GetSeaLevel();
        }
        else if (query.terrain)
        {
            query.has_water        = true;
            query.water_surface_y  = query.terrain->GetSeaLevel();
        }

        return query;
    }

    // heightfield first, sky ray to unbury, never below the water surface
    static bool sample_ground_height(
        float world_x,
        float world_z,
        const GroundQuery& query,
        float& height_out
    )
    {
        // roads grade the terrain, so they have to conform to the ground as it was before any road
        // touched it, otherwise every rebuild would chase the fill it laid down last time
        float heightfield_y  = 0.0f;
        bool has_heightfield = false;
        bool carved          = false;
        if (query.terrain)
        {
            carved          = query.terrain->HasRoadCarve();
            has_heightfield = carved
                              ? query.terrain->SampleHeightBase(world_x, world_z, heightfield_y)
                              : query.terrain->SampleHeight(world_x, world_z, heightfield_y);
        }

        const float sky_y = 10000.0f;
        PhysicsRaycastHit hit;
        bool ray_hit = PhysicsWorld::RaycastStatic(
            Vector3(world_x, sky_y, world_z),
            Vector3::Down,
            sky_y + 5000.0f,
            hit,
            query.ignored
        );

        bool terrain_ray = ray_hit && entity_is_terrain(hit.entity);
        bool found       = false;

        if (has_heightfield)
        {
            height_out = heightfield_y;

            // the collision mesh already carries the carve, trusting it here would feed it back in
            if (terrain_ray && !carved && hit.position.y > height_out)
            {
                height_out = hit.position.y;
            }
            found = true;
        }
        else if (terrain_ray || ray_hit)
        {
            height_out = hit.position.y;
            found = true;
        }

        if (query.has_water)
        {
            if (!found || height_out < query.water_surface_y)
            {
                height_out = query.water_surface_y;
                found      = true;
            }
        }

        return found;
    }

    static float spline_half_extent(const Spline* spline)
    {
        float half_extent = max(spline->GetRoadWidth(), spline->GetRoadWidthEnd()) * 0.5f;
        if (spline->GetProfile() == SplineProfile::Road && spline->GetSidewalkEnabled())
        {
            half_extent += spline->GetSidewalkWidth();
        }

        return max(half_extent, 0.01f);
    }

    static bool sample_deck_height(
        const Vector3& world_pos,
        const Vector3& world_right,
        float half_extent,
        const GroundQuery& query,
        float& height_out,
        float* edge_left_out  = nullptr,
        float* edge_right_out = nullptr
    )
    {
        const uint32_t cross_count = 7;
        float base = -numeric_limits<float>::max();
        bool any_valid = false;

        for (uint32_t s = 0; s < cross_count; s++)
        {
            const float u = -half_extent + (2.0f * half_extent) * (static_cast<float>(s) / static_cast<float>(cross_count - 1));
            const Vector3 p = world_pos + world_right * u;

            float ground_height = 0.0f;
            if (sample_ground_height(p.x, p.z, query, ground_height))
            {
                base = max(base, ground_height);
                any_valid = true;

                // the outermost samples sit exactly on the road edges, the skirt needs them
                if (s == 0 && edge_left_out)
                {
                    *edge_left_out = ground_height;
                }
                if (s == cross_count - 1 && edge_right_out)
                {
                    *edge_right_out = ground_height;
                }
            }
        }

        if (!any_valid)
        {
            return false;
        }

        height_out = base;
        return true;
    }

    // raise samples until no segment climbs or drops faster than the slope limit
    static void grade_limit_from_below(vector<float>& heights, const vector<float>& spans, float slope, bool closed)
    {
        const size_t count = heights.size();
        if (count < 2)
        {
            return;
        }

        const uint32_t rounds = closed ? 3u : 1u;
        for (uint32_t round = 0; round < rounds; round++)
        {
            if (closed)
            {
                const float shared = max(heights.front(), heights.back());
                heights.front()    = shared;
                heights.back()     = shared;
            }

            for (size_t i = 1; i < count; i++)
            {
                heights[i] = max(heights[i], heights[i - 1] - slope * spans[i - 1]);
            }

            for (size_t i = count - 1; i-- > 0;)
            {
                heights[i] = max(heights[i], heights[i + 1] - slope * spans[i]);
            }
        }
    }

    // mirror of the above, lowers samples instead
    static void grade_limit_from_above(vector<float>& heights, const vector<float>& spans, float slope, bool closed)
    {
        const size_t count = heights.size();
        if (count < 2)
        {
            return;
        }

        const uint32_t rounds = closed ? 3u : 1u;
        for (uint32_t round = 0; round < rounds; round++)
        {
            if (closed)
            {
                const float shared = min(heights.front(), heights.back());
                heights.front()    = shared;
                heights.back()     = shared;
            }

            for (size_t i = 1; i < count; i++)
            {
                heights[i] = min(heights[i], heights[i - 1] + slope * spans[i - 1]);
            }

            for (size_t i = count - 1; i-- > 0;)
            {
                heights[i] = min(heights[i], heights[i + 1] + slope * spans[i]);
            }
        }
    }

    // running average of the profile over a fixed arc length, the window is in meters so the result
    // does not change when the spline happens to be sampled more densely
    static void smooth_profile_by_length(vector<float>& heights, const vector<float>& spans, float length, bool closed)
    {
        const size_t count = heights.size();
        if (count < 3 || length <= 0.0f || spans.size() + 1 != count)
        {
            return;
        }

        vector<float> distance(count, 0.0f);
        for (size_t i = 1; i < count; i++)
        {
            distance[i] = distance[i - 1] + spans[i - 1];
        }

        const float total = distance.back();
        if (total < 1e-3f)
        {
            return;
        }

        const float radius = min(length * 0.5f, total * 0.5f);
        if (radius < 1e-3f)
        {
            return;
        }

        // integrating first turns each window average into two lookups instead of a scan
        vector<float> integral(count, 0.0f);
        for (size_t i = 1; i < count; i++)
        {
            integral[i] = integral[i - 1] + (heights[i] + heights[i - 1]) * 0.5f * spans[i - 1];
        }

        auto integral_at = [&](float s) -> float
        {
            // past either end the profile continues flat, which keeps the endpoints on their own height
            if (s <= 0.0f)
            {
                return integral.front() + heights.front() * s;
            }

            if (s >= total)
            {
                return integral.back() + heights.back() * (s - total);
            }

            const size_t hi  = static_cast<size_t>(lower_bound(distance.begin(), distance.end(), s) - distance.begin());
            const size_t lo  = (hi > 0) ? hi - 1 : 0;
            const float span = distance[hi] - distance[lo];
            const float t    = (span > 1e-6f) ? (s - distance[lo]) / span : 0.0f;
            const float h    = heights[lo] + (heights[hi] - heights[lo]) * t;

            return integral[lo] + (heights[lo] + h) * 0.5f * (s - distance[lo]);
        };

        vector<float> result(count);
        for (size_t i = 0; i < count; i++)
        {
            const float a = distance[i] - radius;
            const float b = distance[i] + radius;
            result[i]     = (integral_at(b) - integral_at(a)) / (b - a);
        }

        if (closed)
        {
            const float shared = (result.front() + result.back()) * 0.5f;
            result.front()     = shared;
            result.back()      = shared;
        }

        heights = move(result);
    }

    // a designed road is a long smooth curve fitted through the hills, not a drape over them, so the
    // profile is first averaged over a design length, then clamped to the deepest allowed cut, then
    // turned into the higher of two slope limited profiles which gives fill on the approach to a crest
    // and a cut through it
    static void apply_grade_limit(
        vector<float>& deck_heights,
        const vector<float>& ground_heights,
        const vector<float>& spans,
        float slope,
        float max_cut,
        float smoothing,
        float smoothing_length,
        bool closed
    )
    {
        const size_t count = deck_heights.size();
        if (count < 3 || spans.size() + 1 != count)
        {
            return;
        }

        const float blend = saturate(smoothing);
        if (blend > 0.0f && smoothing_length > 0.0f)
        {
            // two box passes approximate a triangular kernel, enough to kill the curvature kinks
            vector<float> target = deck_heights;
            smooth_profile_by_length(target, spans, smoothing_length, closed);
            smooth_profile_by_length(target, spans, smoothing_length, closed);

            for (size_t i = 0; i < count; i++)
            {
                deck_heights[i] += (target[i] - deck_heights[i]) * blend;
            }
        }

        // the lowest the deck may ever sit, cutting into a hill is cheaper than an endless ramp
        vector<float> floor_heights(count);
        for (size_t i = 0; i < count; i++)
        {
            floor_heights[i] = ground_heights[i] - max_cut;
            deck_heights[i]  = max(deck_heights[i], floor_heights[i]);
        }

        vector<float> fill_profile = floor_heights;
        grade_limit_from_below(fill_profile, spans, slope, closed);

        vector<float> cut_profile = deck_heights;
        grade_limit_from_above(cut_profile, spans, slope, closed);

        // both profiles respect the slope limit, so their maximum does too
        for (size_t i = 0; i < count; i++)
        {
            deck_heights[i] = max(fill_profile[i], cut_profile[i]);
        }
    }

    static float snapped_control_point_y(
        const Vector3& world_pos,
        const Vector3& world_right,
        float half_extent,
        float terrain_offset,
        const GroundQuery& query
    )
    {
        float base = world_pos.y;
        if (!sample_deck_height(world_pos, world_right, half_extent, query, base))
        {
            return world_pos.y;
        }

        return base + applied_terrain_offset(terrain_offset) + conform_sag;
    }

    // drop from the entity origin to the lowest point of its meshes, pivots are rarely at the base
    static float pivot_to_bottom(Entity* entity)
    {
        if (!entity)
        {
            return 0.0f;
        }

        vector<Entity*> parts;
        parts.push_back(entity);
        entity->GetDescendants(&parts);

        float lowest = numeric_limits<float>::max();
        for (Entity* part : parts)
        {
            Render* render = part ? part->GetComponent<Render>() : nullptr;
            if (!render || !render->GetMesh())
            {
                continue;
            }

            const BoundingBox box = render->GetBoundingBoxMesh() * part->GetMatrix();
            lowest                = min(lowest, box.GetMin().y);
        }

        if (lowest == numeric_limits<float>::max())
        {
            return 0.0f;
        }

        return entity->GetPosition().y - lowest;
    }

    Spline::Spline(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_closed_loop, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_resolution, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_road_width, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_needs_road_regeneration, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mesh_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_profile, SplineProfile);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_height, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_thickness, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_tube_sides, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_road_width_end, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_uv_tiling_u, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_uv_tiling_v, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_sidewalk_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_sidewalk_width, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_curb_height, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_conform_to_terrain, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_terrain_offset, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_grade_limit_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_grade_degrees, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_max_cut, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_grade_smoothing, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_smoothing_length, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_embankment_enabled, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_embankment_slope_degrees, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_embankment_max_height, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_carve_terrain, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_carve_bed_drop, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_carve_fill_slope_degrees, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_carve_cut_slope_degrees, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_carve_max_shoulder, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_source_spline_entity_id, SetSourceSplineEntityId, uint64_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_attach_mode, SplineAttachMode);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_attach_lateral_offset, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_attach_vertical_offset, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_attach_inherit_closed_loop, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_attach_sample_count, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_spacing, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_align_instances_to_spline, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_mesh_path, std::string);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_template_id, uint64_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_lateral_offset, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_mirror, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_face_inward, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_random_offset, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_random_scale_min, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_random_scale_max, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_instance_random_yaw, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_saved_material_name, std::string);

        // regenerate the mesh on scene boot so meshes appear without requiring play mode
        m_world_loaded_handle = SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoaded, SP_EVENT_HANDLER(OnWorldLoaded));
    }

    Spline::~Spline()
    {
        if (m_world_loaded_handle != 0)
        {
            SP_UNSUBSCRIBE_FROM_EVENT(EventType::WorldLoaded, m_world_loaded_handle);
            m_world_loaded_handle = 0;
        }

        ClearRoadMesh();

        // the terrain still holds this road's cut and fill, tell it to hand that ground back
        if (m_carve_entity_id != 0)
        {
            if (Terrain* terrain = Terrain::FindActive())
            {
                terrain->MarkSplineHeightCarvesDirty(m_carve_entity_id);
            }
        }

        // no ClearInstances here, shutdown may leave dangling entities and the world already removes descendants
    }

    void Spline::OnWorldLoaded()
    {
        // resolve the source spline entity once the world finished loading so order doesn't matter
        ResolveSourceSplineEntity();

        // inherit closed loop from source if requested
        if (IsAttached() && m_attach_inherit_closed_loop && m_source_spline_entity)
        {
            if (Spline* source = m_source_spline_entity->GetComponent<Spline>())
            {
                m_closed_loop = source->GetClosedLoop();
            }
        }

        // regenerate the road mesh if the saved scene had one
        if (m_needs_road_regeneration)
        {
            m_needs_road_regeneration = false;

            bool has_input = IsAttached() ? (m_source_spline_entity != nullptr) : (GetControlPointCount() >= 2);
            if (m_mesh_enabled && has_input)
            {
                GenerateRoadMesh();
                SnapshotState();
            }
        }

        // auto-spawn instances when a template is configured
        // spawned instances are transient (not saved) so they always regenerate from the spline config
        bool can_spawn = IsAttached() ? (m_source_spline_entity != nullptr) : (GetControlPointCount() >= 2);
        if (m_instance_template_id != 0 && can_spawn)
        {
            SpawnInstances();
        }
    }

    void Spline::RegisterForScripting(sol::state_view state)
    {
        state.new_usertype<Spline>("Spline",
            "GetPoint",             &Spline::GetPoint,
            "GetTangent",           &Spline::GetTangent,
            "GetLength",            [](Spline* self) { return self->GetLength(); },
            "GetTAtDistance",       [](Spline* self, float distance) { return self->GetTAtDistance(distance); },
            "GetControlPointCount", &Spline::GetControlPointCount,
            "GetClosedLoop",        &Spline::GetClosedLoop,
            "GetRoadWidth",         &Spline::GetRoadWidth,
            "GetCurveAlpha",        &Spline::GetCurveAlpha,
            "SetCurveAlpha",        &Spline::SetCurveAlpha,
            "AddControlPoint",      &Spline::AddControlPoint,
            "RemoveLastControlPoint", &Spline::RemoveLastControlPoint,
            "ResampleControlPoints", &Spline::ResampleControlPoints,
            "SimplifyControlPoints", &Spline::SimplifyControlPoints,
            "GenerateRoadMesh",     &Spline::GenerateRoadMesh,
            "ClearRoadMesh",        &Spline::ClearRoadMesh
        );
    }

    sol::reference Spline::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void Spline::SnapshotState()
    {
        m_prev_closed_loop        = m_closed_loop;
        m_prev_resolution         = m_resolution;
        m_prev_road_width         = m_road_width;
        m_prev_curve_alpha        = m_curve_alpha;
        m_prev_road_width_end     = m_road_width_end;
        m_prev_profile            = m_profile;
        m_prev_height             = m_height;
        m_prev_thickness          = m_thickness;
        m_prev_tube_sides         = m_tube_sides;
        m_prev_uv_tiling_u        = m_uv_tiling_u;
        m_prev_uv_tiling_v        = m_uv_tiling_v;
        m_prev_sidewalk_enabled   = m_sidewalk_enabled;
        m_prev_sidewalk_width     = m_sidewalk_width;
        m_prev_curb_height        = m_curb_height;
        m_prev_conform_to_terrain = m_conform_to_terrain;
        m_prev_terrain_offset     = m_terrain_offset;
        m_prev_control_points     = GetControlPointsLocal();

        m_prev_grade_limit_enabled       = m_grade_limit_enabled;
        m_prev_max_grade_degrees         = m_max_grade_degrees;
        m_prev_max_cut                   = m_max_cut;
        m_prev_grade_smoothing           = m_grade_smoothing;
        m_prev_smoothing_length          = m_smoothing_length;
        m_prev_embankment_enabled        = m_embankment_enabled;
        m_prev_embankment_slope_degrees  = m_embankment_slope_degrees;
        m_prev_embankment_max_height     = m_embankment_max_height;
        m_prev_carve_terrain             = m_carve_terrain;
        m_prev_carve_bed_drop            = m_carve_bed_drop;
        m_prev_carve_fill_slope_degrees  = m_carve_fill_slope_degrees;
        m_prev_carve_cut_slope_degrees   = m_carve_cut_slope_degrees;
        m_prev_carve_max_shoulder        = m_carve_max_shoulder;

        m_prev_attach_mode                = m_attach_mode;
        m_prev_source_spline_entity_id    = m_source_spline_entity_id;
        m_prev_attach_lateral_offset      = m_attach_lateral_offset;
        m_prev_attach_vertical_offset     = m_attach_vertical_offset;
        m_prev_attach_inherit_closed_loop = m_attach_inherit_closed_loop;
        m_prev_attach_sample_count        = m_attach_sample_count;
        m_prev_source_hash                = ComputeSourceHash();

        if (m_entity_ptr)
        {
            m_prev_world_position = m_entity_ptr->GetPosition();
            m_prev_world_rotation = m_entity_ptr->GetRotation();
            m_prev_world_scale    = m_entity_ptr->GetScale();
        }
    }

    void Spline::Tick()
    {
        if (ProgressTracker::IsLoading())
        {
            return;
        }

        // resolve the source if it has not been resolved yet (e.g. after a fresh component add)
        if (m_source_spline_entity_id != 0 && !m_source_spline_entity)
        {
            ResolveSourceSplineEntity();
        }

        // mirror the source closed loop state when requested
        if (IsAttached() && m_attach_inherit_closed_loop && m_source_spline_entity)
        {
            if (Spline* source = m_source_spline_entity->GetComponent<Spline>())
            {
                m_closed_loop = source->GetClosedLoop();
            }
        }

        // auto-regenerate mesh when any property/control point changes, or when mesh is enabled but missing
        uint32_t control_point_count = GetControlPointCount();
        bool has_mesh_input = IsAttached() ? (m_source_spline_entity != nullptr) : (control_point_count >= 2);

        if (has_mesh_input)
        {
            vector<Vector3> current_points = GetControlPointsLocal();
            bool mesh_missing              = m_mesh_enabled && !HasRoadMesh();
            uint64_t source_hash           = ComputeSourceHash();

            bool transform_dirty = false;
            if (m_conform_to_terrain && m_entity_ptr)
            {
                transform_dirty =
                    m_entity_ptr->GetPosition() != m_prev_world_position ||
                    m_entity_ptr->GetRotation() != m_prev_world_rotation ||
                    m_entity_ptr->GetScale()    != m_prev_world_scale;
            }

            bool dirty = (m_closed_loop                != m_prev_closed_loop)
                      || (m_resolution                 != m_prev_resolution)
                      || (m_road_width                 != m_prev_road_width)
                      || (m_curve_alpha                != m_prev_curve_alpha)
                      || (m_road_width_end             != m_prev_road_width_end)
                      || (m_profile                    != m_prev_profile)
                      || (m_height                     != m_prev_height)
                      || (m_thickness                  != m_prev_thickness)
                      || (m_tube_sides                 != m_prev_tube_sides)
                      || (m_uv_tiling_u                != m_prev_uv_tiling_u)
                      || (m_uv_tiling_v                != m_prev_uv_tiling_v)
                      || (m_sidewalk_enabled           != m_prev_sidewalk_enabled)
                      || (m_sidewalk_width             != m_prev_sidewalk_width)
                      || (m_curb_height                != m_prev_curb_height)
                      || (m_conform_to_terrain         != m_prev_conform_to_terrain)
                      || (m_terrain_offset             != m_prev_terrain_offset)
                      || (m_grade_limit_enabled        != m_prev_grade_limit_enabled)
                      || (m_max_grade_degrees          != m_prev_max_grade_degrees)
                      || (m_max_cut                    != m_prev_max_cut)
                      || (m_grade_smoothing            != m_prev_grade_smoothing)
                      || (m_smoothing_length           != m_prev_smoothing_length)
                      || (m_embankment_enabled         != m_prev_embankment_enabled)
                      || (m_embankment_slope_degrees   != m_prev_embankment_slope_degrees)
                      || (m_embankment_max_height      != m_prev_embankment_max_height)
                      || (m_carve_terrain              != m_prev_carve_terrain)
                      || (m_carve_bed_drop             != m_prev_carve_bed_drop)
                      || (m_carve_fill_slope_degrees   != m_prev_carve_fill_slope_degrees)
                      || (m_carve_cut_slope_degrees    != m_prev_carve_cut_slope_degrees)
                      || (m_carve_max_shoulder         != m_prev_carve_max_shoulder)
                      || (current_points               != m_prev_control_points)
                      || (m_attach_mode                != m_prev_attach_mode)
                      || (m_source_spline_entity_id    != m_prev_source_spline_entity_id)
                      || (m_attach_lateral_offset      != m_prev_attach_lateral_offset)
                      || (m_attach_vertical_offset     != m_prev_attach_vertical_offset)
                      || (m_attach_inherit_closed_loop != m_prev_attach_inherit_closed_loop)
                      || (m_attach_sample_count        != m_prev_attach_sample_count)
                      || (source_hash                  != m_prev_source_hash)
                      || transform_dirty;

            if (dirty || mesh_missing)
            {
                if (m_mesh_enabled)
                {
                    GenerateRoadMesh();
                }

                // instances ride the same frames as the road, they have to move with it
                if (HasSpawnedInstances())
                {
                    SpawnInstances();
                }

                SnapshotState();
                RefreshHandlePositions();
            }
        }
        else if (m_mesh_enabled && !has_mesh_input && HasRoadMesh())
        {
            ClearRoadMesh();
        }

        if (m_entity_ptr)
        {
            m_prev_world_position = m_entity_ptr->GetPosition();
            m_prev_world_rotation = m_entity_ptr->GetRotation();
            m_prev_world_scale    = m_entity_ptr->GetScale();
        }

        // debug lines are edit only, road meshes already generated above
        if (Engine::IsFlagSet(EngineMode::Playing))
        {
            return;
        }

        const Color color_curve = Color(0.3f, 0.85f, 0.75f, 1.0f);
        const Color color_point = Color(1.0f, 0.8f, 0.3f, 1.0f);

        // attached splines visualize their derived path by walking GetPoint
        if (IsAttached() && m_source_spline_entity)
        {
            // the generated mesh is the path, keep the flying guide for path mode only
            if (m_mesh_enabled)
            {
                return;
            }

            Spline* source = m_source_spline_entity->GetComponent<Spline>();
            if (source && source->GetControlPointCount() >= 2)
            {
                uint32_t total_segments = max(2u, m_resolution * 4u);
                Vector3 prev_point      = GetPoint(0.0f);
                for (uint32_t i = 1; i <= total_segments; i++)
                {
                    float t            = static_cast<float>(i) / static_cast<float>(total_segments);
                    Vector3 curr_point = GetPoint(t);
                    Renderer::DrawLine(prev_point, curr_point, color_curve, color_curve);
                    prev_point = curr_point;
                }
            }
            return;
        }

        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
        {
            return;
        }

        // path mode keeps the flying guide, road mode is the mesh itself
        if (!m_mesh_enabled)
        {
            uint32_t span_count = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
            for (uint32_t span = 0; span < span_count; span++)
            {
                Vector3 prev_point;
                for (uint32_t seg = 0; seg <= m_resolution; seg++)
                {
                    float local_t = static_cast<float>(seg) / static_cast<float>(m_resolution);

                    // determine the four control points for this span
                    int32_t i0 = 0;
                    int32_t i1 = 0;
                    int32_t i2 = 0;
                    int32_t i3 = 0;
                    GetSpanIndices(span, points.size(), i0, i1, i2, i3);

                    Vector3 current_point = CatmullRom(points[i0], points[i1], points[i2], points[i3], local_t, m_curve_alpha);

                    if (seg > 0)
                    {
                        Renderer::DrawLine(prev_point, current_point, color_curve, color_curve);
                    }

                    prev_point = current_point;
                }
            }
        }

        // draw markers at each control point, from the cache so no raycasts run per frame
        if (m_handle_positions.size() != points.size())
        {
            RefreshHandlePositions();
        }

        float marker_size = 0.15f;
        for (const Vector3& point : m_handle_positions)
        {
            Renderer::DrawLine(point - Vector3(marker_size, 0, 0), point + Vector3(marker_size, 0, 0), color_point, color_point);
            Renderer::DrawLine(point - Vector3(0, marker_size, 0), point + Vector3(0, marker_size, 0), color_point, color_point);
            Renderer::DrawLine(point - Vector3(0, 0, marker_size), point + Vector3(0, 0, marker_size), color_point, color_point);
        }
    }

    void Spline::Save(pugi::xml_node& node)
    {
        node.append_attribute("closed_loop")   = m_closed_loop;
        node.append_attribute("resolution")    = m_resolution;
        node.append_attribute("road_width")    = m_road_width;
        node.append_attribute("curve_alpha")   = m_curve_alpha;
        node.append_attribute("mesh_enabled")  = m_mesh_enabled;
        node.append_attribute("has_road_mesh") = HasRoadMesh();

        // profile
        node.append_attribute("profile")       = static_cast<uint32_t>(m_profile);
        node.append_attribute("height")        = m_height;
        node.append_attribute("thickness")     = m_thickness;
        node.append_attribute("tube_sides")    = m_tube_sides;
        node.append_attribute("road_width_end") = m_road_width_end;

        // uv tiling
        node.append_attribute("uv_tiling_u") = m_uv_tiling_u;
        node.append_attribute("uv_tiling_v") = m_uv_tiling_v;

        // sidewalk
        node.append_attribute("sidewalk_enabled") = m_sidewalk_enabled;
        node.append_attribute("sidewalk_width")   = m_sidewalk_width;
        node.append_attribute("curb_height")      = m_curb_height;

        // terrain conforming
        node.append_attribute("conform_to_terrain") = m_conform_to_terrain;
        node.append_attribute("terrain_offset")     = m_terrain_offset;

        // grade limiting and embankment
        node.append_attribute("grade_limit_enabled")      = m_grade_limit_enabled;
        node.append_attribute("max_grade_degrees")        = m_max_grade_degrees;
        node.append_attribute("max_cut")                  = m_max_cut;
        node.append_attribute("grade_smoothing")          = m_grade_smoothing;
        node.append_attribute("smoothing_length")         = m_smoothing_length;
        node.append_attribute("embankment_enabled")       = m_embankment_enabled;
        node.append_attribute("embankment_slope_degrees") = m_embankment_slope_degrees;
        node.append_attribute("embankment_max_height")    = m_embankment_max_height;

        // terrain carving
        node.append_attribute("carve_terrain")             = m_carve_terrain;
        node.append_attribute("carve_bed_drop")            = m_carve_bed_drop;
        node.append_attribute("carve_fill_slope_degrees")  = m_carve_fill_slope_degrees;
        node.append_attribute("carve_cut_slope_degrees")   = m_carve_cut_slope_degrees;
        node.append_attribute("carve_max_shoulder")        = m_carve_max_shoulder;

        // instancing
        node.append_attribute("instance_spacing")          = m_instance_spacing;
        node.append_attribute("align_instances")           = m_align_instances_to_spline;
        node.append_attribute("instance_mesh_path")        = m_instance_mesh_path.c_str();
        node.append_attribute("instance_template_id")      = m_instance_template_id;
        node.append_attribute("instance_lateral_offset")   = m_instance_lateral_offset;
        node.append_attribute("instance_mirror")           = m_instance_mirror;
        node.append_attribute("instance_face_inward")      = m_instance_face_inward;
        node.append_attribute("instance_random_offset")    = m_instance_random_offset;
        node.append_attribute("instance_random_scale_min") = m_instance_random_scale_min;
        node.append_attribute("instance_random_scale_max") = m_instance_random_scale_max;
        node.append_attribute("instance_random_yaw")       = m_instance_random_yaw;

        // attachment
        node.append_attribute("source_spline_id")           = m_source_spline_entity_id;
        node.append_attribute("attach_mode")                = static_cast<uint32_t>(m_attach_mode);
        node.append_attribute("attach_lateral_offset")      = m_attach_lateral_offset;
        node.append_attribute("attach_vertical_offset")     = m_attach_vertical_offset;
        node.append_attribute("attach_inherit_closed_loop") = m_attach_inherit_closed_loop;
        node.append_attribute("attach_sample_count")        = m_attach_sample_count;
    }

    void Spline::Load(pugi::xml_node& node)
    {
        m_closed_loop             = node.attribute("closed_loop").as_bool(false);
        m_resolution              = node.attribute("resolution").as_uint(20);
        m_road_width              = node.attribute("road_width").as_float(8.0f);
        m_curve_alpha             = clamp(node.attribute("curve_alpha").as_float(0.5f), 0.0f, 1.0f);
        m_needs_road_regeneration = node.attribute("has_road_mesh").as_bool(false);
        m_mesh_enabled            = node.attribute("mesh_enabled").as_bool(m_needs_road_regeneration);

        // profile
        m_profile        = static_cast<SplineProfile>(node.attribute("profile").as_uint(static_cast<uint32_t>(SplineProfile::Road)));
        m_height         = node.attribute("height").as_float(3.0f);
        m_thickness      = node.attribute("thickness").as_float(0.3f);
        m_tube_sides     = node.attribute("tube_sides").as_uint(12);
        m_road_width_end = node.attribute("road_width_end").as_float(m_road_width);

        // uv tiling
        m_uv_tiling_u = node.attribute("uv_tiling_u").as_float(1.0f);
        m_uv_tiling_v = node.attribute("uv_tiling_v").as_float(1.0f);

        // sidewalk
        m_sidewalk_enabled = node.attribute("sidewalk_enabled").as_bool(false);
        m_sidewalk_width   = node.attribute("sidewalk_width").as_float(2.0f);
        m_curb_height      = node.attribute("curb_height").as_float(0.15f);

        // terrain conforming
        m_conform_to_terrain = node.attribute("conform_to_terrain").as_bool(false);
        m_terrain_offset     = node.attribute("terrain_offset").as_float(0.25f);
        if (m_terrain_offset < min_ground_clearance)
        {
            m_terrain_offset = min_ground_clearance;
        }

        // grade limiting and embankment, older worlds get the new behaviour by default
        m_grade_limit_enabled      = node.attribute("grade_limit_enabled").as_bool(true);
        m_max_grade_degrees        = node.attribute("max_grade_degrees").as_float(8.0f);
        m_max_cut                  = node.attribute("max_cut").as_float(20.0f);
        m_grade_smoothing          = node.attribute("grade_smoothing").as_float(0.9f);
        m_smoothing_length         = node.attribute("smoothing_length").as_float(160.0f);
        m_embankment_enabled       = node.attribute("embankment_enabled").as_bool(true);
        m_embankment_slope_degrees = node.attribute("embankment_slope_degrees").as_float(50.0f);
        m_embankment_max_height    = node.attribute("embankment_max_height").as_float(8.0f);

        m_carve_terrain             = node.attribute("carve_terrain").as_bool(true);
        m_carve_bed_drop            = node.attribute("carve_bed_drop").as_float(0.15f);
        m_carve_fill_slope_degrees  = node.attribute("carve_fill_slope_degrees").as_float(33.0f);
        m_carve_cut_slope_degrees   = node.attribute("carve_cut_slope_degrees").as_float(45.0f);
        m_carve_max_shoulder        = node.attribute("carve_max_shoulder").as_float(60.0f);

        // instancing
        m_instance_spacing           = node.attribute("instance_spacing").as_float(5.0f);
        m_align_instances_to_spline  = node.attribute("align_instances").as_bool(true);
        m_instance_mesh_path         = node.attribute("instance_mesh_path").as_string("");
        m_instance_template_id       = node.attribute("instance_template_id").as_ullong(0);
        m_instance_lateral_offset    = node.attribute("instance_lateral_offset").as_float(0.0f);
        m_instance_mirror            = node.attribute("instance_mirror").as_bool(false);
        m_instance_face_inward       = node.attribute("instance_face_inward").as_bool(false);
        m_instance_random_offset     = node.attribute("instance_random_offset").as_float(0.0f);
        m_instance_random_scale_min  = node.attribute("instance_random_scale_min").as_float(1.0f);
        m_instance_random_scale_max  = node.attribute("instance_random_scale_max").as_float(1.0f);
        m_instance_random_yaw        = node.attribute("instance_random_yaw").as_float(0.0f);

        // attachment
        m_source_spline_entity_id     = node.attribute("source_spline_id").as_ullong(0);
        m_attach_mode                 = static_cast<SplineAttachMode>(node.attribute("attach_mode").as_uint(static_cast<uint32_t>(SplineAttachMode::None)));
        m_attach_lateral_offset       = node.attribute("attach_lateral_offset").as_float(0.0f);
        m_attach_vertical_offset      = node.attribute("attach_vertical_offset").as_float(0.0f);
        m_attach_inherit_closed_loop  = node.attribute("attach_inherit_closed_loop").as_bool(true);
        m_attach_sample_count         = node.attribute("attach_sample_count").as_uint(0);
        m_source_spline_entity        = nullptr;

        // if a mesh was saved, remove the render and physics components as they will be recreated
        // save the material name first so it can be restored after regeneration
        if (m_needs_road_regeneration && m_entity_ptr)
        {
            if (Render* render = m_entity_ptr->GetComponent<Render>())
            {
                if (Material* material = render->GetMaterial())
                {
                    m_saved_material_name = material->GetObjectName();
                }
            }

            m_entity_ptr->RemoveComponent<Render>();
            m_entity_ptr->RemoveComponent<Physics>();
        }
    }

    Vector3 Spline::GetPoint(float t) const
    {
        if (IsAttached() && m_source_spline_entity)
        {
            if (Spline* source = m_source_spline_entity->GetComponent<Spline>())
            {
                Vector3 world_pos = source->GetPoint(t);
                Vector3 world_tan = source->GetTangent(t);
                if (world_tan.LengthSquared() < 1e-6f)
                {
                    world_tan = Vector3::Forward;
                }
                world_tan.Normalize();

                Vector3 world_up = Vector3::Up;
                if (abs(world_tan.Dot(Vector3::Up)) > 0.99f)
                {
                    world_up = Vector3::Forward;
                }
                Vector3 world_right = world_tan.Cross(world_up);
                world_right.Normalize();

                float side = 0.0f;
                switch (m_attach_mode)
                {
                    case SplineAttachMode::LeftEdge:
                    case SplineAttachMode::LeftOuter:  side = -1.0f; break;
                    case SplineAttachMode::RightEdge:
                    case SplineAttachMode::RightOuter: side = +1.0f; break;
                    default:                           side =  0.0f; break;
                }

                bool source_has_sidewalk = source->GetSidewalkEnabled() && source->GetProfile() == SplineProfile::Road;
                float source_half_width  = (source->GetRoadWidth() + (source->GetRoadWidthEnd() - source->GetRoadWidth()) * t) * 0.5f;
                float edge_offset        = 0.0f;
                if (m_attach_mode == SplineAttachMode::LeftEdge || m_attach_mode == SplineAttachMode::RightEdge)
                {
                    edge_offset = source_half_width;
                }
                else if (m_attach_mode == SplineAttachMode::LeftOuter || m_attach_mode == SplineAttachMode::RightOuter)
                {
                    edge_offset = source_half_width + (source_has_sidewalk ? source->GetSidewalkWidth() : 0.0f);
                }

                float lateral = (m_attach_mode == SplineAttachMode::Centerline)
                                ? m_attach_lateral_offset
                                : side * (edge_offset + m_attach_lateral_offset);

                return world_pos + world_right * lateral + Vector3::Up * m_attach_vertical_offset;
            }
        }

        return EvaluatePoint(GetControlPoints(), t);
    }

    Vector3 Spline::GetTangent(float t) const
    {
        if (IsAttached() && m_source_spline_entity)
        {
            if (Spline* source = m_source_spline_entity->GetComponent<Spline>())
            {
                return source->GetTangent(t);
            }
        }
        return EvaluateTangent(GetControlPoints(), t);
    }

    float Spline::GetLength(uint32_t samples_per_span) const
    {
        if (IsAttached() && m_source_spline_entity)
        {
            // walk the offset curve to compute its arc length
            uint32_t total_samples = max(2u, samples_per_span * 4);
            float length           = 0.0f;
            Vector3 prev_point     = GetPoint(0.0f);
            for (uint32_t i = 1; i <= total_samples; i++)
            {
                float t            = static_cast<float>(i) / static_cast<float>(total_samples);
                Vector3 curr_point = GetPoint(t);
                length            += prev_point.Distance(curr_point);
                prev_point         = curr_point;
            }
            return length;
        }

        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
        {
            return 0.0f;
        }

        uint32_t span_count    = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
        uint32_t total_samples = span_count * samples_per_span;
        float length           = 0.0f;
        Vector3 prev_point     = EvaluatePoint(points, 0.0f);

        for (uint32_t i = 1; i <= total_samples; i++)
        {
            float t            = static_cast<float>(i) / static_cast<float>(total_samples);
            Vector3 curr_point = EvaluatePoint(points, t);
            length            += prev_point.Distance(curr_point);
            prev_point         = curr_point;
        }

        return length;
    }

    float Spline::GetTAtDistance(float distance, uint32_t samples_per_span) const
    {
        // spans are unevenly spaced so parametric t is not proportional to distance, walk the arc length to invert it
        uint32_t span_count    = m_closed_loop ? GetControlPointCount() : (GetControlPointCount() > 0 ? GetControlPointCount() - 1 : 0);
        uint32_t total_samples = max(2u, span_count * samples_per_span);

        if (distance <= 0.0f)
        {
            return 0.0f;
        }

        float accumulated  = 0.0f;
        Vector3 prev_point = GetPoint(0.0f);
        for (uint32_t i = 1; i <= total_samples; i++)
        {
            float t             = static_cast<float>(i) / static_cast<float>(total_samples);
            Vector3 curr_point  = GetPoint(t);
            float segment       = prev_point.Distance(curr_point);
            if (accumulated + segment >= distance && segment > 0.0f)
            {
                float prev_t = static_cast<float>(i - 1) / static_cast<float>(total_samples);
                return prev_t + (t - prev_t) * ((distance - accumulated) / segment);
            }
            accumulated += segment;
            prev_point   = curr_point;
        }

        return 1.0f;
    }

    uint32_t Spline::GetControlPointCount() const
    {
        if (!m_entity_ptr)
        {
            return 0;
        }

        // count only children that are control points (not instances)
        uint32_t count       = 0;
        uint32_t child_count = m_entity_ptr->GetChildrenCount();
        for (uint32_t i = 0; i < child_count; i++)
        {
            if (Entity* child = m_entity_ptr->GetChildByIndex(i))
            {
                if (child->GetObjectName().find(prefix_control_point) == 0)
                {
                    count++;
                }
            }
        }

        return count;
    }

    void Spline::AddControlPoint(const Vector3& local_position)
    {
        if (!m_entity_ptr)
        {
            return;
        }

        Entity* point = World::CreateEntity();

        // name the point based on its index
        uint32_t index = GetControlPointCount();
        point->SetObjectName(prefix_control_point + to_string(index));
        point->SetParent(m_entity_ptr);
        point->SetPositionLocal(local_position);
    }

    void Spline::RemoveLastControlPoint()
    {
        if (!m_entity_ptr || m_entity_ptr->GetChildrenCount() == 0)
        {
            return;
        }

        // find the last control point child (not an instance)
        Entity* last_point    = nullptr;
        uint32_t child_count  = m_entity_ptr->GetChildrenCount();
        for (uint32_t i = child_count; i > 0; i--)
        {
            if (Entity* child = m_entity_ptr->GetChildByIndex(i - 1))
            {
                if (child->GetObjectName().find(prefix_control_point) == 0)
                {
                    last_point = child;
                    break;
                }
            }
        }

        if (last_point)
        {
            World::RemoveEntity(last_point);
        }
    }

    // rewrite the control point children so they match the given local positions, in order
    static void set_control_points_local(Entity* parent, const vector<Vector3>& local_points)
    {
        vector<Entity*> existing;
        const uint32_t child_count = parent->GetChildrenCount();
        for (uint32_t i = 0; i < child_count; i++)
        {
            if (Entity* child = parent->GetChildByIndex(i))
            {
                if (child->GetObjectName().find(prefix_control_point) == 0)
                {
                    existing.push_back(child);
                }
            }
        }

        // surplus children go first so the naming below stays dense
        for (size_t i = local_points.size(); i < existing.size(); i++)
        {
            World::RemoveEntity(existing[i]);
        }

        for (size_t i = 0; i < local_points.size(); i++)
        {
            Entity* point = nullptr;
            if (i < existing.size())
            {
                point = existing[i];
            }
            else
            {
                point = World::CreateEntity();
                point->SetParent(parent);
            }

            point->SetObjectName(prefix_control_point + to_string(i));
            point->SetPositionLocal(local_points[i]);
        }
    }

    void Spline::ResampleControlPoints(float spacing_meters)
    {
        if (!m_entity_ptr || IsAttached() || spacing_meters <= 0.0f)
        {
            return;
        }

        const vector<Vector3> points = GetControlPointsLocal();
        if (points.size() < 2)
        {
            return;
        }

        // dense polyline of the current curve with cumulative arc length
        const uint32_t span_count    = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
        const uint32_t total_samples = max(2u, span_count * 16u);
        vector<Vector3> dense(total_samples + 1);
        vector<float> arc(total_samples + 1, 0.0f);
        for (uint32_t i = 0; i <= total_samples; i++)
        {
            const float t = static_cast<float>(i) / static_cast<float>(total_samples);
            dense[i]      = EvaluatePoint(points, t);
            if (i > 0)
            {
                arc[i] = arc[i - 1] + Vector3::Distance(dense[i - 1], dense[i]);
            }
        }

        const float length = arc.back();
        if (length <= spacing_meters * 0.5f)
        {
            return;
        }

        // whole number of equal spans so the ends stay where they are
        const uint32_t new_span_count = max(m_closed_loop ? 3u : 1u, static_cast<uint32_t>(roundf(length / spacing_meters)));
        const uint32_t new_point_count = m_closed_loop ? new_span_count : new_span_count + 1;
        const float step               = length / static_cast<float>(new_span_count);

        vector<Vector3> resampled;
        resampled.reserve(new_point_count);
        size_t cursor = 0;
        for (uint32_t k = 0; k < new_point_count; k++)
        {
            const float target = min(step * static_cast<float>(k), length);
            while (cursor + 1 < arc.size() && arc[cursor + 1] < target)
            {
                cursor++;
            }

            Vector3 position = dense[cursor];
            if (cursor + 1 < arc.size())
            {
                const float segment = arc[cursor + 1] - arc[cursor];
                const float blend   = segment > 1e-6f ? (target - arc[cursor]) / segment : 0.0f;
                position            = dense[cursor] + (dense[cursor + 1] - dense[cursor]) * blend;
            }
            resampled.push_back(position);
        }

        // the original end points are exact, the dense walk only approximates them
        resampled.front() = points.front();
        if (!m_closed_loop)
        {
            resampled.back() = points.back();
        }

        set_control_points_local(m_entity_ptr, resampled);
    }

    // douglas peucker, marks the points that survive
    static void simplify_polyline(const vector<Vector3>& points, size_t first, size_t last, float tolerance, vector<bool>& keep)
    {
        if (last <= first + 1)
        {
            return;
        }

        const Vector3 a       = points[first];
        const Vector3 b       = points[last];
        const Vector3 ab      = b - a;
        const float ab_length = ab.LengthSquared();

        float max_distance = -1.0f;
        size_t max_index   = first;
        for (size_t i = first + 1; i < last; i++)
        {
            const Vector3 ap = points[i] - a;
            float distance   = 0.0f;
            if (ab_length > 1e-8f)
            {
                const float projection = clamp(ap.Dot(ab) / ab_length, 0.0f, 1.0f);
                distance               = Vector3::Distance(points[i], a + ab * projection);
            }
            else
            {
                distance = ap.Length();
            }

            if (distance > max_distance)
            {
                max_distance = distance;
                max_index    = i;
            }
        }

        if (max_distance > tolerance)
        {
            keep[max_index] = true;
            simplify_polyline(points, first, max_index, tolerance, keep);
            simplify_polyline(points, max_index, last, tolerance, keep);
        }
    }

    void Spline::SimplifyControlPoints(float tolerance_meters)
    {
        if (!m_entity_ptr || IsAttached() || tolerance_meters < 0.0f)
        {
            return;
        }

        const vector<Vector3> points = GetControlPointsLocal();
        if (points.size() < 3)
        {
            return;
        }

        vector<bool> keep(points.size(), false);
        keep.front() = true;
        keep.back()  = true;

        if (m_closed_loop)
        {
            // split the loop at its farthest point from the start so both halves have a real chord
            size_t far_index = 0;
            float far_distance = -1.0f;
            for (size_t i = 1; i < points.size(); i++)
            {
                const float distance = Vector3::Distance(points[0], points[i]);
                if (distance > far_distance)
                {
                    far_distance = distance;
                    far_index    = i;
                }
            }
            keep[far_index] = true;
            simplify_polyline(points, 0, far_index, tolerance_meters, keep);
            simplify_polyline(points, far_index, points.size() - 1, tolerance_meters, keep);
        }
        else
        {
            simplify_polyline(points, 0, points.size() - 1, tolerance_meters, keep);
        }

        vector<Vector3> simplified;
        simplified.reserve(points.size());
        for (size_t i = 0; i < points.size(); i++)
        {
            if (keep[i])
            {
                simplified.push_back(points[i]);
            }
        }

        if (simplified.size() == points.size())
        {
            return;
        }

        set_control_points_local(m_entity_ptr, simplified);
    }

    void Spline::GenerateRoadMesh()
    {
        // build the dense list of frames either from own control points or from the source spline
        vector<SplineFrame> frames = SampleFrames(m_resolution);
        if (frames.size() < 2)
        {
            SP_LOG_WARNING("need at least 2 sampled frames to generate a mesh");
            return;
        }

        // preserve the user-assigned material before clearing the old mesh
        if (m_saved_material_name.empty() && m_entity_ptr)
        {
            if (Render* render = m_entity_ptr->GetComponent<Render>())
            {
                if (Material* material = render->GetMaterial())
                {
                    m_saved_material_name = material->GetObjectName();
                }
            }
        }

        // clean up any previous mesh
        ClearRoadMesh();

        // resolve the profile and extrude it along the spline
        vector<Vector2> profile_points = GetProfilePoints();
        bool close_profile             = IsProfileClosed();
        GenerateMesh(frames, profile_points, close_profile);

        CacheCarveSamples(frames);

        const uint64_t id = m_entity_ptr ? m_entity_ptr->GetObjectId() : 0;
        if (id != 0)
        {
            m_carve_entity_id = id;
        }

        if (Terrain* terrain = Terrain::FindActive())
        {
            terrain->MarkSplinePropCarvesDirty(id);
            terrain->MarkSplineHeightCarvesDirty(id);
        }
    }

    bool Spline::CarvesTerrain() const
    {
        return m_carve_terrain
            && m_mesh_enabled
            && m_conform_to_terrain
            && m_profile == SplineProfile::Road;
    }

    void Spline::CacheCarveSamples(const vector<SplineFrame>& frames)
    {
        m_carve_samples.clear();

        if (!CarvesTerrain() || frames.size() < 2 || !m_entity_ptr)
        {
            return;
        }

        const Matrix world_matrix = m_entity_ptr->GetMatrix();
        const bool width_varies   = (m_road_width_end != m_road_width);

        m_carve_samples.reserve(frames.size());
        for (const SplineFrame& frame : frames)
        {
            const float width = width_varies
                                ? (m_road_width + (m_road_width_end - m_road_width) * frame.t)
                                : m_road_width;

            SplineCarveSample sample;
            sample.position   = world_matrix * frame.position;
            sample.half_width = width * 0.5f + (m_sidewalk_enabled ? m_sidewalk_width : 0.0f);
            m_carve_samples.push_back(sample);
        }
    }

    void Spline::ClearRoadMesh()
    {
        if (m_mesh)
        {
            // preserve the current material so it can be restored on next regeneration
            if (m_saved_material_name.empty() && m_entity_ptr)
            {
                if (Render* render = m_entity_ptr->GetComponent<Render>())
                {
                    if (Material* material = render->GetMaterial())
                    {
                        m_saved_material_name = material->GetObjectName();
                    }
                }
            }

            // remove the render and physics components to avoid dangling mesh pointers
            if (m_entity_ptr)
            {
                m_entity_ptr->RemoveComponent<Physics>();
                m_entity_ptr->RemoveComponent<Render>();
            }

            m_mesh.reset();
        }

        if (!m_mesh_enabled)
        {
            m_carve_samples.clear();

            if (Terrain* terrain = Terrain::FindActive())
            {
                const uint64_t id = m_entity_ptr ? m_entity_ptr->GetObjectId() : 0;
                terrain->MarkSplinePropCarvesDirty(id);
                terrain->MarkSplineHeightCarvesDirty(id);
            }
        }
    }

    void Spline::SpawnInstances()
    {
        if (!m_entity_ptr)
        {
            return;
        }

        // clear any existing instances first
        ClearInstances();

        // sample dense frames in this entity local space (works for standalone and attached splines)
        vector<SplineFrame> frames = SampleFrames(m_resolution * 4);
        if (frames.size() < 2)
        {
            SP_LOG_WARNING("need at least 2 sampled frames to spawn instances");
            return;
        }

        float spline_length = frames.back().distance;
        if (spline_length < m_instance_spacing)
        {
            SP_LOG_WARNING("spline is shorter than instance spacing");
            return;
        }

        // resolve template entity (optional)
        Entity* template_entity = nullptr;
        if (m_instance_template_id != 0)
        {
            template_entity = World::GetEntityById(m_instance_template_id);
            if (template_entity == m_entity_ptr)
            {
                SP_LOG_WARNING("instance template cannot be the spline entity itself");
                template_entity = nullptr;
            }
        }

        // sides: -1 = left, +1 = right (along the spline's right vector)
        // lateral_offset = 0 + mirror = false keeps the legacy behavior of one centerline instance
        vector<int> sides;
        if (m_instance_mirror)
        {
            sides.push_back(+1);
            sides.push_back(-1);
        }
        else
        {
            sides.push_back(+1);
        }

        const Matrix instance_world_matrix   = m_entity_ptr->GetMatrix();
        const Matrix instance_inverse_matrix = instance_world_matrix.Inverted();
        GroundQuery instance_ground          = make_ground_query(m_entity_ptr);

        float next_spawn_distance = 0.0f;
        uint32_t spawned          = 0;

        for (uint32_t i = 0; i < frames.size(); i++)
        {
            const SplineFrame& frame = frames[i];

            if (frame.distance < next_spawn_distance)
            {
                continue;
            }

            Vector3 position = frame.position;
            Vector3 tangent  = frame.tangent;

            // horizontal-only right vector keeps instances upright on tilted segments
            Vector3 horiz_tangent = Vector3(tangent.x, 0.0f, tangent.z);
            if (horiz_tangent.LengthSquared() < 1e-6f)
            {
                horiz_tangent = tangent;
            }
            horiz_tangent.Normalize();
            Vector3 right = horiz_tangent.Cross(Vector3::Up);
            right.Normalize();

            for (int side : sides)
            {
                Entity* instance = nullptr;
                if (template_entity)
                {
                    instance = template_entity->Clone();
                }
                else
                {
                    instance = World::CreateEntity();
                    Render* render = instance->AddComponent<Render>();
                    render->SetMesh(MeshType::Cylinder);
                    render->SetDefaultMaterial();
                }

                instance->SetObjectName(prefix_instance + to_string(spawned));
                instance->SetParent(m_entity_ptr);
                instance->SetTransient(true);

                // base position + lateral offset + optional random jitter
                Vector3 final_position = position + right * (m_instance_lateral_offset * static_cast<float>(side));
                if (m_instance_random_offset > 0.0f)
                {
                    float jitter   = math::random<float>(-m_instance_random_offset, m_instance_random_offset);
                    final_position = final_position + right * jitter;
                }

                instance->SetPositionLocal(final_position);

                // rotation: face inward overrides align-to-spline; optional random yaw on top
                Quaternion rotation = Quaternion::Identity;
                if (m_instance_face_inward)
                {
                    // for attached fences face the source centerline, otherwise mirror around own tangent
                    Vector3 face_dir = right * static_cast<float>(-side);
                    if (IsAttached())
                    {
                        if (m_attach_mode == SplineAttachMode::LeftEdge || m_attach_mode == SplineAttachMode::LeftOuter)
                        {
                            face_dir = right;
                        }
                        else if (m_attach_mode == SplineAttachMode::RightEdge || m_attach_mode == SplineAttachMode::RightOuter)
                        {
                            face_dir = right * -1.0f;
                        }
                    }
                    if (face_dir.LengthSquared() > 0.0f)
                    {
                        rotation = Quaternion::FromLookRotation(face_dir, Vector3::Up);
                    }
                }
                else if (m_align_instances_to_spline)
                {
                    rotation = Quaternion::FromLookRotation(tangent, Vector3::Up);
                }
                if (m_instance_random_yaw > 0.0f)
                {
                    float yaw = math::random<float>(-m_instance_random_yaw, m_instance_random_yaw);
                    rotation  = rotation * Quaternion::FromAxisAngle(Vector3::Up, yaw * math::deg_to_rad);
                }
                instance->SetRotationLocal(rotation);

                // random scale
                if (m_instance_random_scale_min != 1.0f || m_instance_random_scale_max != 1.0f)
                {
                    float scale = math::random<float>(m_instance_random_scale_min, m_instance_random_scale_max);
                    instance->SetScaleLocal(Vector3(scale, scale, scale));
                }

                // the lateral offset moves the instance off the centerline, so it needs its own ground
                // sample, and it has to rest on its base rather than on whatever the pivot happens to be
                if (m_conform_to_terrain)
                {
                    Vector3 world_position = instance_world_matrix * final_position;
                    float ground_height    = 0.0f;
                    if (sample_ground_height(
                        world_position.x,
                        world_position.z,
                        instance_ground,
                        ground_height
                    ))
                    {
                        world_position.y = ground_height + applied_terrain_offset(m_terrain_offset) + pivot_to_bottom(instance);
                        final_position   = instance_inverse_matrix * world_position;
                        instance->SetPositionLocal(final_position);
                    }
                }

                spawned++;
            }

            next_spawn_distance += m_instance_spacing;
        }

        SP_LOG_INFO("spawned %u instances along spline (%.1f m, spacing %.1f m)", spawned, spline_length, m_instance_spacing);
    }

    bool Spline::HasSpawnedInstances() const
    {
        if (!m_entity_ptr)
        {
            return false;
        }

        const uint32_t child_count = m_entity_ptr->GetChildrenCount();
        for (uint32_t i = 0; i < child_count; i++)
        {
            if (Entity* child = m_entity_ptr->GetChildByIndex(i))
            {
                if (child->GetObjectName().find(prefix_instance) == 0)
                {
                    return true;
                }
            }
        }

        return false;
    }

    void Spline::ClearInstances()
    {
        if (!m_entity_ptr)
        {
            return;
        }

        // collect instance children (iterate in reverse to safely remove)
        vector<Entity*> instances_to_remove;
        uint32_t child_count = m_entity_ptr->GetChildrenCount();
        for (uint32_t i = 0; i < child_count; i++)
        {
            if (Entity* child = m_entity_ptr->GetChildByIndex(i))
            {
                if (child->GetObjectName().find(prefix_instance) == 0)
                {
                    instances_to_remove.push_back(child);
                }
            }
        }

        for (Entity* instance : instances_to_remove)
        {
            World::RemoveEntity(instance);
        }
    }

    vector<Vector3> Spline::GetControlPoints() const
    {
        vector<Vector3> points;

        if (!m_entity_ptr)
        {
            return points;
        }

        uint32_t child_count = m_entity_ptr->GetChildrenCount();
        points.reserve(child_count);

        for (uint32_t i = 0; i < child_count; i++)
        {
            if (Entity* child = m_entity_ptr->GetChildByIndex(i))
            {
                // only include control point children, not instances
                if (child->GetObjectName().find(prefix_control_point) == 0)
                {
                    points.push_back(child->GetPosition());
                }
            }
        }

        return points;
    }

    Vector3 Spline::GetEditorHandlePosition(Entity* entity)
    {
        if (!entity)
        {
            return Vector3::Zero;
        }

        Entity* parent = entity->GetParent();
        Spline* spline = parent ? parent->GetComponent<Spline>() : nullptr;
        if (!spline || !spline->GetConformToTerrain() || spline->IsAttached())
        {
            return entity->GetPosition();
        }

        if (entity->GetObjectName().find(prefix_control_point) != 0)
        {
            return entity->GetPosition();
        }

        vector<Entity*> points;
        const uint32_t child_count = parent->GetChildrenCount();
        points.reserve(child_count);
        size_t index = 0;
        bool found = false;
        for (uint32_t i = 0; i < child_count; i++)
        {
            Entity* child = parent->GetChildByIndex(i);
            if (!child || child->GetObjectName().find(prefix_control_point) != 0)
            {
                continue;
            }

            if (child == entity)
            {
                index = points.size();
                found = true;
            }
            points.push_back(child);
        }

        if (!found || points.empty())
        {
            return entity->GetPosition();
        }

        Vector3 world = entity->GetPosition();
        Vector3 right = Vector3::Right;
        const Vector3 prev = (index > 0) ? points[index - 1]->GetPosition() : world;
        const Vector3 next = (index + 1 < points.size()) ? points[index + 1]->GetPosition() : world;
        Vector3 tangent(next.x - prev.x, 0.0f, next.z - prev.z);
        if (tangent.LengthSquared() > 1e-8f)
        {
            tangent.Normalize();
            right = Vector3(-tangent.z, 0.0f, tangent.x);
        }

        world.y = snapped_control_point_y(
            world,
            right,
            spline_half_extent(spline),
            spline->GetTerrainOffset(),
            make_ground_query(parent)
        );

        return world;
    }

    void Spline::RefreshHandlePositions()
    {
        m_handle_positions.clear();
        if (!m_entity_ptr)
        {
            return;
        }

        vector<Entity*> points;
        const uint32_t child_count = m_entity_ptr->GetChildrenCount();
        points.reserve(child_count);
        for (uint32_t i = 0; i < child_count; i++)
        {
            Entity* child = m_entity_ptr->GetChildByIndex(i);
            if (child && child->GetObjectName().find(prefix_control_point) == 0)
            {
                points.push_back(child);
            }
        }

        m_handle_positions.reserve(points.size());
        const bool snap = m_conform_to_terrain && !IsAttached();
        if (!snap)
        {
            for (Entity* point : points)
            {
                m_handle_positions.push_back(point->GetPosition());
            }
            return;
        }

        const GroundQuery query = make_ground_query(m_entity_ptr);
        const float half_extent = spline_half_extent(this);
        for (size_t index = 0; index < points.size(); index++)
        {
            Vector3 world      = points[index]->GetPosition();
            const Vector3 prev = (index > 0) ? points[index - 1]->GetPosition() : world;
            const Vector3 next = (index + 1 < points.size()) ? points[index + 1]->GetPosition() : world;

            Vector3 right = Vector3::Right;
            Vector3 tangent(next.x - prev.x, 0.0f, next.z - prev.z);
            if (tangent.LengthSquared() > 1e-8f)
            {
                tangent.Normalize();
                right = Vector3(-tangent.z, 0.0f, tangent.x);
            }

            world.y = snapped_control_point_y(world, right, half_extent, m_terrain_offset, query);
            m_handle_positions.push_back(world);
        }
    }

    vector<Vector3> Spline::GetControlPointsLocal() const
    {
        vector<Vector3> points;

        if (!m_entity_ptr)
        {
            return points;
        }

        uint32_t child_count = m_entity_ptr->GetChildrenCount();
        points.reserve(child_count);

        for (uint32_t i = 0; i < child_count; i++)
        {
            if (Entity* child = m_entity_ptr->GetChildByIndex(i))
            {
                // only include control point children, not instances
                if (child->GetObjectName().find(prefix_control_point) == 0)
                {
                    points.push_back(child->GetPositionLocal());
                }
            }
        }

        return points;
    }

    vector<Vector2> Spline::GetProfilePoints() const
    {
        return GetProfilePointsForWidth(m_road_width);
    }

    vector<Vector2> Spline::GetProfilePointsForWidth(float width) const
    {
        vector<Vector2> profile;
        float half_width     = width * 0.5f;
        float half_thickness = m_thickness * 0.5f;

        switch (m_profile)
        {
        case SplineProfile::Road:
            if (m_sidewalk_enabled)
            {
                // left sidewalk outer -> curb drop -> road -> curb rise -> right sidewalk outer
                float outer_left  = -(half_width + m_sidewalk_width);
                float outer_right =  (half_width + m_sidewalk_width);
                profile.emplace_back(outer_left,   m_curb_height);
                profile.emplace_back(-half_width,  m_curb_height);
                profile.emplace_back(-half_width,  0.0f);
                profile.emplace_back( half_width,  0.0f);
                profile.emplace_back( half_width,  m_curb_height);
                profile.emplace_back(outer_right,  m_curb_height);
            }
            else
            {
                profile.emplace_back(-half_width, 0.0f);
                profile.emplace_back( half_width, 0.0f);
            }
            break;

        case SplineProfile::Wall:
            profile.emplace_back(-half_thickness, 0.0f);
            profile.emplace_back(-half_thickness, m_height);
            profile.emplace_back( half_thickness, m_height);
            profile.emplace_back( half_thickness, 0.0f);
            break;

        case SplineProfile::Tube:
        {
            uint32_t sides = max(3u, m_tube_sides);
            for (uint32_t i = 0; i < sides; i++)
            {
                float angle = (static_cast<float>(i) / static_cast<float>(sides)) * 2.0f * pi;
                float x     = cosf(angle) * half_width;
                float y     = sinf(angle) * half_width;
                profile.emplace_back(x, y);
            }
            break;
        }

        case SplineProfile::Fence:
            profile.emplace_back(-half_thickness, 0.0f);
            profile.emplace_back(-half_thickness, m_height);
            profile.emplace_back( half_thickness, m_height);
            profile.emplace_back( half_thickness, 0.0f);
            break;

        case SplineProfile::Channel:
            profile.emplace_back(-half_width, m_height);
            profile.emplace_back(-half_width, 0.0f);
            profile.emplace_back( half_width, 0.0f);
            profile.emplace_back( half_width, m_height);
            break;

        default:
            profile.emplace_back(-half_width, 0.0f);
            profile.emplace_back( half_width, 0.0f);
            break;
        }

        return profile;
    }

    bool Spline::UsesEmbankment() const
    {
        return m_embankment_enabled
            && m_conform_to_terrain
            && !IsAttached()
            && m_profile == SplineProfile::Road
            && m_embankment_max_height > 0.0f;
    }

    vector<Vector2> Spline::GetProfileForFrame(float width, float fill_left, float fill_right) const
    {
        vector<Vector2> profile = GetProfilePointsForWidth(width);
        if (!UsesEmbankment() || profile.size() < 2)
        {
            return profile;
        }

        // never let the skirt collapse onto the deck edge, that would emit degenerate triangles
        const float min_depth = applied_terrain_offset(m_terrain_offset);
        const float max_depth = max(m_embankment_max_height, min_depth);

        // past a certain height an earth bank stops being believable, the deck becomes a viaduct edge
        const float depth_left  = clamp(fill_left,  min_depth, max_depth);
        const float depth_right = clamp(fill_right, min_depth, max_depth);

        const float angle    = clamp(m_embankment_slope_degrees, 5.0f, 89.0f) * deg_to_rad;
        const float run_rate = 1.0f / tanf(angle);

        const Vector2 outer_left  = profile.front();
        const Vector2 outer_right = profile.back();

        profile.insert(profile.begin(), Vector2(outer_left.x - depth_left * run_rate, -depth_left));
        profile.emplace_back(outer_right.x + depth_right * run_rate, -depth_right);

        return profile;
    }

    bool Spline::IsProfileClosed() const
    {
        return m_profile == SplineProfile::Tube;
    }

    vector<SplineFrame> Spline::SampleFrames(uint32_t samples_per_span) const
    {
        vector<SplineFrame> frames;

        // attached path: sample the source spline and offset by the chosen edge
        if (IsAttached() && m_source_spline_entity)
        {
            Spline* source = m_source_spline_entity->GetComponent<Spline>();
            if (!source || source->GetControlPointCount() < 2)
            {
                return frames;
            }

            // determine the side sign for the lateral offset
            float side = 0.0f;
            switch (m_attach_mode)
            {
                case SplineAttachMode::LeftEdge:
                case SplineAttachMode::LeftOuter:  side = -1.0f; break;
                case SplineAttachMode::RightEdge:
                case SplineAttachMode::RightOuter: side = +1.0f; break;
                default:                           side =  0.0f; break;
            }

            // does the source profile expose a sidewalk on its outer edge
            bool source_has_sidewalk = source->GetSidewalkEnabled() && source->GetProfile() == SplineProfile::Road;

            // use source resolution unless the user pinned a sample count
            uint32_t source_point_count = source->GetControlPointCount();
            uint32_t source_span_count  = source->GetClosedLoop() ? source_point_count : (source_point_count - 1);
            uint32_t total_samples      = (m_attach_sample_count > 0)
                                          ? m_attach_sample_count
                                          : source_span_count * samples_per_span;
            if (total_samples < 1)
            {
                total_samples = 1;
            }

            Matrix world_inv = m_entity_ptr ? m_entity_ptr->GetMatrix().Inverted() : Matrix::Identity;
            GroundQuery attach_ground = make_ground_query(m_entity_ptr);

            float accumulated_distance = 0.0f;
            Vector3 prev_local_position;

            frames.reserve(total_samples + 1);

            for (uint32_t i = 0; i <= total_samples; i++)
            {
                float t = static_cast<float>(i) / static_cast<float>(total_samples);

                Vector3 world_pos = source->GetPoint(t);
                Vector3 world_tan = source->GetTangent(t);
                if (world_tan.LengthSquared() < 1e-6f)
                {
                    world_tan = Vector3::Forward;
                }
                world_tan.Normalize();

                Vector3 world_up = Vector3::Up;
                if (abs(world_tan.Dot(Vector3::Up)) > 0.99f)
                {
                    world_up = Vector3::Forward;
                }

                Vector3 world_right = world_tan.Cross(world_up);
                world_right.Normalize();
                world_up = world_right.Cross(world_tan);
                world_up.Normalize();

                // edge offset based on the source road width (interpolated start to end)
                float source_half_width = (source->GetRoadWidth() + (source->GetRoadWidthEnd() - source->GetRoadWidth()) * t) * 0.5f;
                float edge_offset       = 0.0f;
                if (m_attach_mode == SplineAttachMode::LeftEdge || m_attach_mode == SplineAttachMode::RightEdge)
                {
                    edge_offset = source_half_width;
                }
                else if (m_attach_mode == SplineAttachMode::LeftOuter || m_attach_mode == SplineAttachMode::RightOuter)
                {
                    edge_offset = source_half_width + (source_has_sidewalk ? source->GetSidewalkWidth() : 0.0f);
                }

                // outward push for non centerline modes, plain right shift for centerline
                float lateral = (m_attach_mode == SplineAttachMode::Centerline)
                                ? m_attach_lateral_offset
                                : side * (edge_offset + m_attach_lateral_offset);

                Vector3 offset_world = world_pos + world_right * lateral + Vector3::Up * m_attach_vertical_offset;

                // the source path is a curve through its control points, it does not know about the ground
                if (m_conform_to_terrain)
                {
                    float ground_height = 0.0f;
                    if (sample_ground_height(
                        offset_world.x,
                        offset_world.z,
                        attach_ground,
                        ground_height
                    ))
                    {
                        offset_world.y = ground_height + applied_terrain_offset(m_terrain_offset) + m_attach_vertical_offset;
                    }
                }

                // transform position and direction vectors into this entity local space
                Vector3 local_pos = world_inv * offset_world;
                Vector3 local_origin = world_inv * Vector3::Zero;
                Vector3 local_tan   = (world_inv * world_tan)   - local_origin;
                Vector3 local_right = (world_inv * world_right) - local_origin;
                Vector3 local_up    = (world_inv * world_up)    - local_origin;

                if (local_tan.LengthSquared() < 1e-6f)
                {
                    local_tan   = Vector3::Forward;
                }
                if (local_right.LengthSquared() < 1e-6f)
                {
                    local_right = Vector3::Right;
                }
                if (local_up.LengthSquared() < 1e-6f)
                {
                    local_up    = Vector3::Up;
                }
                local_tan.Normalize();
                local_right.Normalize();
                local_up.Normalize();

                if (i > 0)
                {
                    accumulated_distance += local_pos.Distance(prev_local_position);
                }
                prev_local_position = local_pos;

                SplineFrame frame;
                frame.position = local_pos;
                frame.tangent  = local_tan;
                frame.right    = local_right;
                frame.up       = local_up;
                frame.t        = t;
                frame.distance = accumulated_distance;
                frames.push_back(frame);
            }

            return frames;
        }

        // standalone path: walk own control points
        vector<Vector3> spline_points = GetControlPointsLocal();
        if (spline_points.size() < 2)
        {
            return frames;
        }

        uint32_t span_count    = m_closed_loop ? static_cast<uint32_t>(spline_points.size()) : static_cast<uint32_t>(spline_points.size()) - 1;
        uint32_t total_samples = span_count * samples_per_span;
        if (total_samples < 1)
        {
            total_samples = 1;
        }

        Matrix world_matrix   = m_entity_ptr ? m_entity_ptr->GetMatrix() : Matrix::Identity;
        Matrix inverse_matrix = world_matrix.Inverted();

        // direction transforms, the matrices carry translation so the origin has to be subtracted
        const Vector3 world_origin   = world_matrix * Vector3::Zero;
        const Vector3 inverse_origin = inverse_matrix * Vector3::Zero;
        GroundQuery ground_query     = make_ground_query(m_entity_ptr);

        // parametric positions to sample, uniform to start with
        vector<float> sample_ts;
        sample_ts.reserve(total_samples + 1);
        for (uint32_t i = 0; i <= total_samples; i++)
        {
            sample_ts.push_back(static_cast<float>(i) / static_cast<float>(total_samples));
        }

        // how far a chord between two samples is allowed to drift from the ground, the surface
        // is lifted by the same amount later so the remaining sag cannot break through
        // the surface is linear between samples, so a straight chord over a ridge ends up buried,
        // keep splitting segments whose midpoint drifts from the ground until they all fit
        if (m_conform_to_terrain)
        {
            const float tolerance      = conform_sag;
            const uint32_t pass_count  = 6;
            const size_t sample_budget = 8192;

            auto ground_at_t = [this, &spline_points, &world_matrix, &ground_query](float t, float& height_out)
            {
                const Vector3 world_pos = world_matrix * EvaluatePoint(spline_points, t);
                return sample_ground_height(world_pos.x, world_pos.z, ground_query, height_out);
            };

            for (uint32_t pass = 0; pass < pass_count && sample_ts.size() < sample_budget; pass++)
            {
                vector<float> refined;
                refined.reserve(sample_ts.size() * 2);

                bool inserted = false;
                for (size_t i = 0; i + 1 < sample_ts.size(); i++)
                {
                    refined.push_back(sample_ts[i]);

                    const float t_mid = (sample_ts[i] + sample_ts[i + 1]) * 0.5f;

                    float height_a   = 0.0f;
                    float height_b   = 0.0f;
                    float height_mid = 0.0f;
                    if (!ground_at_t(sample_ts[i], height_a) ||
                        !ground_at_t(sample_ts[i + 1], height_b) ||
                        !ground_at_t(t_mid, height_mid))
                    {
                        continue;
                    }

                    if (fabsf(height_mid - (height_a + height_b) * 0.5f) > tolerance)
                    {
                        refined.push_back(t_mid);
                        inserted = true;
                    }
                }

                refined.push_back(sample_ts.back());
                sample_ts = move(refined);

                if (!inserted)
                {
                    break;
                }
            }
        }

        // half of the widest part of the cross section, the edges have to clear the ground too
        const float half_extent = spline_half_extent(this);

        const size_t sample_count = sample_ts.size();
        vector<Vector3> positions(sample_count);
        vector<Vector3> rights(sample_count);

        // world space bookkeeping used by the grade limiter and the embankment skirt
        vector<Vector3> world_positions(sample_count);
        vector<float> ground_heights(sample_count, 0.0f);
        vector<float> edge_ground_left(sample_count, 0.0f);
        vector<float> edge_ground_right(sample_count, 0.0f);
        bool ground_valid = m_conform_to_terrain;

        // first pass, place every sample, keep the deck level and lift it off the ground
        for (size_t i = 0; i < sample_count; i++)
        {
            const float t = sample_ts[i];

            Vector3 position = EvaluatePoint(spline_points, t);
            Vector3 tangent  = EvaluateTangent(spline_points, t);
            if (tangent.LengthSquared() < 1e-12f)
            {
                tangent = Vector3::Forward;
            }
            tangent.Normalize();

            Vector3 up = Vector3::Up;
            if (abs(tangent.Dot(Vector3::Up)) > 0.99f)
            {
                up = Vector3::Forward;
            }

            Vector3 right = tangent.Cross(up);
            right.Normalize();

            if (m_conform_to_terrain)
            {
                Vector3 world_pos   = world_matrix * position;
                Vector3 world_right = (world_matrix * right) - world_origin;
                world_right.y       = 0.0f;
                if (world_right.LengthSquared() < 1e-6f)
                {
                    world_right = Vector3::Right;
                }
                world_right.Normalize();

                // the edges get their own ground so the skirt lands on the real surface rather than
                // on the highest point across the cross-section that the deck was lifted to
                edge_ground_left[i]  = world_pos.y;
                edge_ground_right[i] = world_pos.y;

                float base = 0.0f;
                if (sample_deck_height(
                    world_pos,
                    world_right,
                    half_extent,
                    ground_query,
                    base,
                    &edge_ground_left[i],
                    &edge_ground_right[i]
                ))
                {
                    // the chord between two samples may sag by up to the refinement tolerance
                    world_pos.y = base + applied_terrain_offset(m_terrain_offset) + conform_sag;

                    position = inverse_matrix * world_pos;
                    right    = (inverse_matrix * world_right) - inverse_origin;
                    if (right.LengthSquared() < 1e-6f)
                    {
                        right = tangent.Cross(Vector3::Up);
                    }
                    right.Normalize();

                    ground_heights[i] = base;
                }
                else
                {
                    ground_valid = false;
                }

                world_positions[i] = world_pos;
            }

            positions[i] = position;
            rights[i]    = right;
        }

        // rebuild the elevation profile so a car can actually drive it
        if (ground_valid && m_grade_limit_enabled && sample_count >= 3)
        {
            // grade is rise over horizontal run, so the spans ignore height
            vector<float> spans(sample_count - 1);
            for (size_t i = 0; i + 1 < sample_count; i++)
            {
                const Vector3 delta = world_positions[i + 1] - world_positions[i];
                spans[i]            = max(sqrtf(delta.x * delta.x + delta.z * delta.z), 1e-3f);
            }

            vector<float> deck_heights(sample_count);
            for (size_t i = 0; i < sample_count; i++)
            {
                deck_heights[i] = world_positions[i].y;
            }

            const float slope = tanf(clamp(m_max_grade_degrees, 0.5f, 80.0f) * deg_to_rad);
            apply_grade_limit(
                deck_heights,
                ground_heights,
                spans,
                slope,
                max(m_max_cut, 0.0f),
                m_grade_smoothing,
                max(m_smoothing_length, 0.0f),
                m_closed_loop
            );

            for (size_t i = 0; i < sample_count; i++)
            {
                world_positions[i].y = deck_heights[i];
                positions[i]         = inverse_matrix * world_positions[i];
            }
        }

        // second pass, tangents come from the conformed positions so the frames follow the real slope
        frames.reserve(sample_count);

        float accumulated_distance = 0.0f;

        for (size_t i = 0; i < sample_count; i++)
        {
            const size_t i_prev = (i == 0) ? 0 : i - 1;
            const size_t i_next = (i + 1 < sample_count) ? i + 1 : i;

            Vector3 tangent = positions[i_next] - positions[i_prev];
            if (tangent.LengthSquared() < 1e-12f)
            {
                tangent = EvaluateTangent(spline_points, sample_ts[i]);
            }
            if (tangent.LengthSquared() < 1e-12f)
            {
                tangent = Vector3::Forward;
            }
            tangent.Normalize();

            // keep the deck level, only pitch along the path
            Vector3 world_right = (world_matrix * rights[i]) - world_origin;
            world_right.y = 0.0f;
            if (world_right.LengthSquared() < 1e-6f)
            {
                Vector3 world_tan = (world_matrix * tangent) - world_origin;
                world_right = Vector3(world_tan.z, 0.0f, -world_tan.x);
            }
            if (world_right.LengthSquared() < 1e-6f)
            {
                world_right = Vector3::Right;
            }
            world_right.Normalize();

            Vector3 right = (inverse_matrix * world_right) - inverse_origin;
            right.Normalize();

            Vector3 up = right.Cross(tangent);
            if (up.LengthSquared() < 1e-6f)
            {
                up = Vector3::Up;
            }
            up.Normalize();

            if (i > 0)
            {
                accumulated_distance += positions[i].Distance(positions[i - 1]);
            }

            SplineFrame frame;
            frame.position = positions[i];
            frame.tangent  = tangent;
            frame.right    = right;
            frame.up       = up;
            frame.t        = sample_ts[i];
            frame.distance = accumulated_distance;

            // the skirt always reaches down to the untouched ground, the terrain carve fills most of
            // that volume and swallows it, what stays visible is wherever the terrain grid was too
            // coarse to follow the road, which is exactly where a gap would otherwise show
            if (m_conform_to_terrain)
            {
                frame.fill_left  = max(world_positions[i].y - edge_ground_left[i], 0.0f);
                frame.fill_right = max(world_positions[i].y - edge_ground_right[i], 0.0f);
            }

            frames.push_back(frame);
        }

        return frames;
    }

    void Spline::SetSourceSplineEntityId(uint64_t id)
    {
        if (m_source_spline_entity_id == id)
        {
            return;
        }

        m_source_spline_entity_id = id;
        m_source_spline_entity    = nullptr;
        ResolveSourceSplineEntity();
    }

    void Spline::ResolveSourceSplineEntity()
    {
        m_source_spline_entity = nullptr;
        if (m_source_spline_entity_id == 0)
        {
            return;
        }

        Entity* candidate = World::GetEntityById(m_source_spline_entity_id);
        if (candidate == m_entity_ptr)
        {
            return;
        }
        m_source_spline_entity = candidate;
    }

    uint64_t Spline::ComputeSourceHash() const
    {
        if (!m_source_spline_entity)
        {
            return 0;
        }

        Spline* source = m_source_spline_entity->GetComponent<Spline>();
        if (!source)
        {
            return 0;
        }

        // fnv-1a style hash mixing the source state that affects derived frames
        uint64_t hash = 1469598103934665603ULL;
        auto mix = [&](uint64_t v)
        {
            hash ^= v;
            hash *= 1099511628211ULL;
        };
        auto mix_f = [&](float f)
        {
            uint32_t bits;
            std::memcpy(&bits, &f, sizeof(bits));
            mix(static_cast<uint64_t>(bits));
        };

        vector<Vector3> points = source->GetControlPoints();
        mix(static_cast<uint64_t>(points.size()));
        for (const Vector3& p : points)
        {
            mix_f(p.x);
            mix_f(p.y);
            mix_f(p.z);
        }

        mix_f(source->GetRoadWidth());
        mix_f(source->GetRoadWidthEnd());
        mix_f(source->GetSidewalkWidth());
        mix(source->GetSidewalkEnabled() ? 1ULL : 0ULL);
        mix(source->GetClosedLoop()      ? 1ULL : 0ULL);
        mix(static_cast<uint64_t>(source->GetProfile()));
        mix(static_cast<uint64_t>(source->GetResolution()));

        return hash;
    }

    void Spline::GenerateMesh(const vector<SplineFrame>& frames, const vector<Vector2>& profile_points, bool close_profile)
    {
        if (frames.size() < 2 || profile_points.size() < 2)
        {
            return;
        }

        bool width_varies     = (m_road_width_end != m_road_width);
        const bool embankment = UsesEmbankment();

        uint32_t total_samples = static_cast<uint32_t>(frames.size()) - 1;
        uint32_t profile_count = static_cast<uint32_t>(profile_points.size()) + (embankment ? 2u : 0u);

        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        vertices.reserve(frames.size() * profile_count);

        for (uint32_t i = 0; i < frames.size(); i++)
        {
            const SplineFrame& frame = frames[i];

            // interpolate width if it varies along the spline, the skirt depth varies per frame
            float current_width = width_varies ? (m_road_width + (m_road_width_end - m_road_width) * frame.t) : m_road_width;
            vector<Vector2> cur_profile;
            if (embankment)
            {
                cur_profile = GetProfileForFrame(current_width, frame.fill_left, frame.fill_right);
            }
            else
            {
                cur_profile = width_varies ? GetProfilePointsForWidth(current_width) : profile_points;
            }
            uint32_t cur_profile_count = static_cast<uint32_t>(cur_profile.size());

            // recompute perimeter for the current cross-section
            float cur_perimeter = 0.0f;
            uint32_t cur_edge_count = close_profile ? cur_profile_count : cur_profile_count - 1;
            for (uint32_t j = 0; j < cur_edge_count; j++)
            {
                uint32_t j_next = (j + 1) % cur_profile_count;
                cur_perimeter += Vector2::Distance(cur_profile[j], cur_profile[j_next]);
            }
            if (cur_perimeter < 0.001f)
            {
                cur_perimeter = 1.0f;
            }

            float v = (frame.distance / m_road_width) * m_uv_tiling_v;

            float accumulated_profile_distance = 0.0f;
            for (uint32_t j = 0; j < cur_profile_count; j++)
            {
                Vector3 vertex_pos = frame.position + frame.right * cur_profile[j].x + frame.up * cur_profile[j].y;

                if (j > 0)
                {
                    accumulated_profile_distance += Vector2::Distance(cur_profile[j], cur_profile[j - 1]);
                }
                float u = (accumulated_profile_distance / cur_perimeter) * m_uv_tiling_u;

                Vector3 normal;
                if (cur_profile_count == 2)
                {
                    normal = frame.up;
                }
                else
                {
                    uint32_t j_prev = (j == 0) ? (close_profile ? cur_profile_count - 1 : 0) : j - 1;
                    uint32_t j_next = (j == cur_profile_count - 1) ? (close_profile ? 0 : cur_profile_count - 1) : j + 1;

                    Vector2 edge = cur_profile[j_next] - cur_profile[j_prev];

                    // open profiles (road/wall/fence/channel) need the opposite winding
                    // from closed profiles (tube) to keep normals facing outward/upward
                    Vector2 perp = close_profile ? Vector2(edge.y, -edge.x) : Vector2(-edge.y, edge.x);
                    float perp_len = sqrtf(perp.x * perp.x + perp.y * perp.y);
                    if (perp_len > 0.001f)
                    {
                        perp.x /= perp_len;
                        perp.y /= perp_len;
                    }

                    normal = frame.right * perp.x + frame.up * perp.y;
                    normal.Normalize();
                }

                vertices.emplace_back(vertex_pos, Vector2(u, v), normal, frame.tangent);
            }
        }

        // generate triangle indices connecting adjacent cross-sections
        uint32_t idx_edge_count = close_profile ? profile_count : profile_count - 1;

        indices.reserve(total_samples * idx_edge_count * 6);
        for (uint32_t i = 0; i < total_samples; i++)
        {
            for (uint32_t j = 0; j < idx_edge_count; j++)
            {
                uint32_t j_next = (j + 1) % profile_count;

                uint32_t bl = i * profile_count + j;
                uint32_t br = i * profile_count + j_next;
                uint32_t tl = (i + 1) * profile_count + j;
                uint32_t tr = (i + 1) * profile_count + j_next;

                indices.push_back(bl);
                indices.push_back(br);
                indices.push_back(tl);

                indices.push_back(br);
                indices.push_back(tr);
                indices.push_back(tl);
            }
        }

        float total_length = frames.back().distance;

        // create the mesh
        m_mesh = make_shared<Mesh>();
        m_mesh->SetObjectName("spline_mesh");
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessOptimize), false);
        m_mesh->SetFlag(static_cast<uint32_t>(MeshFlags::PostProcessNormalizeScale), false);
        m_mesh->AddGeometry(vertices, indices, false);
        m_mesh->CreateGpuBuffers();

        // attach to a render component on this entity
        Render* render = m_entity_ptr->GetComponent<Render>();
        if (!render)
        {
            render = m_entity_ptr->AddComponent<Render>();
        }
        render->SetMesh(m_mesh.get(), 0);

        // restore saved material if one was preserved from a previous load, otherwise use default
        if (!m_saved_material_name.empty())
        {
            shared_ptr<Material> material = ResourceCache::GetByName<Material>(m_saved_material_name);
            if (material)
            {
                render->SetMaterial(material);
            }
            else
            {
                render->SetDefaultMaterial();
            }
            m_saved_material_name.clear();
        }
        else if (!render->GetMaterial())
        {
            render->SetDefaultMaterial();
        }

        // disable face culling for profiles that are visible from both sides
        if (m_profile == SplineProfile::Wall || m_profile == SplineProfile::Fence || m_profile == SplineProfile::Tube)
        {
            if (Material* material = render->GetMaterial())
            {
                material->SetProperty(MaterialProperty::CullMode, static_cast<float>(RHI_CullMode::None));
            }
        }

        // attach a physics component so the mesh is collidable
        if (m_entity_ptr->GetComponent<Physics>())
        {
            m_entity_ptr->RemoveComponent<Physics>();
        }
        Physics* physics = m_entity_ptr->AddComponent<Physics>();
        physics->SetBodyType(BodyType::Mesh);

        SP_LOG_INFO("generated spline mesh: %u vertices, %u indices, %.1f m long",
            static_cast<uint32_t>(vertices.size()), static_cast<uint32_t>(indices.size()),
            total_length);
    }

    // knot intervals for the non uniform form, degenerate spans borrow from the middle one
    static void catmull_rom_knots(
        const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3,
        float alpha,
        float& dt0, float& dt1, float& dt2
    )
    {
        const float epsilon = 1e-4f;
        alpha               = clamp(alpha, 0.0f, 1.0f);

        dt0 = powf(Vector3::Distance(p0, p1), alpha);
        dt1 = powf(Vector3::Distance(p1, p2), alpha);
        dt2 = powf(Vector3::Distance(p2, p3), alpha);

        if (dt1 < epsilon)
        {
            dt1 = 1.0f;
        }
        if (dt0 < epsilon)
        {
            dt0 = dt1;
        }
        if (dt2 < epsilon)
        {
            dt2 = dt1;
        }
    }

    // hermite tangents at p1 and p2 scaled to the span, with equal knots this is the classic matrix form
    static void catmull_rom_hermite(
        const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3,
        float alpha,
        Vector3& m1, Vector3& m2
    )
    {
        float dt0 = 1.0f;
        float dt1 = 1.0f;
        float dt2 = 1.0f;
        catmull_rom_knots(p0, p1, p2, p3, alpha, dt0, dt1, dt2);

        m1 = ((p1 - p0) / dt0 - (p2 - p0) / (dt0 + dt1) + (p2 - p1) / dt1) * dt1;
        m2 = ((p2 - p1) / dt1 - (p3 - p1) / (dt1 + dt2) + (p3 - p2) / dt2) * dt1;
    }

    Vector3 Spline::CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t, float alpha)
    {
        Vector3 m1;
        Vector3 m2;
        catmull_rom_hermite(p0, p1, p2, p3, alpha, m1, m2);

        const float t2 = t * t;
        const float t3 = t2 * t;

        const Vector3 c2 = (p2 - p1) * 3.0f - m1 * 2.0f - m2;
        const Vector3 c3 = (p1 - p2) * 2.0f + m1 + m2;

        return p1 + m1 * t + c2 * t2 + c3 * t3;
    }

    Vector3 Spline::CatmullRomTangent(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t, float alpha)
    {
        Vector3 m1;
        Vector3 m2;
        catmull_rom_hermite(p0, p1, p2, p3, alpha, m1, m2);

        const Vector3 c2 = (p2 - p1) * 3.0f - m1 * 2.0f - m2;
        const Vector3 c3 = (p1 - p2) * 2.0f + m1 + m2;

        return m1 + c2 * (2.0f * t) + c3 * (3.0f * t * t);
    }

    void Spline::GetSpanIndices(uint32_t span_index, size_t point_count_in, int32_t& i0, int32_t& i1, int32_t& i2, int32_t& i3) const
    {
        const int32_t point_count = static_cast<int32_t>(point_count_in);
        i1 = static_cast<int32_t>(span_index);
        i2 = m_closed_loop ? (i1 + 1) % point_count : min(i1 + 1, point_count - 1);
        i0 = m_closed_loop ? (i1 - 1 + point_count) % point_count : max(i1 - 1, 0);
        i3 = m_closed_loop ? (i2 + 1) % point_count : min(i2 + 1, point_count - 1);
    }

    Vector3 Spline::EvaluatePoint(const vector<Vector3>& points, float t) const
    {
        if (points.empty())
        {
            return Vector3::Zero;
        }
        if (points.size() == 1)
        {
            return points[0];
        }

        uint32_t span_index = 0;
        float local_t       = 0.0f;
        MapToSpan(t, points, span_index, local_t);

        int32_t i0 = 0;
        int32_t i1 = 0;
        int32_t i2 = 0;
        int32_t i3 = 0;
        GetSpanIndices(span_index, points.size(), i0, i1, i2, i3);

        return CatmullRom(points[i0], points[i1], points[i2], points[i3], local_t, m_curve_alpha);
    }

    Vector3 Spline::EvaluateTangent(const vector<Vector3>& points, float t) const
    {
        if (points.size() < 2)
        {
            return Vector3::Forward;
        }

        uint32_t span_index = 0;
        float local_t       = 0.0f;
        MapToSpan(t, points, span_index, local_t);

        int32_t i0 = 0;
        int32_t i1 = 0;
        int32_t i2 = 0;
        int32_t i3 = 0;
        GetSpanIndices(span_index, points.size(), i0, i1, i2, i3);

        Vector3 tangent = CatmullRomTangent(points[i0], points[i1], points[i2], points[i3], local_t, m_curve_alpha);
        if (tangent.LengthSquared() < 1e-12f)
        {
            tangent = points[i2] - points[i1];
        }
        tangent.Normalize();
        return tangent;
    }

    void Spline::MapToSpan(float t, const vector<Vector3>& points, uint32_t& span_index, float& local_t) const
    {
        uint32_t span_count = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;

        // clamp t to [0, 1]
        t = max(0.0f, min(1.0f, t));

        // scale t to span range
        float scaled_t = t * static_cast<float>(span_count);
        span_index     = static_cast<uint32_t>(scaled_t);
        local_t        = scaled_t - static_cast<float>(span_index);

        // handle the edge case where t = 1.0
        if (span_index >= span_count)
        {
            span_index = span_count - 1;
            local_t    = 1.0f;
        }
    }
}
