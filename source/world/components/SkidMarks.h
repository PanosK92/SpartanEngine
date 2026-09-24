/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

#pragma once

//= INCLUDES ===================
#include "Component.h"
#include <memory>
#include <vector>
#include "../../math/Vector3.h"
#include "../../rhi/RHI_Vertex.h"
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
        void Initialize() override;
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
            uint32_t slot      = 0;
            float u_a          = 0.0f;
            float u_b          = 0.0f;
            float intensity_a  = 1.0f;
            float intensity_b  = 1.0f;
            float fade_a       = 0.0f;
            float fade_b       = 0.0f;
            float birth_time   = 0.0f;
            float age_fade     = 1.0f;
            uint64_t sequence  = 0;
            bool occupied     = false;
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
            math::Vector3 anchor_center   = math::Vector3::Zero;
            math::Vector3 edge_left       = math::Vector3::Zero;
            math::Vector3 edge_right      = math::Vector3::Zero;
            float u_accum                 = 0.0f; // distance since the strip started
            float intensity_edge          = 1.0f; // slip intensity baked into the last deposited edge
            std::vector<RecentQuad> recent;        // trailing quads pending end fade
            std::vector<RecentQuad> quads;         // CPU mirror for gradual age/capacity retirement
            uint64_t quad_sequence        = 0;
            float intensity               = 0.0f;
            math::Vector3 edge_normal     = math::Vector3::Up;
            math::Vector3 last_travel     = math::Vector3::Zero;
            bool stationary_patch        = false;
            uint32_t patch_slots[2]      = {};
            float patch_deposit          = 0.0f;
        };

        void EnsureInitialized();
        void BuildTrailMesh(WheelTrail& trail, const std::string& name);
        void DepositQuad(WheelTrail& trail, const math::Vector3& bl, const math::Vector3& br, float u_a, float u_b, float fade_a, float fade_b, float intensity_a, float intensity_b, const math::Vector3& normal, const math::Vector3& tangent);
        void FadeStripEnd(WheelTrail& trail);
        void AgeTrail(WheelTrail& trail);
        void DepositStationaryPatch(WheelTrail& trail, const math::Vector3& center, const math::Vector3& right,
            const math::Vector3& normal, float half_width, float patch_length, float dt);
        void ValidateSettings();
        void CreateMaterial();

        Physics* m_physics  = nullptr;
        bool m_initialized  = false;
        float m_time = 0.0f;
        WheelTrail m_trails[4];
        std::shared_ptr<Material> m_material;
        std::shared_ptr<RHI_Texture> m_texture;

        // tunables
        float m_slip_threshold       = 0.35f; // combined slip magnitude needed to start marking
        float m_min_segment_distance = 0.05f; // minimum travel before a new quad is laid
        uint32_t m_max_segments      = 4096;  // bounded ring buffer per wheel
        float m_opacity              = 0.75f; // maximum deposited coverage
        float m_z_offset             = 0.02f; // lift above ground to avoid z-fighting
        float m_uv_tiling            = 1.25f; // texture repeats per meter along travel
        float m_fade_distance        = 1.1f;  // length of the alpha fade in and fade out at each strip end, in meters
        float m_width_scale          = 0.86f; // contact patch is narrower than the physical tire
    };
}
