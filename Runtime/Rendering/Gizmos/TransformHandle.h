/*
Copyright(c) 2016-2020 Panos Karabelas

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

//= INCLUDES ==============================
#include "Transform_Enums.h"
#include "../../Math/Matrix.h"
#include "../../Math/BoundingBox.h"
#include <memory>
#include "../../Core/Spartan_Definitions.h"
//=========================================

namespace Spartan
{
    class Transform_Gizmo;
    class Renderer;
    class Context;
    class RHI_VertexBuffer;
    class RHI_IndexBuffer;
    class Entity;
    class Model;
    class Input;
    class Transform;
    class Camera;

    struct TransformHandleAxis
    {
        TransformHandleAxis() = default;
        TransformHandleAxis(const Math::Vector3& axis, Context* context)
        {
            this->axis    = axis;
            m_context   = context;
        }

        void UpdateTransform()
        {
            transform        = Math::Matrix(position, rotation, scale);
            box_transformed    = box.Transform(transform);
        }

        void UpdateInput(TransformHandle_Type type, Transform* transform, Input* input);
        void DrawExtra(Renderer* renderer, const Math::Vector3& transform_center) const;

        const Math::Vector3& GetColor() const
        {
            if (isDisabled)
                return m_color_disabled;

            if (isHovered || isEditing)
                return m_color_active;

            return axis;
        }

        Math::Vector3 axis                  = Math::Vector3::One;
        Math::Matrix transform              = Math::Matrix::Identity;
        Math::Vector3 position              = Math::Vector3::One;
        Math::Quaternion rotation           = Math::Quaternion::Identity;
        Math::Vector3 scale                 = Math::Vector3::One;
        Math::BoundingBox box               = Math::BoundingBox::Zero;
        Math::BoundingBox box_transformed   = Math::BoundingBox::Zero;
        float delta                         = 0.0f;
        bool isEditing                      = false;
        bool isHovered                      = false;
        bool isDisabled                     = false;
        Math::Vector3 m_color_active        = Math::Vector3(1.0f, 1.0f, 0.0f);
        Math::Vector3 m_color_disabled        = Math::Vector3(0.5f, 0.5f, 0.5f);
        Context* m_context                  = nullptr;
    };

    class SPARTAN_CLASS TransformHandle
    {
    public:
        TransformHandle() = default;
        ~TransformHandle() = default;

        void Initialize(TransformHandle_Type type, Context* context);
        bool Update(TransformHandle_Space space, Entity* entity, Camera* camera, float handle_size, float handle_speed);
        const Math::Matrix& GetTransform(const Math::Vector3& axis) const;
        const Math::Vector3& GetColor(const Math::Vector3& axis) const;
        const RHI_VertexBuffer* GetVertexBuffer() const;
        const RHI_IndexBuffer* GetIndexBuffer() const;
    
    private:
        void SnapToTransform(TransformHandle_Space space, Entity* entity, Camera* camera, float handle_size);

        TransformHandleAxis m_handle_x;
        TransformHandleAxis m_handle_y;
        TransformHandleAxis m_handle_z;
        TransformHandleAxis m_handle_xyz;
        Math::Vector3 m_ray_previous;
        Math::Vector3 m_ray_current;
        std::unique_ptr<Model> m_model;
        TransformHandle_Type m_type;
        Context* m_context            = nullptr;
        Renderer* m_renderer        = nullptr;
        Input* m_input                = nullptr;
    };
}
