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

//= INCLUDES ===================
#include "Component.h"
#include <memory>
#include <vector>
#include "../../Math/Vector3.h"
#include "../../RHI/RHI_Vertex.h"
//==============================

namespace spartan
{
    class Mesh;
    class Material;
    class Physics;
    class RHI_Texture;

    // slip-driven tire skid marks, grows a ribbon of quads at each wheel contact point
    class SkidMarks : public Component
    {
    public:
        SkidMarks(Entity* entity);
        ~SkidMarks();

        // component
        void Tick() override;
        void Remove() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;

        // tunables
        float GetSlipThreshold() const          { return m_slip_threshold; }
        void  SetSlipThreshold(float value)     { m_slip_threshold = value; }
        float GetMinSegmentDistance() const     { return m_min_segment_distance; }
        void  SetMinSegmentDistance(float value){ m_min_segment_distance = value; }
        uint32_t GetMaxSegments() const         { return m_max_segments; }
        void  SetMaxSegments(uint32_t value)    { m_max_segments = value; }
        float GetOpacity() const                { return m_opacity; }
        void  SetOpacity(float value)           { m_opacity = value; }

    private:
        // a recently deposited quad, kept so its tail can be faded out when the skid ends
        struct RecentQuad
        {
            RHI_Vertex_PosTexNorTan verts[4];
            uint32_t slot = 0;
        };

        // per-wheel growing ribbon, each segment is an independent quad so gaps never bridge
        struct WheelTrail
        {
            Entity* entity                = nullptr;
            std::shared_ptr<Mesh> mesh;
            uint32_t global_vertex_offset = 0;
            uint32_t capacity_quads       = 0;
            uint32_t head_quad            = 0; // next ring slot to write
            bool active                   = false;
            bool has_edge                 = false;
            bool has_smooth               = false;
            math::Vector3 anchor_center   = math::Vector3::Zero;
            math::Vector3 smooth_center   = math::Vector3::Zero; // jitter filtered contact point
            math::Vector3 edge_left       = math::Vector3::Zero;
            math::Vector3 edge_right      = math::Vector3::Zero;
            float u_accum                 = 0.0f; // distance since the strip started
            float height_offset           = 0.0f; // per strip lift, breaks coplanar z-fighting
            uint32_t strip_index          = 0;     // increments per strip for height layering
            std::vector<RecentQuad> recent;        // trailing quads pending end fade
        };

        void EnsureInitialized();
        void BuildTrailMesh(WheelTrail& trail, const std::string& name);
        void DepositQuad(WheelTrail& trail, const math::Vector3& bl, const math::Vector3& br, float u_a, float u_b, const math::Vector3& normal, const math::Vector3& tangent, float intensity_a, float intensity_b);
        void FadeStripEnd(WheelTrail& trail);
        void CreateMaterial();

        Physics* m_physics  = nullptr;
        bool m_initialized  = false;
        WheelTrail m_trails[4];
        std::shared_ptr<Material> m_material;
        std::shared_ptr<RHI_Texture> m_texture;

        // tunables
        float m_slip_threshold       = 0.35f; // combined slip magnitude needed to start marking
        float m_min_segment_distance = 0.15f; // minimum travel before a new quad is laid
        uint32_t m_max_segments      = 256;   // ring buffer size per wheel  
        float m_opacity              = 0.75f; // base material alpha
        float m_z_offset             = 0.02f; // lift above ground to avoid z-fighting
        float m_uv_tiling            = 0.5f;  // texture repeats per meter along travel
        float m_fade_distance        = 0.8f;  // length over which a strip fades in and out
        float m_center_smoothing     = 0.5f;  // contact point low pass, kills lateral jitter
    };
}
