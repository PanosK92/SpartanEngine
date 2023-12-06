/*
Copyright(c) 2016-2023 Panos Karabelas

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

//= INCLUDES =========================
#include "pch.h"
#include "ReflectionProbe.h"
#include "Renderable.h"
#include "../../RHI/RHI_TextureCube.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../Rendering/Renderer.h"
#include "../../IO/FileStream.h"
#include "../../RHI/RHI_Device.h"
#include "../Entity.h"
//====================================

//= NAMESPACES ===============
using namespace Spartan::Math;
using namespace std;
//============================

namespace Spartan
{
    ReflectionProbe ::ReflectionProbe(weak_ptr<Entity> entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_resolution, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_extents, Vector3);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_update_interval_frames, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_update_face_count, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_plane_near, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_plane_far, float);

        CreateTextures();
    }

    void ReflectionProbe::OnTick()
    {
        // Determine if it's time to update
        if (m_frames_since_last_update >= m_update_interval_frames)
        {
            if (!m_first_update)
            {
                m_update_face_start_index += m_update_face_count;

                if (m_update_face_start_index + m_update_face_count > 6)
                {
                    m_update_face_start_index = 0;
                }
            }
            else
            {
                m_update_face_start_index = 0;
            }
            
            m_first_update             = false;
            m_frames_since_last_update = 0;
            m_needs_to_update          = true;
        }
        else
        {
            m_needs_to_update = false;
        }

        m_frames_since_last_update++;

        if (!m_needs_to_update)
            return;

        ComputeProjectionMatrix();

        if (m_entity_ptr->HasPositionChangedThisFrame())
        { 
            // Compute view for each side of the cube map
            const Vector3 position = m_entity_ptr->GetPosition();
            m_matrix_view[0] = Matrix::CreateLookAtLH(position, position + Vector3::Right,    Vector3::Up);       // x+
            m_matrix_view[1] = Matrix::CreateLookAtLH(position, position + Vector3::Left,     Vector3::Up);       // x-
            m_matrix_view[2] = Matrix::CreateLookAtLH(position, position + Vector3::Up,       Vector3::Backward); // y+
            m_matrix_view[3] = Matrix::CreateLookAtLH(position, position + Vector3::Down,     Vector3::Forward);  // y-
            m_matrix_view[4] = Matrix::CreateLookAtLH(position, position + Vector3::Forward,  Vector3::Up);       // z+
            m_matrix_view[5] = Matrix::CreateLookAtLH(position, position + Vector3::Backward, Vector3::Up);       // z-

            m_aabb = BoundingBox(position - m_extents, position + m_extents);
        }

        if (m_entity_ptr->HasPositionChangedThisFrame())
        {
            // Compute frustum
            for (uint32_t i = 0; i < m_texture_color->GetArrayLength(); i++)
            {
                const float far_plane = m_plane_near; // reverse-z
                m_frustum[i] = Frustum(m_matrix_view[i], m_matrix_projection, far_plane);
            }
        }
    }

    void ReflectionProbe::Serialize(FileStream* stream)
    {
        stream->Write(m_resolution);
        stream->Write(m_extents);
        stream->Write(m_update_interval_frames);
        stream->Write(m_update_face_count);
        stream->Write(m_plane_near);
        stream->Write(m_plane_far);
    }

    void ReflectionProbe::Deserialize(FileStream* stream)
    {
        stream->Read(&m_resolution);
        stream->Read(&m_extents);
        stream->Read(&m_update_interval_frames);
        stream->Read(&m_update_face_count);
        stream->Read(&m_plane_near);
        stream->Read(&m_plane_far);
    }

    bool ReflectionProbe::IsInViewFrustum(shared_ptr<Renderable> renderable, uint32_t index) const
    {
        const auto box     = renderable->GetBoundingBoxInstance();
        const auto center  = box.GetCenter();
        const auto extents = box.GetExtents();

        return m_frustum[index].IsVisible(center, extents);
    }

    void ReflectionProbe::SetResolution(const uint32_t resolution)
    {
        uint32_t new_value = Math::Helper::Clamp<uint32_t>(resolution, 16, RHI_Device::PropertyGetMaxTextureCubeDimension());

        if (m_resolution == new_value)
            return;

        m_resolution = new_value;

        CreateTextures();
    }

    void ReflectionProbe::SetExtents(const Math::Vector3& extents)
    {
        m_extents = extents;
    }

    void ReflectionProbe::SetUpdateIntervalFrames(const uint32_t update_interval_frame)
    {
        m_update_interval_frames = Math::Helper::Clamp<uint32_t>(update_interval_frame, 0, 128);
    }

    void ReflectionProbe::SetUpdateFaceCount(const uint32_t update_face_count)
    {
        m_update_face_count = Math::Helper::Clamp<uint32_t>(update_face_count, 1, 6);
    }

    void ReflectionProbe::SetNearPlane(const float near_plane)
    {
        float new_value = Math::Helper::Clamp<float>(near_plane, 0.1f, 1000.0f);

        if (m_plane_near == new_value)
            return;

        m_plane_near = new_value;

        ComputeProjectionMatrix();
        ComputeFrustums();
    }

    void ReflectionProbe::SetFarPlane(const float far_plane)
    {
        float new_value = Math::Helper::Clamp<float>(far_plane, 0.1f, 1000.0f);

        if (m_plane_far == new_value)
            return;

        m_plane_far = new_value;

        ComputeProjectionMatrix();
        ComputeFrustums();
    }

    void ReflectionProbe::CreateTextures()
    {
        m_texture_color = make_unique<RHI_TextureCube>(m_resolution, m_resolution, RHI_Format::R8G8B8A8_Unorm, RHI_Texture_Rtv | RHI_Texture_Srv, "reflection_probe_color");
        m_texture_depth = make_unique<RHI_Texture2D>(m_resolution, m_resolution, 1, RHI_Format::D32_Float, RHI_Texture_Rtv | RHI_Texture_Srv, "reflection_probe_depth");
    }

    void ReflectionProbe::ComputeProjectionMatrix()
    {
        const float near_plane   = m_plane_far;  // reverse-z
        const float far_plane    = m_plane_near; // reverse-z
        const float fov          = Math::Helper::PI_DIV_2; // 90 degrees
        const float aspect_ratio = 1.0f;
        m_matrix_projection      = Matrix::CreatePerspectiveFieldOfViewLH(fov, aspect_ratio, near_plane, far_plane);
    }

    void ReflectionProbe::ComputeFrustums()
    {
        for (uint32_t i = 0; i < m_texture_color->GetArrayLength(); i++)
        {
            const float far_plane = m_plane_near; // reverse-z
            m_frustum[i] = Frustum(m_matrix_view[i], m_matrix_projection, far_plane);
        }
    }

}
