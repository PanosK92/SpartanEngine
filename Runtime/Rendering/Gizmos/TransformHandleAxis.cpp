/*
Copyright(c) 2016-2021 Panos Karabelas

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
#include "Spartan.h"
#include "TransformHandleAxis.h"
#include "../Renderer.h"
#include "../../Input/Input.h"
#include "../../World/Components/Transform.h"
//============================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{

    TransformHandleAxis::TransformHandleAxis(TransformHandleType type, const Math::Vector3& axis, Context* context)
    {
        m_axis      = axis;
        m_type      = type;
        m_context   = context;
        m_renderer  = context->GetSubsystem<Renderer>();
        m_input     = context->GetSubsystem<Input>();
    }

    void TransformHandleAxis::UpdateTransform()
    {
        m_transform         = Math::Matrix(m_position, m_rotation, m_scale);
        m_box_transformed   = m_box.Transform(m_transform);
    }

    void TransformHandleAxis::UpdateInput(Transform* transform)
    {
        // First press
        if (m_is_hovered && m_input->GetKeyDown(KeyCode::Click_Left))
        {
            m_is_editing = m_is_hovered && m_input->GetKeyDown(KeyCode::Click_Left);
        }

        // Editing can happen here
        if (m_is_editing && m_input->GetKey(KeyCode::Click_Left))
        {
            if (m_type == TransformHandleType::Position)
            {
                auto position = transform->GetPosition();
                position += m_delta * m_axis;
                transform->SetPosition(position);
            }
            else if (m_type == TransformHandleType::Scale)
            {
                auto scale = transform->GetScale();
                scale += m_delta * m_axis;
                transform->SetScale(scale);
            }
            else if (m_type == TransformHandleType::Rotation)
            {
                auto rotation = transform->GetRotation().ToEulerAngles();
                const auto speed_multiplier = 10.0f;
                rotation += m_delta * m_axis * speed_multiplier;
                transform->SetRotation(Quaternion::FromEulerAngles(rotation));
            }
        }

        // Last press (on release)
        if (m_is_editing && m_input->GetKeyUp(KeyCode::Click_Left))
        {
            m_is_editing = false;
        }
    }

    void TransformHandleAxis::DrawPrimitives(const Vector3& transform_center) const
    {
        // Draw axis line (connect the handle with the origin of the transform)
        const Vector4 color = Vector4(GetColor(), 1.0f);
        const Vector3 from  = m_box_transformed.GetCenter();
        const Vector3& to   = transform_center;
        m_renderer->DrawDebugLine(from, to, color, color, 0.0f, false);
    }

    const Spartan::Math::Vector3& TransformHandleAxis::GetColor() const
    {
        if (m_is_disabled)
            return m_color_disabled;

        if (m_is_hovered || m_is_editing)
            return m_color_active;

        return m_axis;
    }
}
