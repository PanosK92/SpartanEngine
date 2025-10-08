/*
Copyright(c) 2015-2025 Panos Karabelas, George Mavroeidis

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
#include "Geometry/Mesh.h"
#include "Rendering/Renderer_Definitions.h"
//============================================

namespace spartan
{
    enum class VolumeType
    {
        Box,
        Sphere,
        Max
    };

    /*
     * A component that handles local overrides of global components.
     * A volume contains a map of each component with a list of its registered attributes.
     */
    class Volume : public Component
    {
    public:
        Volume(Entity* entity);
        ~Volume();

        //= COMPONENT ================================
        //void PreTick() override;
        void Tick() override;
        //void Save(pugi::xml_node& node) override;
        //void Load(pugi::xml_node& node) override;
        //============================================

        // Render Options
        std::unordered_map<Renderer_Option, float> GetRenderOptions() const { return m_options;}
        void GetRenderOptions(const std::unordered_map<Renderer_Option, float>& options) { m_options = options; }

        // Mesh Type
        VolumeType GetVolumeShapeType() const { return m_volume_shape_type; }
        void SetMeshType(const VolumeType volume_type) { m_volume_shape_type = volume_type; }

        // Shape size
        float GetShapeSize() const { return m_shape_size; }
        void SetShapeSize(const float shape_size) { m_shape_size = shape_size; }

        // Transition size
        float GetTransitionSize() const { return m_transition_size; }
        void SetTransitionSize(const float transition_size) { m_transition_size = transition_size; }

    private:
        VolumeType m_volume_shape_type   = VolumeType::Sphere;
        float m_shape_size               = 1.0f;
        float m_transition_size          = 0.2f;
        math::BoundingBox m_bounding_box = math::BoundingBox::Unit;

        //std::unordered_map<ComponentType, std::vector<Attribute>> m_component_attributes; // might be useful for interpolating directional light attributes?
        std::unordered_map<Renderer_Option, float> m_options;
    };
}
