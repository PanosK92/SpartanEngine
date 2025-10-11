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

//= INCLUDES =================================
#include "pch.h"
#include "Rendering/Renderer.h"
#include "Volume.h"
#include "World/Entity.h"
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

        m_options_collection = RenderOptionsPool();
    }

    Volume::~Volume()
    {

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
}
