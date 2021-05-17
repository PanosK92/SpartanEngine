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

//= INCLUDES ==============================
#include "TransformEnums.h"
#include "TransformHandleAxis.h"
#include <memory>
#include "../Model.h"
#include "../../Core/Spartan_Definitions.h"
//=========================================

namespace Spartan
{
    class Renderer;
    class Context;
    class RHI_VertexBuffer;
    class RHI_IndexBuffer;
    class Entity;
    class Input;
    class Camera;

    class SPARTAN_CLASS TransformHandle
    {
    public:
        TransformHandle(Context* context, const TransformHandleType transform_handle_type);
        ~TransformHandle() = default;

        bool Tick(TransformHandleSpace space, Entity* entity, Camera* camera, float handle_size, float handle_speed);
        const Math::Matrix& GetTransform(const Math::Vector3& axis) const;
        const Math::Vector3& GetColor(const Math::Vector3& axis) const;
        const RHI_VertexBuffer* GetVertexBuffer();
        const RHI_IndexBuffer* GetIndexBuffer();
        bool HasModel() const { return m_axis_model != nullptr; }
        bool IsEditing() const;
        bool IsHovered() const;

    private:
        void ReflectEntityTransform(TransformHandleSpace space, Entity* entity, Camera* camera, float handle_size);
        void UpdateHandleAxesState();
        void UpdateHandleAxesMouseDelta(Camera* camera, const Math::Vector3& ray_end, const float handle_speed);

    protected:
        // Test if the ray intersects any of the handles
        virtual void InteresectionTest(const Math::Ray& camera_to_pixel) = 0;

        TransformHandleAxis m_handle_x;
        TransformHandleAxis m_handle_y;
        TransformHandleAxis m_handle_z;
        TransformHandleAxis m_handle_xyz;
        bool m_handle_x_intersected             = false;
        bool m_handle_y_intersected             = false;
        bool m_handle_z_intersected             = false;
        bool m_handle_xyz_intersected           = false;
        TransformHandleType m_type              = TransformHandleType::Unknown;
        Math::Vector3 m_ray_previous            = Math::Vector3::Zero;
        Math::Vector3 m_ray_current             = Math::Vector3::Zero;
        bool m_offset_handle_axes_from_center   = true;
        Context* m_context                      = nullptr;
        Renderer* m_renderer                    = nullptr;
        Input* m_input                          = nullptr;
        std::unique_ptr<Model> m_axis_model;
    };
}
