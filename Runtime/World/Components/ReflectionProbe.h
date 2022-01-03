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

//= INCLUDES ==========
#include "IComponent.h"
//=====================

namespace Spartan
{
    //= FWD DECLARATIONS =
    class Renderable;
    //====================

    class SPARTAN_CLASS ReflectionProbe : public IComponent
    {
    public:
        ReflectionProbe(Context* context, Entity* entity, uint64_t id = 0);
        ~ReflectionProbe() = default;

        //= COMPONENT ================================
        void OnTick(double delta_time) override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;
        //============================================

        // Returns true if the entity (renderable) is within the view frustum of a particular face (index) of the probe.
        bool IsInViewFrustum(Renderable* renderable, uint32_t index) const;

        // Properties
        RHI_Texture* GetColorTexture()                    { return m_texture_color.get(); }
        RHI_Texture* GetDepthTexture()                    { return m_texture_depth.get(); }
        Math::Matrix& GetViewMatrix(const uint32_t index) { return m_matrix_view[index]; }
        Math::Matrix& GetProjectionMatrix()               { return m_matrix_projection; }

        uint32_t GetResolution() const { return m_resolution; }
        void SetResolution(const uint32_t resolution);

        const Math::Vector3& GetExtents() const { return m_extents; }
        void SetExtents(const Math::Vector3& extents);

        uint32_t GetUpdateIntervalFrames() const { return m_update_interval_frames; }
        void SetUpdateIntervalFrames(const uint32_t update_interval_frame);

        uint32_t GetUpdateFaceCount() const { return m_update_face_count; }
        void SetUpdateFaceCount(const uint32_t update_face_count);

        float GetNearPlane() const { return m_plane_near; }
        void SetNearPlane(const float near_plane);

        float GetFarPlane() const { return m_plane_far; }
        void SetFarPlane(const float far_plane);

        bool GetNeedsToUpdate() const { return m_needs_to_update; }
        uint32_t GetUpdateFaceStartIndex() const { return m_update_face_start_index; }

        const Math::BoundingBox& GetAabb() const { return m_aabb; }

    private:
        void CreateTextures();
        void ComputeProjectionMatrix();
        void ComputeFrustums();

        // The resolution of the faces of the cubemap.
        uint32_t m_resolution = 512;

        // Defines the are within which all rendered objects will received parallax corrected cubemap reflections.
        Math::Vector3 m_extents  = Math::Vector3(4.0f, 2.0f, 4.0f);
        Math::BoundingBox m_aabb = Math::BoundingBox::Zero;

        // How often should the reflection update.
        uint32_t m_update_interval_frames = 0;

        // How many faces of the cubemap to update per update.
        uint32_t m_update_face_count = 6;

        // Near and far planes used when rendering the probe.
        float m_plane_near = 0.3f;
        float m_plane_far  = 1000.0f;

        // Matrices and frustums
        std::array<Math::Matrix, 6> m_matrix_view;
        Math::Matrix m_matrix_projection;
        std::array< Math::Frustum, 6> m_frustum;

        // Updating
        uint32_t m_frames_since_last_update = 0;
        uint32_t m_update_face_start_index  = 0;
        bool m_needs_to_update              = false;
        bool m_first_update                 = true;

        // Textures
        std::shared_ptr<RHI_Texture> m_texture_color;
        std::shared_ptr<RHI_Texture> m_texture_depth;

        // Dirty checks
        bool m_previous_reverse_z = false;
    };
}
