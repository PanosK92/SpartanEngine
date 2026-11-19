/*
Copyright(c) 2015-2026 Panos Karabelas
Licensed under the Spartan Engine License. See license.md in the repository root.
https://github.com/PanosK92/SpartanEngine/blob/master/license.md
Commercial use requires written permission and negotiated payment terms.
*/

//= INCLUDES ========================================
#include "pch.h"
#include <sol/sol.hpp>
#include "Scripting.h"
#include "ScriptingInternal.h"
#include "visual/VisualApiReflection.h"
#include "../world/Entity.h"
#include "../world/components/AudioSource.h"
#include "../world/components/Animator.h"
#include "../world/components/Camera.h"
#include "../world/components/Light.h"
#include "../world/components/Navigation.h"
#include "../world/components/NetworkSync.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Physics.h"
#include "../world/components/Ragdoll.h"
#include "../world/components/Render.h"
#include "../world/components/Script.h"
#include "../world/components/Spline.h"
#include "../world/components/Text3D.h"
//===================================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    namespace
    {
        void register_audio_source(sol::state_view State)
        {
            visual::usertype<AudioSource>(State, "AudioSource",
                sol::base_classes,              sol::bases<Component>(),
                "SetAudioClip",                 &AudioSource::SetAudioClip,
                "IsPlaying",                    &AudioSource::IsPlaying,
                "PlayClip",                     &AudioSource::PlayClip,
                "StopClip",                     &AudioSource::StopClip,
                "GetAudioClipName",             &AudioSource::GetAudioClipName,
                "GetMute",                      &AudioSource::GetMute,
                "SetMute",                      &AudioSource::SetMute,
                "IsSynthesisMode",              &AudioSource::IsSynthesisMode,
                "SetSynthesisMode",             &AudioSource::SetSynthesisMode,
                "StartSynthesis",               &AudioSource::StartSynthesis,
                "StopSynthesis",                &AudioSource::StopSynthesis,
                "GetPitch",                     &AudioSource::GetPitch,
                "SetPitch",                     &AudioSource::SetPitch,
                "GetLoop",                      &AudioSource::GetLoop,
                "SetLoop",                      &AudioSource::SetLoop,
                "GetVolume",                    &AudioSource::GetVolume,
                "SetVolume",                    &AudioSource::SetVolume,
                "GetPlayOnStart",               &AudioSource::GetPlayOnStart,
                "SetPlayOnStart",               &AudioSource::SetPlayOnStart,
                "GetIs3d",                      &AudioSource::GetIs3d,
                "SetIs3d",                      &AudioSource::SetIs3d,
                "GetAmbient",                   &AudioSource::GetAmbient,
                "SetAmbient",                   &AudioSource::SetAmbient,
                "GetAmbientGain",               &AudioSource::GetAmbientGain,
                "GetAmbientProfile",            &AudioSource::GetAmbientProfile,
                "SetAmbientProfile",            &AudioSource::SetAmbientProfile,
                "GetHabitatGain",               &AudioSource::GetHabitatGain,
                "GetReverbEnabled",             &AudioSource::GetReverbEnabled,
                "SetReverbEnabled",             &AudioSource::SetReverbEnabled,
                "GetReverbRoomSize",            &AudioSource::GetReverbRoomSize,
                "SetReverbRoomSize",            &AudioSource::SetReverbRoomSize,
                "GetReverbDecay",               &AudioSource::GetReverbDecay,
                "SetReverbDecay",               &AudioSource::SetReverbDecay,
                "GetReverbWet",                 &AudioSource::GetReverbWet,
                "SetReverbWet",                 &AudioSource::SetReverbWet
            );
        }

        void register_spline(sol::state_view state)
        {
            visual::usertype<Spline>(state, "Spline",
                "GetPoint",              &Spline::GetPoint,
                "GetTangent",            &Spline::GetTangent,
                "GetLength",             [](Spline* self) { return self->GetLength(); },
                "GetTAtDistance",        [](Spline* self, float distance) { return self->GetTAtDistance(distance); },
                "GetControlPointCount",  &Spline::GetControlPointCount,
                "GetClosedLoop",         &Spline::GetClosedLoop,
                "GetRoadWidth",          &Spline::GetRoadWidth,
                "GetCurveAlpha",         &Spline::GetCurveAlpha,
                "SetCurveAlpha",         &Spline::SetCurveAlpha,
                "AddControlPoint",       &Spline::AddControlPoint,
                "RemoveLastControlPoint", &Spline::RemoveLastControlPoint,
                "ResampleControlPoints", &Spline::ResampleControlPoints,
                "SimplifyControlPoints", &Spline::SimplifyControlPoints,
                "GenerateRoadMesh",      &Spline::GenerateRoadMesh,
                "ClearRoadMesh",         &Spline::ClearRoadMesh
            );
        }

        void register_text_3d(sol::state_view state)
        {
            visual::enumeration<Text3DAlignment>(state, "Text3DAlignment", {
                {"Left",   Text3DAlignment::Left},
                {"Center", Text3DAlignment::Center},
                {"Right",  Text3DAlignment::Right}
            });

            visual::usertype<Text3D>(state, "Text3D",
                sol::base_classes,  sol::bases<Component>(),
                "GetText",          &Text3D::GetText,
                "SetText",          &Text3D::SetText,
                "GetFontPath",      &Text3D::GetFontPath,
                "SetFontPath",      &Text3D::SetFontPath,
                "GetSize",          &Text3D::GetSize,
                "SetSize",          &Text3D::SetSize,
                "GetDepth",         &Text3D::GetDepth,
                "SetDepth",         &Text3D::SetDepth,
                "GetWeight",        &Text3D::GetWeight,
                "SetWeight",        &Text3D::SetWeight,
                "GetLetterSpacing", &Text3D::GetLetterSpacing,
                "SetLetterSpacing", &Text3D::SetLetterSpacing,
                "GetLineSpacing",   &Text3D::GetLineSpacing,
                "SetLineSpacing",   &Text3D::SetLineSpacing,
                "GetResolution",    &Text3D::GetResolution,
                "SetResolution",    &Text3D::SetResolution,
                "GetAlignment",     &Text3D::GetAlignment,
                "SetAlignment",     &Text3D::SetAlignment,
                "GenerateMesh",     &Text3D::GenerateMesh
            );
        }

        void register_navigation(sol::state_view state)
        {
            visual::usertype<Navigation>(state, "Navigation",
                sol::base_classes, sol::bases<Component>(),
                "GetEnabled",    &Navigation::GetEnabled,
                "SetEnabled",    &Navigation::SetEnabled,
                "GetDebugDraw",  &Navigation::GetDebugDraw,
                "SetDebugDraw",  &Navigation::SetDebugDraw,
                "Refresh",       &Navigation::Refresh,
                "RemoveAgent",   &Navigation::RemoveAgent,
                "MoveAgent",     &Navigation::MoveAgent,
                "WanderAgent",   &Navigation::WanderAgent,
                "SetAgentSpeed", &Navigation::SetAgentSpeed,
                // agent id, or -1 when there is no navmesh under the position yet
                "AddAgent",      [](Navigation& self, const Vector3& position, sol::optional<float> speed) { return self.AddAgent(position, speed.value_or(3.5f)); },
                // the nearest walkable point, or nil
                "ProjectPoint",  [](Navigation& self, Vector3 position) -> sol::optional<Vector3>
                {
                    if (!self.ProjectPoint(position))
                    {
                        return sol::nullopt;
                    }
                    return position;
                },
                "GetAgentPosition", [](Navigation& self, int agent) -> sol::optional<Vector3>
                {
                    Vector3 position, velocity;
                    if (!self.GetAgent(agent, position, velocity))
                    {
                        return sol::nullopt;
                    }
                    return position;
                },
                "GetAgentVelocity", [](Navigation& self, int agent) -> sol::optional<Vector3>
                {
                    Vector3 position, velocity;
                    if (!self.GetAgent(agent, position, velocity))
                    {
                        return sol::nullopt;
                    }
                    return velocity;
                },
                // the corners of the shortest path as a list of Vector3, or nil when unreachable
                "FindPath",      [](Navigation& self, const Vector3& from, const Vector3& to) -> sol::object
                {
                    vector<Vector3> points;
                    if (!self.FindPath(from, to, points))
                    {
                        return sol::lua_nil;
                    }
                    sol::state_view lua = get_state();
                    sol::table result   = lua.create_table(static_cast<int>(points.size()), 0);
                    for (size_t i = 0; i < points.size(); i++)
                    {
                        result[i + 1] = points[i];
                    }
                    return result;
                }
            );
        }

        void register_network_sync(sol::state_view state)
        {
            visual::usertype<NetworkSync>(state, "NetworkSync",
                sol::base_classes, sol::bases<Component>(),
                "GetNetworkId",   &NetworkSync::GetNetworkId,
                "SetNetworkId",   &NetworkSync::SetNetworkId,
                "GetOwner",       &NetworkSync::GetOwner,
                "SetOwner",       &NetworkSync::SetOwner,
                "GetInterpolate", &NetworkSync::GetInterpolate,
                "SetInterpolate", &NetworkSync::SetInterpolate,
                "IsLocallyOwned", &NetworkSync::IsLocallyOwned
            );
        }

        void register_camera(sol::state_view state)
        {
            visual::enumeration<CameraExposureMode>(state, "CameraExposureMode", {
                {"manual",    CameraExposureMode::manual},
                {"automatic", CameraExposureMode::automatic}
            });

            visual::enumeration<CameraFlags>(state, "CameraFlags", {
                {"CanBeControlled",       CameraFlags::CanBeControlled},
                {"IsControlled",          CameraFlags::IsControlled},
                {"WantsCursorHidden",     CameraFlags::WantsCursorHidden},
                {"IsDirty",               CameraFlags::IsDirty},
                {"PhysicalBodyAnimation", CameraFlags::PhysicalBodyAnimation},
                {"Flashlight",            CameraFlags::Flashlight}
            });

            visual::usertype<Camera>(state, "Camera",
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
                "GetAperture",                     &Camera::GetAperture,
                "SetAperture",                     &Camera::SetAperture,
                "GetShutterSpeed",                 &Camera::GetShutterSpeed,
                "SetShutterSpeed",                 &Camera::SetShutterSpeed,
                "GetFlag",                         &Camera::GetFlag,
                "SetFlag",                         &Camera::SetFlag
            );
        }
    }

    void register_components(sol::state_view state)
    {
        register_components_rendering(state);
        register_audio_source(state);
        register_components_physics(state);
        register_spline(state);
        register_text_3d(state);
        register_navigation(state);
        register_network_sync(state);
        register_camera(state);
    }

    sol::object to_lua(sol::state_view state, Component* component)
    {
        if (!component)
        {
            return sol::lua_nil;
        }

        switch (component->GetType())
        {
            case ComponentType::AudioSource:    return sol::make_object(state, static_cast<AudioSource*>(component));
            case ComponentType::Animator:       return sol::make_object(state, static_cast<Animator*>(component));
            case ComponentType::Camera:         return sol::make_object(state, static_cast<Camera*>(component));
            case ComponentType::Light:          return sol::make_object(state, static_cast<Light*>(component));
            case ComponentType::Navigation:     return sol::make_object(state, static_cast<Navigation*>(component));
            case ComponentType::NetworkSync:    return sol::make_object(state, static_cast<NetworkSync*>(component));
            case ComponentType::ParticleSystem: return sol::make_object(state, static_cast<ParticleSystem*>(component));
            case ComponentType::Physics:        return sol::make_object(state, static_cast<Physics*>(component));
            case ComponentType::Ragdoll:        return sol::make_object(state, static_cast<Ragdoll*>(component));
            case ComponentType::Render:         return sol::make_object(state, static_cast<Render*>(component));
            case ComponentType::Script:         return sol::make_object(state, static_cast<Script*>(component));
            case ComponentType::Spline:         return sol::make_object(state, static_cast<Spline*>(component));
            case ComponentType::Text3D:         return sol::make_object(state, static_cast<Text3D*>(component));
            default:                            return sol::lua_nil;
        }
    }
}
