/*
Copyright(c) 2015-2025 Panos Karabelas

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

//= INCLUDES =================================
#include "Component.h"
#include "Rendering/RenderOptionsPool.h"
//============================================

namespace spartan
{
    static constexpr float default_shape_size = 1.0f;
    static constexpr float default_transition_size = 0.2f;

    enum class VolumeType
    {
        Box,
        Sphere,
        Max
    };

    /*
     * A component that handles local overrides of global components.
     * A volume contains a map of each component with a list of its registered attributes.
     * Static data help with managing and accumulating data from multiple volumes at once
     * and avoid repeated actions per frame.
     */
    class Volume : public Component
    {
    private:
        //===== STATIC DATA FOR VOLUME MANAGEMENT =====
        static RenderOptionsPool blended_options; // volume data accumulation (on interpolation)
        static std::map<Renderer_Option, float> accumulator_floats;
        static std::map<Renderer_Option, float> accumulator_weights;
        static int overlapping_count;
        static uint64_t accumulation_frame;
        static uint64_t finalization_frame;
        //=============================================

        VolumeType m_volume_shape_type   = VolumeType::Sphere;
        float m_shape_size               = default_shape_size;
        float m_transition_size          = default_transition_size;
        bool m_is_debug_draw_enabled     = true;

        math::BoundingBox m_bounding_box = math::BoundingBox::Unit;

        RenderOptionsPool m_options_pool = RenderOptionsPool(RenderOptionsListType::Override);

        void AccumulateRenderOptions(float alpha);
        static void ApplyRenderOptions();

        void DrawVolume();
    public:
        Volume(Entity* entity);
        ~Volume() override;

        //= COMPONENT ================================
        void PreTick() override;
        void Tick() override;
        void Save(pugi::xml_node& node) override;
        void Load(pugi::xml_node& node) override;
        //============================================

        float ComputeAlpha(const math::Vector3& camera_position) const;

        // Render Options Collection
        RenderOptionsPool& GetOptionsCollection() { return m_options_pool; }
        void SetOptionsCollection(const RenderOptionsPool& options_collection) { m_options_pool = options_collection; }

        // Mesh Type
        VolumeType GetVolumeShapeType() const { return m_volume_shape_type; }
        void SetMeshType(const VolumeType volume_type) { m_volume_shape_type = volume_type; }

        // Shape size
        float GetShapeSize() const { return m_shape_size; }
        void SetShapeSize(const float shape_size) { m_shape_size = shape_size; }

        // Transition size
        float GetTransitionSize() const { return m_transition_size; }
        void SetTransitionSize(const float transition_size) { m_transition_size = transition_size; }

        // Debug Draw
        bool GetDebugDrawEnabled() const { return m_is_debug_draw_enabled; }
        void SetDebugDrawEnabled(bool value) { m_is_debug_draw_enabled = value; }
    };
}
