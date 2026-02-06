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
#include "../../Math/Vector3.h"
//=============================

namespace spartan
{
    class Mesh;

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

        // road mesh generation
        void GenerateRoadMesh();
        void ClearRoadMesh();
        bool HasRoadMesh() const { return m_mesh != nullptr; }

        // properties
        bool GetClosedLoop() const              { return m_closed_loop; }
        void SetClosedLoop(bool closed)         { m_closed_loop = closed; }
        uint32_t GetResolution() const          { return m_resolution; }
        void SetResolution(uint32_t resolution) { m_resolution = resolution; }
        float GetRoadWidth() const              { return m_road_width; }
        void SetRoadWidth(float width)          { m_road_width = width; }

    private:
        // gather control point world positions from child entities
        std::vector<math::Vector3> GetControlPoints() const;

        // gather control point positions local to the spline entity
        std::vector<math::Vector3> GetControlPointsLocal() const;

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

        bool m_closed_loop     = false;
        uint32_t m_resolution  = 20; // line segments per span
        float m_road_width     = 8.0f;

        // generated road mesh
        std::shared_ptr<Mesh> m_mesh;
    };
}
