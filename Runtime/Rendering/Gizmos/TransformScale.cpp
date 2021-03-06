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

//= INCLUDES ========================
#include "Spartan.h"
#include "TransformScale.h"
#include "../../Utilities/Geometry.h"
//===================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    TransformScale::TransformScale(Context* context) : TransformHandle(context, TransformHandleType::Scale)
    {
        // Create model
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        Utility::Geometry::CreateCube(&vertices, &indices);

        m_axis_model = make_unique<Model>(m_context);
        m_axis_model->AppendGeometry(indices, vertices);
        m_axis_model->UpdateGeometry();

        // Create an axis for each axis of control and fourth axis which control all of them
        m_handle_x      = TransformHandleAxis(m_type, Vector3::Right, m_context);
        m_handle_y      = TransformHandleAxis(m_type, Vector3::Up, m_context);
        m_handle_z      = TransformHandleAxis(m_type, Vector3::Forward, m_context);
        m_handle_xyz    = TransformHandleAxis(m_type, Vector3::One, m_context);

        // Create bounding boxes for the handles, based on the vertices used
        m_handle_x.m_box    = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
        m_handle_y.m_box    = m_handle_x.m_box;
        m_handle_z.m_box    = m_handle_x.m_box;
        m_handle_xyz.m_box  = m_handle_x.m_box;

        m_offset_handle_axes_from_center = true;
    }

    void TransformScale::InteresectionTest(const Math::Ray& camera_to_mouse)
    {
        // Test if the ray intersects any of the handles
        m_handle_x_intersected      = camera_to_mouse.HitDistance(m_handle_x.m_box_transformed)   != Math::Helper::INFINITY_;
        m_handle_y_intersected      = camera_to_mouse.HitDistance(m_handle_y.m_box_transformed)   != Math::Helper::INFINITY_;
        m_handle_z_intersected      = camera_to_mouse.HitDistance(m_handle_z.m_box_transformed)   != Math::Helper::INFINITY_;
        m_handle_xyz_intersected    = camera_to_mouse.HitDistance(m_handle_xyz.m_box_transformed) != Math::Helper::INFINITY_;
    }
}
