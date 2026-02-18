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

        // instancing properties
        float GetInstanceSpacing() const                        { return m_instance_spacing; }
        void SetInstanceSpacing(float spacing)                  { m_instance_spacing = spacing; }
        bool GetAlignInstancesToSpline() const                  { return m_align_instances_to_spline; }
        void SetAlignInstancesToSpline(bool align)              { m_align_instances_to_spline = align; }
        const std::string& GetInstanceMeshPath() const          { return m_instance_mesh_path; }
        void SetInstanceMeshPath(const std::string& path)       { m_instance_mesh_path = path; }

    private:
        // gather control point world positions from child entities
        std::vector<math::Vector3> GetControlPoints() const;

        // gather control point positions local to the spline entity
        std::vector<math::Vector3> GetControlPointsLocal() const;

        // resolve the current profile into a set of 2d cross-section points (in right-up plane)
        std::vector<math::Vector2> GetProfilePoints() const;

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
        uint32_t m_resolution          = 20; // line segments per span
        float m_road_width             = 8.0f;
        bool m_needs_road_regeneration = false;

        // profile
        SplineProfile m_profile  = SplineProfile::Road;
        float m_height           = 3.0f;
        float m_thickness        = 0.3f;
        uint32_t m_tube_sides    = 12;

        // instancing
        float m_instance_spacing              = 5.0f;
        bool m_align_instances_to_spline      = true;
        std::string m_instance_mesh_path;

        // material name to restore after mesh regeneration
        std::string m_saved_material_name;

        // generated mesh
        std::shared_ptr<Mesh> m_mesh;
    };
}
