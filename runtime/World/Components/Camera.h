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

#pragma once

//= INCLUDES =========================
#include "Component.h"
#include <memory>
#include "../../RHI/RHI_Definitions.h"
#include "../../RHI/RHI_Viewport.h"
#include "../../Math/Matrix.h"
#include "../../Math/Ray.h"
#include "../../Math/Frustum.h"
#include "../../Math/Vector2.h"
#include "../../Math/Rectangle.h"
#include "../../Rendering/Color.h"
//====================================

namespace Spartan
{
    class Entity;
    class Renderable;
    class Renderer;
    class PhysicsBody;

    enum ProjectionType
    {
        Projection_Perspective,
        Projection_Orthographic,
    };

    struct camera_bookmark
    {
        Math::Vector3 position = Math::Vector3::Zero;
        Math::Vector3 rotation = Math::Vector3::Zero;
    };

    class SP_CLASS Camera : public Component
    {
    public:
        Camera(std::weak_ptr<Entity> entity);
        ~Camera() = default;

        // Component
        void OnInitialize() override;
        void OnTick() override;
        void Serialize(FileStream* stream) override;
        void Deserialize(FileStream* stream) override;

        // Matrices
        const Math::Matrix& GetViewMatrix()           const { return m_view; }
        const Math::Matrix& GetProjectionMatrix()     const { return m_projection; }
        const Math::Matrix& GetViewProjectionMatrix() const { return m_view_projection; }

        // Raycasting
        const Math::Ray ComputePickingRay();
        const Math::Ray& GetPickingRay() const { return m_ray; }

        // Picks the nearest entity under the mouse cursor
        void Pick();

        // Converts a world point to a screen point
        Math::Vector2 WorldToScreenCoordinates(const Math::Vector3& position_world) const;

        // Converts a world bounding box to a screen rectangle
        Math::Rectangle WorldToScreenCoordinates(const Math::BoundingBox& bounding_box) const;

        // Converts a screen point to a world point. Z can be 0.0f to 1.0f and it will lerp between the near and far plane.
        Math::Vector3 ScreenToWorldCoordinates(const Math::Vector2& position_screen, const float z) const;
        //=================================================================================================================

        // Aperture
        float GetAperture() const              { return m_aperture; }
        void SetAperture(const float aperture) { m_aperture = aperture; }

        // Shutter speed
        float GetShutterSpeed() const                   { return m_shutter_speed; }
        void SetShutterSpeed(const float shutter_speed) { m_shutter_speed = shutter_speed; }

        // ISO
        float GetIso() const         { return m_iso; }
        void SetIso(const float iso) { m_iso = iso; }

        // Exposure
        float GetEv100()    const { return std::log2(m_aperture / m_shutter_speed * 100.0f / m_iso); }
        float GetExposure() const { return 1.0f / (std::pow(2.0f, GetEv100())); }
        // Planes/projection
        void SetNearPlane(float near_plane);
        void SetFarPlane(float far_plane);
        void SetProjection(ProjectionType projection);
        float GetNearPlane()               const { return m_near_plane; }
        float GetFarPlane()                const { return m_far_plane; }
        ProjectionType GetProjectionType() const { return m_projection_type; }

        // FOV
        float GetFovHorizontalRad() const { return m_fov_horizontal_rad; }
        float GetFovVerticalRad()   const;
        float GetFovHorizontalDeg() const;
        void SetFovHorizontalDeg(float fov);
  
        // Frustum
        bool IsInViewFrustum(std::shared_ptr<Renderable> renderable) const;
        bool IsInViewFrustum(const Math::Vector3& center, const Math::Vector3& extents) const;

        // Bookmarks
        void AddBookmark(camera_bookmark bookmark)               { m_bookmarks.emplace_back(bookmark); };
        const std::vector<camera_bookmark>& GetBookmarks() const { return m_bookmarks; };

        // First person control
        bool GetIsControlEnabled()             const { return m_first_person_control_enabled; }
        void SetIsControlEnalbed(const bool enabled) { m_first_person_control_enabled = enabled; }
        bool IsActivelyControlled()            const { return m_is_controlled_by_keyboard_mouse; }
        void SetPhysicsBodyToControl(PhysicsBody* physics_body);

        // Misc
        bool IsWalking();
        void MakeDirty() { m_is_dirty = true; }
        void SetSelectedEntity(std::shared_ptr<Spartan::Entity> entity) { m_selected_entity = entity; }
        std::shared_ptr<Spartan::Entity> GetSelectedEntity()            { return m_selected_entity.lock(); }

        Math::Matrix ComputeViewMatrix() const;
        Math::Matrix ComputeProjection(const float near_plane, const float far_plane);

        void GoToCameraBookmark(int bookmark_index);
        void FocusOnSelectedEntity();

    private:
        void ProcessInput();
        void ProcessInputFpsControl();
        void ProcessInputLerpToEntity();

        float m_aperture                           = 2.8f;         // Aperture value in f-stop. Controls the amount of light, depth of field and chromatic aberration.
        float m_shutter_speed                      = 1.0f / 60.0f; // Length of time for which the camera shutter is open (sec). Also controls the amount of motion blur.
        float m_iso                                = 500.0f;       // Sensitivity to light.
        float m_fov_horizontal_rad                 = Math::Helper::DegreesToRadians(90.0f);
        float m_near_plane                         = 0.1f;
        float m_far_plane                          = 1000.0f;
        ProjectionType m_projection_type           = Projection_Perspective;
        Math::Matrix m_view                        = Math::Matrix::Identity;
        Math::Matrix m_projection                  = Math::Matrix::Identity;
        Math::Matrix m_view_projection             = Math::Matrix::Identity;
        Math::Vector3 m_position                   = Math::Vector3::Zero;
        Math::Quaternion m_rotation                = Math::Quaternion::Identity;
        bool m_is_dirty                            = false;
        bool m_first_person_control_enabled        = true;
        bool m_is_controlled_by_keyboard_mouse     = false;
        Math::Vector2 m_mouse_last_position        = Math::Vector2::Zero;
        bool m_fps_control_cursor_hidden           = false;
        Math::Vector3 m_movement_speed             = Math::Vector3::Zero;
        float m_movement_scroll_accumulator        = 0.0f;
        Math::Vector2 m_mouse_smoothed             = Math::Vector2::Zero;
        Math::Vector2 m_first_person_rotation      = Math::Vector2::Zero;
        float m_mouse_sensitivity                  = 0.2f;
        float m_mouse_smoothing                    = 0.5f;
        bool m_lerp_to_target_p                    = false;
        bool m_lerp_to_target_r                    = false;
        bool m_lerpt_to_bookmark                   = false;
        int m_target_bookmark_index                = -1;
        float m_lerp_to_target_alpha               = 0.0f;
        float m_lerp_to_target_distance            = 0.0f;
        Math::Vector3 m_lerp_to_target_position    = Math::Vector3::Zero;
        Math::Quaternion m_lerp_to_target_rotation = Math::Quaternion::Identity;
        PhysicsBody* m_physics_body_to_control     = nullptr;
        RHI_Viewport m_last_known_viewport;
        Math::Ray m_ray;
        Math::Frustum m_frustum;
        std::vector<camera_bookmark> m_bookmarks;
        std::weak_ptr<Spartan::Entity> m_selected_entity;
    };
}
