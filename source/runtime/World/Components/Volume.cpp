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

//= INCLUDES =================================
#include "pch.h"
#include "Rendering/Renderer.h"
#include "Volume.h"

#include <algorithm>

#include "Camera.h"
#include "IO/pugixml.hpp"
#include "World/Entity.h"
#include "World/Subsystems/RenderOptionsSubsystem.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    Volume::Volume(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_volume_shape_type, VolumeType);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_shape_size, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_transition_size, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_is_debug_draw_enabled, bool);

        m_volume_shape_type = VolumeType::Sphere;
        m_bounding_box = BoundingBox::Unit;
        m_shape_size = default_shape_size;
        m_transition_size = default_transition_size;
        m_is_debug_draw_enabled = true;

        m_options_pool = RenderOptionsPool(RenderOptionsListType::Component);
    }

    Volume::~Volume()
    {
        RenderOptionsSubsystem::RemoveVolume(this);
    }

    void Volume::Initialize()
    {
        RenderOptionsSubsystem::AddVolume(this);
    }

    void Volume::Tick()
    {
        if (m_is_debug_draw_enabled)
        {
            // For debugging
            if (m_volume_shape_type == VolumeType::Sphere)
            {
                Renderer::DrawSphere(m_entity_ptr->GetPosition(), m_shape_size, 16, Color::standard_yellow);
                Renderer::DrawSphere(m_entity_ptr->GetPosition(), m_shape_size + m_transition_size, 16, Color::standard_renderer_lines);
            }
            else if (m_volume_shape_type == VolumeType::Box)
            {
                const Matrix new_matrix_inner = Matrix(m_entity_ptr->GetPosition(), m_entity_ptr->GetRotation(), m_entity_ptr->GetScale() + m_shape_size);
                Renderer::DrawBox(m_bounding_box * new_matrix_inner, Color::standard_yellow);

                const Matrix new_matrix_outer = Matrix(m_entity_ptr->GetPosition(), m_entity_ptr->GetRotation(), m_entity_ptr->GetScale() + m_shape_size + m_transition_size);
                Renderer::DrawBox(m_bounding_box * new_matrix_outer, Color::standard_renderer_lines);
            }
        }
    }

    void Volume::Save(pugi::xml_node& node)
    {
        node.append_attribute("shape_type")      = static_cast<int>(m_volume_shape_type);
        node.append_attribute("shape_size")      = m_shape_size;
        node.append_attribute("transition_size") = m_transition_size;
        node.append_attribute("debug_enabled")   = m_is_debug_draw_enabled;
    }

    void Volume::Load(pugi::xml_node& node)
    {
        m_volume_shape_type = static_cast<VolumeType>(node.attribute("shape_type").as_int(static_cast<int>(VolumeType::Max)));
        m_shape_size = node.attribute("shape_size").as_float();
        m_transition_size = node.attribute("transition_size").as_float();
        m_is_debug_draw_enabled = node.attribute("debug_enabled").as_bool();

        m_bounding_box = BoundingBox::Unit;
        m_options_pool = RenderOptionsPool(RenderOptionsListType::Component);
    }

    float Volume::ComputeAlpha(const Vector3& camera_position) const
    {
        if (m_volume_shape_type == VolumeType::Box)
        {
            const Vector3 distance_absolute = (camera_position - m_entity_ptr->GetPosition()).Abs();

            const Vector3 inner_half_extents = m_bounding_box.GetExtents() + Vector3(m_shape_size / 2.0f);
            const Vector3 outer_half_extents = inner_half_extents + Vector3(m_transition_size);

            const float dist_to_inner = (distance_absolute - inner_half_extents).Max(Vector3::Zero).Length();
            const float dist_to_outer = (distance_absolute - outer_half_extents).Max(Vector3::Zero).Length();

            if (dist_to_inner <= 0.0f)
            {
                return 1.0f;
            }
            if (dist_to_outer > 0.0f)
            {
                return 0.0f;
            }

            return 1.0f - dist_to_inner / m_transition_size;
        }
        if (m_volume_shape_type == VolumeType::Sphere)
        {
            const float distance = Vector3::Distance(camera_position, m_entity_ptr->GetPosition());

            if (distance > m_transition_size + m_shape_size)
            {
                return 0.0f;
            }
            if (distance <= m_shape_size)
            {
                return 1.0f;
            }

            return 1.0f - (distance - m_shape_size) / m_transition_size;
        }

        // Unknown shape
        return 1.0f;
    }
}
