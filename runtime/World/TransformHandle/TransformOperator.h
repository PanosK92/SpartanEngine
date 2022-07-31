/*
Copyright(c) 2016-2022 Panos Karabelas

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

//= INCLUDES ===========================
#include "TransformEnums.h"
#include "TransformOperatorAxis.h"
#include <memory>
#include "../../Rendering/Model.h"
//======================================

namespace Spartan
{
    //= FWD DECLARATIONS ==
    class Renderer;
    class Context;
    class RHI_VertexBuffer;
    class RHI_IndexBuffer;
    class Entity;
    class Input;
    class Camera;
    class Model;
    //=====================

    class SPARTAN_CLASS TransformOperator
    {
    public:
        TransformOperator(Context* context, const TransformHandleType transform_handle_type);
        ~TransformOperator() = default;

        void Tick(TransformHandleSpace space, Entity* entity, Camera* camera, float handle_size);
        const Math::Matrix& GetTransform(const Math::Vector3& axis) const;
        const Math::Vector3& GetColor(const Math::Vector3& axis) const;
        const RHI_VertexBuffer* GetVertexBuffer();
        const RHI_IndexBuffer* GetIndexBuffer();
        bool HasModel() const { return m_axis_model != nullptr; }
        bool IsEditing() const;
        bool IsHovered() const;

    private:
        void SnapToTransform(TransformHandleSpace space, Entity* entity, Camera* camera, float handle_size);

    protected:
        // Test if the mouse ray intersects any of the handles.
        virtual void InteresectionTest(const Math::Ray& mouse_ray) = 0;
        // Compute transformation (position, rotation or scale) delta.
        virtual void ComputeDelta(const Math::Ray& mouse_ray, const Camera* camera) = 0;
        // Map the transformation delta to the entity's transform.
        virtual void MapToTransform(Transform* transform, const TransformHandleSpace space) = 0;

        TransformOperatorAxis m_handle_x;
        TransformOperatorAxis m_handle_y;
        TransformOperatorAxis m_handle_z;
        TransformOperatorAxis m_handle_xyz;
        bool m_handle_x_intersected           = false;
        bool m_handle_y_intersected           = false;
        bool m_handle_z_intersected           = false;
        bool m_handle_xyz_intersected         = false;
        TransformHandleType m_type            = TransformHandleType::Unknown;
        bool m_offset_handle_axes_from_center = true;
        float m_offset_handle_from_center     = 0.0f;
        Math::Vector3 m_position              = Math::Vector3::Zero;
        Math::Vector3 m_rotation              = Math::Vector3::Zero;
        Math::Vector3 m_scale                 = Math::Vector3::Zero;
        Context* m_context                    = nullptr;
        Renderer* m_renderer                  = nullptr;
        Input* m_input                        = nullptr;
        std::unique_ptr<Model> m_axis_model;
    };
}
