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
#include "../../Rendering/Material.h"
#include "../../Resource/ResourceCache.h"
#include "../../Math/Quaternion.h"
#include "../../Math/Helper.h"
#include "../../RHI/RHI_Definitions.h"
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

    }

    Spline::~Spline()
    {
        ClearRoadMesh();

        // don't call ClearInstances() here because during destruction the world's entity
        // list may contain dangling pointers (e.g. shutdown deletes entities in a loop),
        // and RemoveEntity -> AcquireChildren would iterate over freed memory.
        // the world already removes all descendants when an entity is removed or shut down.
    }

    void Spline::Tick()
    {
        // if the spline had a mesh when saved, regenerate it now that child entities are loaded
        if (m_needs_road_regeneration)
        {
            m_needs_road_regeneration = false;
            if (m_mesh_enabled)
            {
                GenerateRoadMesh();
            }
        }

        // only draw spline visualization in edit mode
        if (Engine::IsFlagSet(EngineMode::Playing))
            return;

        // auto-regenerate mesh when any property or control point position changes
        if (m_mesh_enabled && GetControlPointCount() >= 2)
        {
            vector<Vector3> current_points = GetControlPointsLocal();

            bool dirty = (m_closed_loop        != m_prev_closed_loop)
                      || (m_resolution         != m_prev_resolution)
                      || (m_road_width         != m_prev_road_width)
                      || (m_road_width_end     != m_prev_road_width_end)
                      || (m_profile            != m_prev_profile)
                      || (m_height             != m_prev_height)
                      || (m_thickness          != m_prev_thickness)
                      || (m_tube_sides         != m_prev_tube_sides)
                      || (m_uv_tiling_u        != m_prev_uv_tiling_u)
                      || (m_uv_tiling_v        != m_prev_uv_tiling_v)
                      || (m_sidewalk_enabled   != m_prev_sidewalk_enabled)
                      || (m_sidewalk_width     != m_prev_sidewalk_width)
                      || (m_curb_height        != m_prev_curb_height)
                      || (m_conform_to_terrain != m_prev_conform_to_terrain)
                      || (m_terrain_offset     != m_prev_terrain_offset)
                      || (current_points       != m_prev_control_points);

            if (dirty)
            {
                GenerateRoadMesh();

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
                m_prev_control_points     = current_points;
            }
        }
        else if (m_mesh_enabled && GetControlPointCount() < 2 && HasRoadMesh())
        {
            ClearRoadMesh();
        }

        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
            return;

        const Color color_curve = Color(0.3f, 0.85f, 0.75f, 1.0f);
        const Color color_point = Color(1.0f, 0.8f, 0.3f, 1.0f);

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
            // draw a small cross at each control point
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
        node.append_attribute("instance_random_offset")    = m_instance_random_offset;
        node.append_attribute("instance_random_scale_min") = m_instance_random_scale_min;
        node.append_attribute("instance_random_scale_max") = m_instance_random_scale_max;
        node.append_attribute("instance_random_yaw")       = m_instance_random_yaw;
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
        m_instance_random_offset     = node.attribute("instance_random_offset").as_float(0.0f);
        m_instance_random_scale_min  = node.attribute("instance_random_scale_min").as_float(1.0f);
        m_instance_random_scale_max  = node.attribute("instance_random_scale_max").as_float(1.0f);
        m_instance_random_yaw        = node.attribute("instance_random_yaw").as_float(0.0f);

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
        return EvaluatePoint(GetControlPoints(), t);
    }

    Vector3 Spline::GetTangent(float t) const
    {
        return EvaluateTangent(GetControlPoints(), t);
    }

    float Spline::GetLength(uint32_t samples_per_span) const
    {
        vector<Vector3> points = GetControlPoints();
        if (points.size() < 2)
            return 0.0f;

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
            return 0;

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
            return;

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
            return;

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
        // need at least 2 control points
        vector<Vector3> spline_points = GetControlPointsLocal();
        if (spline_points.size() < 2)
        {
            SP_LOG_WARNING("need at least 2 control points to generate a mesh");
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
        GenerateMesh(spline_points, profile_points, close_profile);
    }

    void Spline::ClearRoadMesh()
    {
        if (m_mesh)
        {
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
            return;

        // clear any existing instances first
        ClearInstances();

        vector<Vector3> points = GetControlPointsLocal();
        if (points.size() < 2)
        {
            SP_LOG_WARNING("need at least 2 control points to spawn instances");
            return;
        }

        float spline_length = GetLength();
        if (spline_length < m_instance_spacing)
        {
            SP_LOG_WARNING("spline is shorter than instance spacing");
            return;
        }

        // walk along the spline at arc-length intervals and place instances
        uint32_t instance_count = static_cast<uint32_t>(spline_length / m_instance_spacing);
        uint32_t total_samples  = static_cast<uint32_t>(points.size()) * m_resolution * 4; // dense sampling for arc-length
        float step              = 1.0f / static_cast<float>(total_samples);

        float accumulated_distance  = 0.0f;
        float next_spawn_distance   = 0.0f;
        Vector3 prev_position       = EvaluatePoint(points, 0.0f);
        uint32_t spawned            = 0;

        for (uint32_t i = 0; i <= total_samples; i++)
        {
            float t         = static_cast<float>(i) * step;
            Vector3 position = EvaluatePoint(points, t);

            if (i > 0)
            {
                accumulated_distance += position.Distance(prev_position);
            }
            prev_position = position;

            if (accumulated_distance >= next_spawn_distance)
            {
                Entity* instance = World::CreateEntity();
                instance->SetObjectName(prefix_instance + to_string(spawned));
                instance->SetParent(m_entity_ptr);

                // apply random lateral offset perpendicular to the spline
                Vector3 final_position = position;
                if (m_instance_random_offset > 0.0f)
                {
                    Vector3 tangent_dir = EvaluateTangent(points, t);
                    tangent_dir.Normalize();
                    Vector3 lateral = tangent_dir.Cross(Vector3::Up);
                    lateral.Normalize();
                    float offset = math::random<float>(-m_instance_random_offset, m_instance_random_offset);
                    final_position = final_position + lateral * offset;
                }
                instance->SetPositionLocal(final_position);

                // rotation: align to spline + optional random yaw
                Quaternion rotation = Quaternion::Identity;
                if (m_align_instances_to_spline)
                {
                    Vector3 tangent = EvaluateTangent(points, t);
                    tangent.Normalize();
                    rotation = Quaternion::FromLookRotation(tangent, Vector3::Up);
                }
                if (m_instance_random_yaw > 0.0f)
                {
                    float yaw = math::random<float>(-m_instance_random_yaw, m_instance_random_yaw);
                    rotation = rotation * Quaternion::FromAxisAngle(Vector3::Up, yaw * math::deg_to_rad);
                }
                instance->SetRotationLocal(rotation);

                // random scale
                if (m_instance_random_scale_min != 1.0f || m_instance_random_scale_max != 1.0f)
                {
                    float scale = math::random<float>(m_instance_random_scale_min, m_instance_random_scale_max);
                    instance->SetScaleLocal(Vector3(scale, scale, scale));
                }

                Render* renderable = instance->AddComponent<Render>();
                renderable->SetMesh(MeshType::Cylinder);
                renderable->SetDefaultMaterial();

                spawned++;
                next_spawn_distance += m_instance_spacing;
            }
        }

        SP_LOG_INFO("spawned %u instances along spline (%.1f m, spacing %.1f m)", spawned, spline_length, m_instance_spacing);
    }

    void Spline::ClearInstances()
    {
        if (!m_entity_ptr)
            return;

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
            return points;

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
            return points;

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

    void Spline::GenerateMesh(const vector<Vector3>& spline_points, const vector<Vector2>& profile_points, bool close_profile)
    {
        if (spline_points.size() < 2 || profile_points.size() < 2)
            return;

        bool width_varies = (m_road_width_end != m_road_width);

        uint32_t span_count    = m_closed_loop ? static_cast<uint32_t>(spline_points.size()) : static_cast<uint32_t>(spline_points.size()) - 1;
        uint32_t total_samples = span_count * m_resolution;
        uint32_t profile_count = static_cast<uint32_t>(profile_points.size());
        uint32_t edge_count    = close_profile ? profile_count : profile_count - 1;

        // world transform for terrain conforming (local -> world and back)
        Matrix world_matrix   = m_entity_ptr ? m_entity_ptr->GetMatrix() : Matrix::Identity;
        Matrix inverse_matrix = world_matrix.Inverted();

        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        uint32_t sample_count = total_samples + 1;
        vertices.reserve(sample_count * profile_count);

        float accumulated_distance = 0.0f;
        Vector3 prev_position;

        for (uint32_t i = 0; i <= total_samples; i++)
        {
            float t = static_cast<float>(i) / static_cast<float>(total_samples);

            Vector3 position = EvaluatePoint(spline_points, t);
            Vector3 tangent  = EvaluateTangent(spline_points, t);
            tangent.Normalize();

            // terrain conforming: raycast downward from this sample to snap to ground
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

            // interpolate width if it varies along the spline
            float current_width         = width_varies ? (m_road_width + (m_road_width_end - m_road_width) * t) : m_road_width;
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
                cur_perimeter = 1.0f;

            float v = (accumulated_distance / m_road_width) * m_uv_tiling_v;

            float accumulated_profile_distance = 0.0f;
            for (uint32_t j = 0; j < cur_profile_count; j++)
            {
                Vector3 vertex_pos = position + right * cur_profile[j].x + up * cur_profile[j].y;

                if (j > 0)
                {
                    accumulated_profile_distance += Vector2::Distance(cur_profile[j], cur_profile[j - 1]);
                }
                float u = (accumulated_profile_distance / cur_perimeter) * m_uv_tiling_u;

                Vector3 normal;
                if (cur_profile_count == 2)
                {
                    normal = up;
                }
                else
                {
                    uint32_t j_prev = (j == 0) ? (close_profile ? cur_profile_count - 1 : 0) : j - 1;
                    uint32_t j_next = (j == cur_profile_count - 1) ? (close_profile ? 0 : cur_profile_count - 1) : j + 1;

                    Vector2 edge = cur_profile[j_next] - cur_profile[j_prev];
                    Vector2 perp = Vector2(edge.y, -edge.x);
                    float perp_len = sqrtf(perp.x * perp.x + perp.y * perp.y);
                    if (perp_len > 0.001f)
                    {
                        perp.x /= perp_len;
                        perp.y /= perp_len;
                    }

                    normal = right * perp.x + up * perp.y;
                    normal.Normalize();
                }

                vertices.emplace_back(vertex_pos, Vector2(u, v), normal, tangent);
            }
        }

        // generate triangle indices connecting adjacent cross-sections
        // profile point count is constant regardless of width, so profile_count is always valid
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
            accumulated_distance);
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
            return Vector3::Zero;
        if (points.size() == 1)
            return points[0];

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
            return Vector3::Forward;

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
