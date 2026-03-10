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

#pragma once

//= INCLUDES ==================
#include "Component.h"
#include "../../Math/Vector2.h"
#include "../../Math/Vector3.h"
//=============================

namespace spartan
{
    class Mesh;

    // cross-section profile that gets extruded along the spline
    enum class SplineProfile : uint8_t
    {
        Road,    // flat strip
        Wall,    // vertical quad
        Tube,    // circular cross-section
        Fence,   // thin tall rectangle
        Channel, // u-shaped gutter/trench
        Max
    };

    class Spline : public Component
    {
    public:
        Spline(Entity* entity);
        ~Spline();

        // lifecycle
        void Tick() override;

        // serialization
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // evaluation - t is normalized [0, 1] across the entire spline
        math::Vector3 GetPoint(float t) const;
        math::Vector3 GetTangent(float t) const;
        float GetLength(uint32_t samples_per_span = 10) const;

        // control point management (children of the owning entity)
        uint32_t GetControlPointCount() const;
        void AddControlPoint(const math::Vector3& local_position = math::Vector3::Zero);
        void RemoveLastControlPoint();

        // mesh generation - extrudes the current profile along the spline
        void GenerateRoadMesh();
        void ClearRoadMesh();
        bool HasRoadMesh() const { return m_mesh != nullptr; }

        // mesh generation toggle
        bool GetMeshEnabled() const       { return m_mesh_enabled; }
        void SetMeshEnabled(bool enabled) { m_mesh_enabled = enabled; }

        // instanced mesh placement along the spline
        void SpawnInstances();
        void ClearInstances();

        // spline properties
        bool GetClosedLoop() const              { return m_closed_loop; }
        void SetClosedLoop(bool closed)         { m_closed_loop = closed; }
        uint32_t GetResolution() const          { return m_resolution; }
        void SetResolution(uint32_t resolution) { m_resolution = resolution; }
        float GetRoadWidth() const              { return m_road_width; }
        void SetRoadWidth(float width)          { m_road_width = width; }

        // profile properties
        SplineProfile GetProfile() const              { return m_profile; }
        void SetProfile(SplineProfile profile)        { m_profile = profile; }
        float GetHeight() const                       { return m_height; }
        void SetHeight(float height)                  { m_height = height; }
        float GetThickness() const                    { return m_thickness; }
        void SetThickness(float thickness)            { m_thickness = thickness; }
        uint32_t GetTubeSides() const                 { return m_tube_sides; }
        void SetTubeSides(uint32_t sides)             { m_tube_sides = sides; }

        // width variation (interpolated from start to end along the spline)
        float GetRoadWidthEnd() const                 { return m_road_width_end; }
        void SetRoadWidthEnd(float width)             { m_road_width_end = width; }

        // uv tiling
        float GetUvTilingU() const                    { return m_uv_tiling_u; }
        void SetUvTilingU(float tiling)               { m_uv_tiling_u = tiling; }
        float GetUvTilingV() const                    { return m_uv_tiling_v; }
        void SetUvTilingV(float tiling)               { m_uv_tiling_v = tiling; }

        // sidewalk/curb (road profile only)
        bool GetSidewalkEnabled() const               { return m_sidewalk_enabled; }
        void SetSidewalkEnabled(bool enabled)         { m_sidewalk_enabled = enabled; }
        float GetSidewalkWidth() const                { return m_sidewalk_width; }
        void SetSidewalkWidth(float width)            { m_sidewalk_width = width; }
        float GetCurbHeight() const                   { return m_curb_height; }
        void SetCurbHeight(float height)              { m_curb_height = height; }

        // terrain conforming
        bool GetConformToTerrain() const              { return m_conform_to_terrain; }
        void SetConformToTerrain(bool conform)        { m_conform_to_terrain = conform; }
        float GetTerrainOffset() const                { return m_terrain_offset; }
        void SetTerrainOffset(float offset)           { m_terrain_offset = offset; }

        // instancing properties
        float GetInstanceSpacing() const                        { return m_instance_spacing; }
        void SetInstanceSpacing(float spacing)                  { m_instance_spacing = spacing; }
        bool GetAlignInstancesToSpline() const                  { return m_align_instances_to_spline; }
        void SetAlignInstancesToSpline(bool align)              { m_align_instances_to_spline = align; }
        const std::string& GetInstanceMeshPath() const          { return m_instance_mesh_path; }
        void SetInstanceMeshPath(const std::string& path)       { m_instance_mesh_path = path; }

