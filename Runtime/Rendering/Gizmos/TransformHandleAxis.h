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

#pragma once

//= INCLUDES ======================
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include "TransformEnums.h"
//=================================

namespace Spartan
{
    class Renderer;
    class Context;
    class Input;
    class Transform;

    struct TransformHandleAxis
    {
        TransformHandleAxis() = default;
        TransformHandleAxis(TransformHandleType type, const Math::Vector3& axis, Context* context);

        void UpdateTransform();
        void ApplyDeltaToTransform(Transform* transform, const TransformHandleSpace space);
        void DrawPrimitives(const Math::Vector3& transform_center) const;
        const Math::Vector3& GetColor() const;

        Math::Vector3 m_axis                = Math::Vector3::One;
        Math::Matrix m_transform            = Math::Matrix::Identity;
        Math::Vector3 m_position            = Math::Vector3::One;
        Math::Quaternion m_rotation         = Math::Quaternion::Identity;
        Math::Vector3 m_scale               = Math::Vector3::One;
        Math::BoundingBox m_box             = Math::BoundingBox::Zero;
        Math::BoundingBox m_box_transformed = Math::BoundingBox::Zero;
        float m_delta                       = 0.0f;
        bool m_is_editing                   = false;
        bool m_is_hovered                   = false;
        bool m_is_disabled                  = false;
        Math::Vector3 m_color_active        = Math::Vector3(1.0f, 1.0f, 0.0f);
        Math::Vector3 m_color_disabled      = Math::Vector3(0.5f, 0.5f, 0.5f);
        TransformHandleType m_type          = TransformHandleType::Unknown;

        Context* m_context      = nullptr;
        Renderer* m_renderer    = nullptr;
        Input* m_input          = nullptr;
    };
}
