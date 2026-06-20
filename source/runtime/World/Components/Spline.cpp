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
#include "../Entity.h"
#include "../../Rendering/Renderer.h"
#include "../../Resource/ResourceCache.h"
#include "../../Physics/PhysicsWorld.h"
SP_WARNINGS_OFF
#include "../../IO/pugixml.hpp"
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

        // don't call ClearInstances() here because during destruction the world's entity
        // list may contain dangling pointers (e.g. shutdown deletes entities in a loop),
        // and RemoveEntity -> AcquireChildren would iterate over freed memory.
        // the world already removes all descendants when an entity is removed or shut down.
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

    void Spline::SnapshotState()
    {
        m_prev_closed_loop        = m_closed_loop;
        m_prev_resolution         = m_resolution;
        m_prev_road_width         = m_road_width;
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

        m_prev_attach_mode                = m_attach_mode;
        m_prev_source_spline_entity_id    = m_source_spline_entity_id;
        m_prev_attach_lateral_offset      = m_attach_lateral_offset;
        m_prev_attach_vertical_offset     = m_attach_vertical_offset;
        m_prev_attach_inherit_closed_loop = m_attach_inherit_closed_loop;
        m_prev_attach_sample_count        = m_attach_sample_count;
        m_prev_source_hash                = ComputeSourceHash();
    }

    void Spline::Tick()
    {
        // only draw spline visualization in edit mode
        if (Engine::IsFlagSet(EngineMode::Playing))
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

        if (m_mesh_enabled && has_mesh_input)
        {
            vector<Vector3> current_points = GetControlPointsLocal();
            bool mesh_missing              = !HasRoadMesh();
            uint64_t source_hash           = ComputeSourceHash();

            bool dirty = (m_closed_loop                != m_prev_closed_loop)
                      || (m_resolution                 != m_prev_resolution)
                      || (m_road_width                 != m_prev_road_width)
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
                      || (current_points               != m_prev_control_points)
                      || (m_attach_mode                != m_prev_attach_mode)
                      || (m_source_spline_entity_id    != m_prev_source_spline_entity_id)
                      || (m_attach_lateral_offset      != m_prev_attach_lateral_offset)
                      || (m_attach_vertical_offset     != m_prev_attach_vertical_offset)
                      || (m_attach_inherit_closed_loop != m_prev_attach_inherit_closed_loop)
                      || (m_attach_sample_count        != m_prev_attach_sample_count)
                      || (source_hash                  != m_prev_source_hash);

            if (dirty || mesh_missing)
            {
                GenerateRoadMesh();
                SnapshotState();
            }
        }
        else if (m_mesh_enabled && !has_mesh_input && HasRoadMesh())
        {
            ClearRoadMesh();
        }

        const Color color_curve = Color(0.3f, 0.85f, 0.75f, 1.0f);
        const Color color_point = Color(1.0f, 0.8f, 0.3f, 1.0f);

        // attached splines visualize their derived path by walking GetPoint
        if (IsAttached() && m_source_spline_entity)
        {
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

        // draw the interpolated curve
        uint32_t span_count = m_closed_loop ? static_cast<uint32_t>(points.size()) : static_cast<uint32_t>(points.size()) - 1;
        for (uint32_t span = 0; span < span_count; span++)
        {
            Vector3 prev_point;
            for (uint32_t seg = 0; seg <= m_resolution; seg++)
            {
                float local_t = static_cast<float>(seg) / static_cast<float>(m_resolution);

                // determine the four control points for this span
                int32_t point_count = static_cast<int32_t>(points.size());
                int32_t i1 = static_cast<int32_t>(span);
                int32_t i2 = m_closed_loop ? (i1 + 1) % point_count : min(i1 + 1, point_count - 1);
                int32_t i0 = m_closed_loop ? (i1 - 1 + point_count) % point_count : max(i1 - 1, 0);
                int32_t i3 = m_closed_loop ? (i2 + 1) % point_count : min(i2 + 1, point_count - 1);

                Vector3 current_point = CatmullRom(points[i0], points[i1], points[i2], points[i3], local_t);

                if (seg > 0)
                {
                    Renderer::DrawLine(prev_point, current_point, color_curve, color_curve);
                }

                prev_point = current_point;
            }
        }

        // draw markers at each control point
        float marker_size = 0.15f;
        for (const Vector3& point : points)
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
        m_terrain_offset     = node.attribute("terrain_offset").as_float(0.01f);

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

        // if a mesh was saved, remove the renderable and physics as they will be recreated
        // save the material name first so it can be restored after regeneration
        if (m_needs_road_regeneration && m_entity_ptr)
        {
            if (Render* renderable = m_entity_ptr->GetComponent<Render>())
            {
                if (Material* material = renderable->GetMaterial())
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
            if (Render* renderable = m_entity_ptr->GetComponent<Render>())
            {
                if (Material* material = renderable->GetMaterial())
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
    }

    void Spline::ClearRoadMesh()
    {
        if (m_mesh)
        {
            // preserve the current material so it can be restored on next regeneration
            if (m_saved_material_name.empty() && m_entity_ptr)
            {
                if (Render* renderable = m_entity_ptr->GetComponent<Render>())
                {
                    if (Material* material = renderable->GetMaterial())
                    {
                        m_saved_material_name = material->GetObjectName();
                    }
                }
            }

            // remove the renderable and physics components to avoid dangling mesh pointers
            if (m_entity_ptr)
            {
                m_entity_ptr->RemoveComponent<Physics>();
                m_entity_ptr->RemoveComponent<Render>();
            }

            m_mesh.reset();
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
                    Render* renderable = instance->AddComponent<Render>();
                    renderable->SetMesh(MeshType::Cylinder);
                    renderable->SetDefaultMaterial();
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

                spawned++;
            }

            next_spawn_distance += m_instance_spacing;
        }

        SP_LOG_INFO("spawned %u instances along spline (%.1f m, spacing %.1f m)", spawned, spline_length, m_instance_spacing);
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

        frames.reserve(total_samples + 1);

        float accumulated_distance = 0.0f;
        Vector3 prev_position;

        for (uint32_t i = 0; i <= total_samples; i++)
        {
            float t = static_cast<float>(i) / static_cast<float>(total_samples);

            Vector3 position = EvaluatePoint(spline_points, t);
            Vector3 tangent  = EvaluateTangent(spline_points, t);
            tangent.Normalize();

            // terrain conforming, raycast downward and snap the sample to ground
            if (m_conform_to_terrain)
            {
                Vector3 world_pos = world_matrix * position;
                Vector3 ray_origin(world_pos.x, world_pos.y + 500.0f, world_pos.z);
                Vector3 hit_pos;
                if (PhysicsWorld::RaycastStatic(ray_origin, Vector3::Down, 1000.0f, hit_pos))
                {
                    hit_pos.y += m_terrain_offset;
                    position = inverse_matrix * hit_pos;
                }
            }

            Vector3 up = Vector3::Up;
            if (abs(tangent.Dot(Vector3::Up)) > 0.99f)
            {
                up = Vector3::Forward;
            }

            Vector3 right = tangent.Cross(up);
            right.Normalize();
            up = right.Cross(tangent);
            up.Normalize();

            if (i > 0)
            {
                accumulated_distance += position.Distance(prev_position);
            }
            prev_position = position;

            SplineFrame frame;
            frame.position = position;
            frame.tangent  = tangent;
            frame.right    = right;
            frame.up       = up;
            frame.t        = t;
            frame.distance = accumulated_distance;
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

        bool width_varies = (m_road_width_end != m_road_width);

        uint32_t total_samples = static_cast<uint32_t>(frames.size()) - 1;
        uint32_t profile_count = static_cast<uint32_t>(profile_points.size());

        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        vertices.reserve(frames.size() * profile_count);

        for (uint32_t i = 0; i < frames.size(); i++)
        {
            const SplineFrame& frame = frames[i];

            // interpolate width if it varies along the spline
            float current_width         = width_varies ? (m_road_width + (m_road_width_end - m_road_width) * frame.t) : m_road_width;
            vector<Vector2> cur_profile = width_varies ? GetProfilePointsForWidth(current_width) : profile_points;
            uint32_t cur_profile_count  = static_cast<uint32_t>(cur_profile.size());

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

        // attach to a renderable component on this entity
        Render* renderable = m_entity_ptr->GetComponent<Render>();
        if (!renderable)
        {
            renderable = m_entity_ptr->AddComponent<Render>();
        }
        renderable->SetMesh(m_mesh.get(), 0);

        // restore saved material if one was preserved from a previous load, otherwise use default
        if (!m_saved_material_name.empty())
        {
            shared_ptr<Material> material = ResourceCache::GetByName<Material>(m_saved_material_name);
            if (material)
            {
                renderable->SetMaterial(material);
            }
            else
            {
                renderable->SetDefaultMaterial();
            }
            m_saved_material_name.clear();
        }
        else if (!renderable->GetMaterial())
        {
            renderable->SetDefaultMaterial();
        }

        // disable face culling for profiles that are visible from both sides
        if (m_profile == SplineProfile::Wall || m_profile == SplineProfile::Fence || m_profile == SplineProfile::Tube)
        {
            if (Material* material = renderable->GetMaterial())
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

    Vector3 Spline::CatmullRom(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
    {
        float t2 = t * t;
        float t3 = t2 * t;

        // catmull-rom matrix form
        return 0.5f * (
            (2.0f * p1) +
            (-p0 + p2) * t +
            (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
            (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
        );
    }

    Vector3 Spline::CatmullRomTangent(const Vector3& p0, const Vector3& p1, const Vector3& p2, const Vector3& p3, float t)
    {
        float t2 = t * t;

        // first derivative of the catmull-rom formula
        return 0.5f * (
            (-p0 + p2) +
            (4.0f * p0 - 10.0f * p1 + 8.0f * p2 - 2.0f * p3) * t +
            (-3.0f * p0 + 9.0f * p1 - 9.0f * p2 + 3.0f * p3) * t2
        );
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

        int32_t point_count = static_cast<int32_t>(points.size());
        int32_t i1 = static_cast<int32_t>(span_index);
        int32_t i2 = m_closed_loop ? (i1 + 1) % point_count : min(i1 + 1, point_count - 1);
        int32_t i0 = m_closed_loop ? (i1 - 1 + point_count) % point_count : max(i1 - 1, 0);
        int32_t i3 = m_closed_loop ? (i2 + 1) % point_count : min(i2 + 1, point_count - 1);

        return CatmullRom(points[i0], points[i1], points[i2], points[i3], local_t);
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

        int32_t point_count = static_cast<int32_t>(points.size());
        int32_t i1 = static_cast<int32_t>(span_index);
        int32_t i2 = m_closed_loop ? (i1 + 1) % point_count : min(i1 + 1, point_count - 1);
        int32_t i0 = m_closed_loop ? (i1 - 1 + point_count) % point_count : max(i1 - 1, 0);
        int32_t i3 = m_closed_loop ? (i2 + 1) % point_count : min(i2 + 1, point_count - 1);

        Vector3 tangent = CatmullRomTangent(points[i0], points[i1], points[i2], points[i3], local_t);
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