        // procedural placement randomization
        float GetInstanceRandomOffset() const                   { return m_instance_random_offset; }
        void SetInstanceRandomOffset(float offset)              { m_instance_random_offset = offset; }
        float GetInstanceRandomScaleMin() const                 { return m_instance_random_scale_min; }
        void SetInstanceRandomScaleMin(float scale)             { m_instance_random_scale_min = scale; }
        float GetInstanceRandomScaleMax() const                 { return m_instance_random_scale_max; }
        void SetInstanceRandomScaleMax(float scale)             { m_instance_random_scale_max = scale; }
        float GetInstanceRandomYaw() const                      { return m_instance_random_yaw; }
        void SetInstanceRandomYaw(float degrees)                { m_instance_random_yaw = degrees; }

    private:
        // gather control point world positions from child entities
        std::vector<math::Vector3> GetControlPoints() const;

        // gather control point positions local to the spline entity
        std::vector<math::Vector3> GetControlPointsLocal() const;

        // resolve the current profile into a set of 2d cross-section points (in right-up plane)
        std::vector<math::Vector2> GetProfilePoints() const;
        std::vector<math::Vector2> GetProfilePointsForWidth(float width) const;

        // whether the current profile forms a closed loop cross-section (e.g. tube)
        bool IsProfileClosed() const;

        // generalized mesh extrusion along the spline using the given cross-section
        void GenerateMesh(const std::vector<math::Vector3>& spline_points, const std::vector<math::Vector2>& profile_points, bool close_profile);

        // catmull-rom interpolation between four points
        static math::Vector3 CatmullRom(
            const math::Vector3& p0, const math::Vector3& p1,
            const math::Vector3& p2, const math::Vector3& p3,
            float t
        );

        // catmull-rom tangent (first derivative) between four points
        static math::Vector3 CatmullRomTangent(
            const math::Vector3& p0, const math::Vector3& p1,
            const math::Vector3& p2, const math::Vector3& p3,
            float t
        );

        // evaluate spline position from an arbitrary set of control points
        math::Vector3 EvaluatePoint(const std::vector<math::Vector3>& points, float t) const;
        math::Vector3 EvaluateTangent(const std::vector<math::Vector3>& points, float t) const;

        // maps a normalized t to a span index and local t
        void MapToSpan(float t, const std::vector<math::Vector3>& points, uint32_t& span_index, float& local_t) const;

        // spline
        bool m_closed_loop             = false;
        uint32_t m_resolution          = 20;
        float m_road_width             = 8.0f;
        bool m_needs_road_regeneration = false;
        bool m_mesh_enabled            = false;

        // profile
        SplineProfile m_profile  = SplineProfile::Road;
        float m_height           = 3.0f;
        float m_thickness        = 0.3f;
        uint32_t m_tube_sides    = 12;

        // width variation
        float m_road_width_end = 8.0f;

        // uv tiling
        float m_uv_tiling_u = 1.0f;
        float m_uv_tiling_v = 1.0f;

        // sidewalk/curb
        bool m_sidewalk_enabled  = false;
        float m_sidewalk_width   = 2.0f;
        float m_curb_height      = 0.15f;

        // terrain conforming
        bool m_conform_to_terrain = false;
        float m_terrain_offset    = 0.01f;

        // instancing
        float m_instance_spacing              = 5.0f;
        bool m_align_instances_to_spline      = true;
        std::string m_instance_mesh_path;
        float m_instance_random_offset        = 0.0f;
        float m_instance_random_scale_min     = 1.0f;
        float m_instance_random_scale_max     = 1.0f;
        float m_instance_random_yaw           = 0.0f;

        // material name to restore after mesh regeneration
        std::string m_saved_material_name;

        // generated mesh
        std::shared_ptr<Mesh> m_mesh;

        // snapshot of previous state for auto-regeneration
        bool m_prev_closed_loop                         = false;
        uint32_t m_prev_resolution                      = 0;
        float m_prev_road_width                         = 0.0f;
        float m_prev_road_width_end                     = 0.0f;
        SplineProfile m_prev_profile                    = SplineProfile::Road;
        float m_prev_height                             = 0.0f;
        float m_prev_thickness                          = 0.0f;
        uint32_t m_prev_tube_sides                      = 0;
        float m_prev_uv_tiling_u                        = 0.0f;
        float m_prev_uv_tiling_v                        = 0.0f;
        bool m_prev_sidewalk_enabled                    = false;
        float m_prev_sidewalk_width                     = 0.0f;
        float m_prev_curb_height                        = 0.0f;
        bool m_prev_conform_to_terrain                  = false;
        float m_prev_terrain_offset                     = 0.0f;
        std::vector<math::Vector3> m_prev_control_points;
    };
}
