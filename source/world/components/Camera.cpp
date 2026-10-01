/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================
#include "pch.h"
#include "Camera.h"
#include "Render.h"
#include "../Entity.h"
#include "../World.h"
#include "../../rendering/Renderer.h"
#include "../../rhi/RHI_Viewport.h"
#include "../../display/Display.h"
#include "../../xr/Xr.h"
SP_WARNINGS_OFF
#include "../io/pugixml.hpp"
#include <sol/sol.hpp>
SP_WARNINGS_ON
//===================================

//= NAMESPACES ===============
using namespace spartan::math;
using namespace std;
//============================

namespace spartan
{
    Camera::Camera(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flags, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_aperture, SetAperture, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_shutter_speed, SetShutterSpeed, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_iso, SetIso, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(
            m_exposure_mode,
            SetExposureMode,
            CameraExposureMode
        );
        SP_REGISTER_ATTRIBUTE_VALUE_SET(
            m_auto_exposure_adaptation_speed,
            SetAutoExposureAdaptationSpeed,
            float
        );
        SP_REGISTER_ATTRIBUTE_VALUE_SET(
            m_auto_exposure_compensation,
            SetAutoExposureCompensation,
            float
        );
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_fov_horizontal_rad, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_near_plane, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_far_plane, float);
        SP_REGISTER_ATTRIBUTE_VALUE_SET(m_projection_type, SetProjection, ProjectionType);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mouse_sensitivity, float);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mouse_smoothing, float);

        // do not override the entity's transform here, otherwise loading a saved scene clobbers the persisted camera position
        SetFlag(CameraFlags::CanBeControlled, true);
        SetFlag(CameraFlags::PhysicalBodyAnimation, true);
    }

    void Camera::Initialize()
    {
        Component::Initialize();
        ComputeMatrices();
    }

    void Camera::RegisterForScripting(sol::state_view state)
    {
        state.new_enum("CameraExposureMode",
            "manual",    CameraExposureMode::manual,
            "automatic", CameraExposureMode::automatic
        );

        state.new_enum("CameraFlags",
            "CanBeControlled",       CameraFlags::CanBeControlled,
            "IsControlled",          CameraFlags::IsControlled,
            "WantsCursorHidden",     CameraFlags::WantsCursorHidden,
            "IsDirty",               CameraFlags::IsDirty,
            "PhysicalBodyAnimation", CameraFlags::PhysicalBodyAnimation,
            "Flashlight",            CameraFlags::Flashlight
        );

        state.new_usertype<Camera>("Camera",
            "GetFovHorizontalDeg",             &Camera::GetFovHorizontalDeg,
            "SetFovHorizontalDeg",             &Camera::SetFovHorizontalDeg,
            "GetNearPlane",                    &Camera::GetNearPlane,
            "GetFarPlane",                     &Camera::GetFarPlane,
            "GetExposureMode",                 &Camera::GetExposureMode,
            "SetExposureMode",                 &Camera::SetExposureMode,
            "GetAutoExposureAdaptationSpeed",  &Camera::GetAutoExposureAdaptationSpeed,
            "SetAutoExposureAdaptationSpeed",  &Camera::SetAutoExposureAdaptationSpeed,
            "GetAutoExposureCompensation",     &Camera::GetAutoExposureCompensation,
            "SetAutoExposureCompensation",     &Camera::SetAutoExposureCompensation,
            "GetFlag",                         &Camera::GetFlag,
            "SetFlag",                         &Camera::SetFlag
        );
    }

    sol::reference Camera::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void Camera::Tick()
    {
        const auto& current_viewport = Renderer::GetViewport();
        if (m_last_known_viewport != current_viewport)
        {
            m_last_known_viewport = current_viewport;
            SetFlag(CameraFlags::IsDirty, true);
        }

        // check if transform changed by comparing matrix directly (avoids quaternion decomposition instability)
        const Matrix& current_matrix = GetEntity()->GetMatrix();
        if (m_matrix_previous != current_matrix)
        {
            m_matrix_previous = current_matrix;
            SetFlag(CameraFlags::IsDirty, true);
        }

        ComputeMatrices();
    }

    void Camera::RefreshMatrices()
    {
        const Matrix& current_matrix = GetEntity()->GetMatrix();
        if (m_matrix_previous != current_matrix)
        {
            m_matrix_previous = current_matrix;
            SetFlag(CameraFlags::IsDirty, true);
        }
        ComputeMatrices();
    }

    CameraSettings Camera::GetSettings() const
    {
        CameraSettings settings;
        settings.flags = m_flags;
        settings.preset = m_preset;
        settings.exposure_mode = m_exposure_mode;
        settings.auto_exposure_adaptation_speed = m_auto_exposure_adaptation_speed;
        settings.auto_exposure_compensation = m_auto_exposure_compensation;
        settings.aperture = m_aperture;
        settings.shutter_speed = m_shutter_speed;
        settings.iso = m_iso;
        settings.fov_horizontal_rad = m_fov_horizontal_rad;
        settings.aspect_ratio_override = m_aspect_ratio_override;
        settings.near_plane = m_near_plane;
        settings.far_plane = m_far_plane;
        settings.projection_type = m_projection_type;
        settings.mouse_sensitivity = m_mouse_sensitivity;
        settings.mouse_smoothing = m_mouse_smoothing;
        settings.flags &= ~(CameraFlags::IsDirty | CameraFlags::IsControlled | CameraFlags::WantsCursorHidden);
        return settings;
    }

    void Camera::ApplySettings(const CameraSettings& settings)
    {
        const auto finite = [](float value, float fallback) { return std::isfinite(value) ? value : fallback; };
        SetAperture(finite(settings.aperture, 5.6f));
        SetShutterSpeed(finite(settings.shutter_speed, 1.0f / 125.0f));
        SetIso(finite(settings.iso, 200.0f));
        SetExposureMode(settings.exposure_mode == CameraExposureMode::automatic ? CameraExposureMode::automatic : CameraExposureMode::manual);
        SetAutoExposureAdaptationSpeed(finite(settings.auto_exposure_adaptation_speed, 1.0f));
        SetAutoExposureCompensation(finite(settings.auto_exposure_compensation, 0.0f));
        SetFovHorizontalDeg(finite(settings.fov_horizontal_rad, math::pi / 2.0f) * math::rad_to_deg);
        m_near_plane = std::max(finite(settings.near_plane, 0.1f), 0.001f);
        m_far_plane = std::max(finite(settings.far_plane, 100000.0f), m_near_plane + 0.001f);
        SetProjection(settings.projection_type == Projection_Orthographic ? Projection_Orthographic : Projection_Perspective);
        SetAspectRatioOverride(std::max(finite(settings.aspect_ratio_override, 0.0f), 0.0f));
        m_mouse_sensitivity = std::max(finite(settings.mouse_sensitivity, 0.2f), 0.0f);
        m_mouse_smoothing = std::clamp(finite(settings.mouse_smoothing, 0.5f), 0.0f, 1.0f);
        m_preset = settings.preset >= CameraPreset::custom && settings.preset <= CameraPreset::cinematic ? settings.preset : CameraPreset::custom;
        m_flags = (settings.flags & ~(CameraFlags::IsControlled | CameraFlags::WantsCursorHidden)) | CameraFlags::IsDirty;
        ComputeMatrices();
    }

    void Camera::CopyFrom(const Component& source)
    {
        SP_ASSERT(source.GetType() == ComponentType::Camera);
        ApplySettings(static_cast<const Camera&>(source).GetSettings());
    }

    void Camera::Save(pugi::xml_node& node)
    {
        const auto settings = GetSettings();
        node.append_attribute("flags") = settings.flags;
        node.append_attribute("preset") = static_cast<int>(settings.preset);
        node.append_attribute("exposure_mode") = static_cast<int>(settings.exposure_mode);
        node.append_attribute("auto_exposure_adaptation_speed") = settings.auto_exposure_adaptation_speed;
        node.append_attribute("auto_exposure_compensation") = settings.auto_exposure_compensation;
        node.append_attribute("aperture") = settings.aperture;
        node.append_attribute("shutter_speed") = settings.shutter_speed;
        node.append_attribute("iso") = settings.iso;
        node.append_attribute("fov_horizontal") = settings.fov_horizontal_rad;
        node.append_attribute("aspect_ratio_override") = settings.aspect_ratio_override;
        node.append_attribute("near_plane") = settings.near_plane;
        node.append_attribute("far_plane") = settings.far_plane;
        node.append_attribute("projection") = static_cast<int>(settings.projection_type);
        node.append_attribute("mouse_sensitivity") = settings.mouse_sensitivity;
        node.append_attribute("mouse_smoothing") = settings.mouse_smoothing;
    }

    void Camera::Load(pugi::xml_node& node)
    {
        CameraSettings settings;
        settings.exposure_mode = CameraExposureMode::manual; // legacy files
        settings.flags = node.attribute("flags").as_uint(settings.flags);
        settings.preset = static_cast<CameraPreset>(node.attribute("preset").as_int(static_cast<int>(settings.preset)));
        settings.exposure_mode = static_cast<CameraExposureMode>(node.attribute("exposure_mode").as_int(static_cast<int>(settings.exposure_mode)));
        settings.auto_exposure_adaptation_speed = node.attribute("auto_exposure_adaptation_speed").as_float(settings.auto_exposure_adaptation_speed);
        settings.auto_exposure_compensation = node.attribute("auto_exposure_compensation").as_float(settings.auto_exposure_compensation);
        settings.aperture = node.attribute("aperture").as_float(settings.aperture);
        settings.shutter_speed = node.attribute("shutter_speed").as_float(settings.shutter_speed);
        settings.iso = node.attribute("iso").as_float(settings.iso);
        settings.fov_horizontal_rad = node.attribute("fov_horizontal").as_float(settings.fov_horizontal_rad);
        settings.aspect_ratio_override = node.attribute("aspect_ratio_override").as_float(settings.aspect_ratio_override);
        settings.near_plane = node.attribute("near_plane").as_float(settings.near_plane);
        settings.far_plane = node.attribute("far_plane").as_float(settings.far_plane);
        settings.projection_type = static_cast<ProjectionType>(node.attribute("projection").as_int(static_cast<int>(settings.projection_type)));
        settings.mouse_sensitivity = node.attribute("mouse_sensitivity").as_float(settings.mouse_sensitivity);
        settings.mouse_smoothing = node.attribute("mouse_smoothing").as_float(settings.mouse_smoothing);
        ApplySettings(settings);
    }

    void Camera::SetProjection(const ProjectionType projection)
    {
        m_projection_type = projection;
        SetFlag(CameraFlags::IsDirty, true);
    }

    void Camera::SetPreset(const CameraPreset preset)
    {
        m_preset = preset;

        // apertures included so each preset lands on its real world ev100,
        // daylight ~15, overcast ~12, golden hour ~10, interior ~6, night ~2, cinematic ~7
        switch (preset)
        {
        case CameraPreset::daylight:
            m_aperture           = 11.0f;
            m_shutter_speed      = 1.0f / 250.0f;
            m_iso                = 100.0f;
            break;
        case CameraPreset::overcast:
            m_aperture           = 8.0f;
            m_shutter_speed      = 1.0f / 125.0f;
            m_iso                = 200.0f;
            break;
        case CameraPreset::golden_hour:
            m_aperture           = 5.6f;
            m_shutter_speed      = 1.0f / 125.0f;
            m_iso                = 400.0f;
            break;
        case CameraPreset::interior:
            m_aperture           = 2.8f;
            m_shutter_speed      = 1.0f / 60.0f;
            m_iso                = 800.0f;
            break;
        case CameraPreset::night:
            m_aperture           = 1.4f;
            m_shutter_speed      = 1.0f / 30.0f;
            m_iso                = 1600.0f;
            break;
        case CameraPreset::cinematic:
            m_aperture           = 2.0f;
            m_shutter_speed      = 1.0f / 48.0f;
            m_iso                = 400.0f;
            break;
        case CameraPreset::custom:
            return;
        }

        SetFlag(CameraFlags::IsDirty, true);
    }

    float Camera::GetFovHorizontalDeg() const
    {
        return m_fov_horizontal_rad * math::rad_to_deg;
    }

    float Camera::GetFovVerticalRad() const
    {
        return
            2.0f *
            atan(
                tan(m_fov_horizontal_rad / 2.0f) /
                GetAspectRatio()
            );
    }

    void Camera::SetFovHorizontalDeg(const float fov)
    {
        m_fov_horizontal_rad = fov * math::deg_to_rad;
        SetFlag(CameraFlags::IsDirty, true);
    }

    float Camera::GetAspectRatio() const
    {
        return m_aspect_ratio_override > 0.0f
            ? m_aspect_ratio_override
            : Renderer::GetViewport().GetAspectRatio();
    }

    bool Camera::IsInViewFrustum(const BoundingBox& bounding_box) const
    {
        if (bounding_box.IsInfinite())
        {
            return true;
        }

        const Vector3 center  = bounding_box.GetCenter();
        const Vector3 extents = bounding_box.GetExtents();
        if (center.IsNaN() || extents.IsNaN())
        {
            return false;
        }

        return m_frustum.IsVisible(center, extents);
    }

    bool Camera::IsInViewFrustum(shared_ptr<Render> render) const
    {
        const BoundingBox& box = render->GetBoundingBox();
        return IsInViewFrustum(box);
    }

    Ray Camera::ComputeRay(const Vector2& screen_position) const
    {
        Ray ray;
        ray.m_origin = GetEntity()->GetPosition();
        ray.m_direction = ScreenToWorldCoordinates(screen_position, 1.0f);
        return ray;
    }

    void Camera::WorldToScreenCoordinates(const Vector3& position_world, Vector2& position_screen) const
    {
        const Vector4 position_clip = Vector4(position_world, 1.0f) * m_view_projection_non_reverse_z;

        // convert clip space position to screen space position
        const RHI_Viewport& viewport = Renderer::GetViewport();
        float viewport_half_width    = viewport.width  * 0.5f;
        float viewport_half_height   = viewport.height * 0.5f;
        position_screen.x            = (position_clip.x / position_clip.w) *  viewport_half_width  + viewport_half_width;
        position_screen.y            = (position_clip.y / position_clip.w) * -viewport_half_height + viewport_half_height;
    }

    Rectangle Camera::WorldToScreenCoordinates(const BoundingBox& bounding_box) const
    {
        const Vector3& min = bounding_box.GetMin();
        const Vector3& max = bounding_box.GetMax();

        Vector3 corners[8];
        corners[0] = min;
        corners[1] = Vector3(max.x, min.y, min.z);
        corners[2] = Vector3(min.x, max.y, min.z);
        corners[3] = Vector3(max.x, max.y, min.z);
        corners[4] = Vector3(min.x, min.y, max.z);
        corners[5] = Vector3(max.x, min.y, max.z);
        corners[6] = Vector3(min.x, max.y, max.z);
        corners[7] = max;

        math::Rectangle rectangle_screen_Space;
        Vector2 position_screen_space;
        for (Vector3& corner : corners)
        {
            WorldToScreenCoordinates(corner, position_screen_space);
            rectangle_screen_Space.Merge(position_screen_space);
        }

        return rectangle_screen_Space;
    }

    Vector3 Camera::ScreenToWorldCoordinates(const Vector2& position_screen, const float z) const
    {
        Vector3 position_clip;
        const RHI_Viewport& viewport = Renderer::GetViewport();
        position_clip.x              = (position_screen.x / viewport.width) * 2.0f - 1.0f;
        position_clip.y              = (position_screen.y / viewport.height) * -2.0f + 1.0f;
        position_clip.z              = clamp(z, 0.0f, 1.0f);

        // compute world space position
        Matrix view_projection_inverted = m_view_projection_non_reverse_z.Inverted();
        Vector4 position_world          = Vector4(position_clip, 1.0f) * view_projection_inverted;

        return Vector3(position_world) / position_world.w;
    }

    void Camera::ComputeMatrices()
    {
        if (!GetFlag(CameraFlags::IsDirty))
        {
            return;
        }

        m_view                          = UpdateViewMatrix();
        m_projection                    = ComputeProjection(m_far_plane, m_near_plane);
        m_projection_non_reverse_z      = ComputeProjection(m_near_plane, m_far_plane);
        m_view_projection               = m_view * m_projection;
        m_view_projection_non_reverse_z = m_view * m_projection_non_reverse_z;
        m_frustum                       = Frustum(GetViewMatrix(), GetProjectionMatrix());
        SetFlag(CameraFlags::IsDirty, false);
    }

    void Camera::SetFlag(const CameraFlags flag, const bool enable)
    {
        bool flag_present = m_flags & flag;

        if (enable && !flag_present)
        {
            m_flags |= static_cast<uint32_t>(flag);
        }
        else if (!enable && flag_present)
        {
            m_flags  &= ~static_cast<uint32_t>(flag);
        }
    }

    Matrix Camera::UpdateViewMatrix() const
    {
        // extract basis vectors directly from world matrix to avoid quaternion decomposition instability
        // row-major layout: row 0 = right (X), row 1 = up (Y), row 2 = forward (Z), row 3 = translation
        const Matrix& m = GetEntity()->GetMatrix();
        
        Vector3 position = Vector3(m.m30, m.m31, m.m32);
        Vector3 forward  = Vector3(m.m20, m.m21, m.m22).Normalized();
        Vector3 up       = Vector3(m.m10, m.m11, m.m12).Normalized();

        // compute view matrix
        return Matrix::CreateLookToLH(position, forward, up);
    }

    Matrix Camera::ComputeProjection(const float near_plane, const float far_plane)
    {
        if (m_projection_type == Projection_Perspective)
        {
            return Matrix::CreatePerspectiveFieldOfViewLH(GetFovVerticalRad(), GetAspectRatio(), near_plane, far_plane);
        }
        else if (m_projection_type == Projection_Orthographic)
        {
            return Matrix::CreateOrthographicLH(Renderer::GetViewport().width, Renderer::GetViewport().height, near_plane, far_plane);
        }

        return Matrix::Identity;
    }
}
