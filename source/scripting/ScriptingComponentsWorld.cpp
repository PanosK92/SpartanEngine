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
#include "../world/components/Camera.h"
#include "../world/components/NetworkSync.h"
#include "../world/components/Destructible.h"
#include "../world/components/Water.h"
//===================================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    namespace
    {
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

        void register_destructible(sol::state_view state)
        {
            visual::enumeration<FracturePattern>(state, "FracturePattern", {
                {"Uniform",      FracturePattern::Uniform},
                {"RadialGlass",  FracturePattern::RadialGlass},
                {"SplinterWood", FracturePattern::SplinterWood},
                {"LayeredStone", FracturePattern::LayeredStone}
            });

            visual::usertype<Destructible>(state, "Destructible",
                sol::base_classes, sol::bases<Component>(),
                "GetPattern",            &Destructible::GetPattern,
                "SetPattern",            &Destructible::SetPattern,
                "GetPatternIndex",       &Destructible::GetPatternIndex,
                "SetPatternIndex",       &Destructible::SetPatternIndex,
                "GetChunkCount",         &Destructible::GetChunkCount,
                "SetChunkCount",         &Destructible::SetChunkCount,
                "GetImpactThreshold",    &Destructible::GetImpactThreshold,
                "SetImpactThreshold",    &Destructible::SetImpactThreshold,
                "GetHealth",             &Destructible::GetHealth,
                "SetHealth",             &Destructible::SetHealth,
                "GetCurrentHealth",      &Destructible::GetCurrentHealth,
                "GetDebrisMass",         &Destructible::GetDebrisMass,
                "SetDebrisMass",         &Destructible::SetDebrisMass,
                "GetExplosionImpulse",   &Destructible::GetExplosionImpulse,
                "SetExplosionImpulse",   &Destructible::SetExplosionImpulse,
                "GetInheritVelocity",    &Destructible::GetInheritVelocity,
                "SetInheritVelocity",    &Destructible::SetInheritVelocity,
                "GetSupportAnchored",    &Destructible::GetSupportAnchored,
                "SetSupportAnchored",    &Destructible::SetSupportAnchored,
                "GetDamageRadius",       &Destructible::GetDamageRadius,
                "SetDamageRadius",       &Destructible::SetDamageRadius,
                "GetMaxDepth",           &Destructible::GetMaxDepth,
                "SetMaxDepth",           &Destructible::SetMaxDepth,
                "GetDebrisLifetime",     &Destructible::GetDebrisLifetime,
                "SetDebrisLifetime",     &Destructible::SetDebrisLifetime,
                "GetInteriorUvScale",    &Destructible::GetInteriorUvScale,
                "SetInteriorUvScale",    &Destructible::SetInteriorUvScale,
                "GetSeed",               &Destructible::GetSeed,
                "SetSeed",               &Destructible::SetSeed,
                "IsFractured",            &Destructible::IsFractured,
                "GetActiveShardCount",    &Destructible::GetActiveShardCount,
                "GetStructuralIntegrity", &Destructible::GetStructuralIntegrity,
                "HasTopSupport",          &Destructible::HasTopSupport,
                "Reset",                  &Destructible::Reset,
                "Fracture", sol::overload(
                    [](Destructible& self, const Vector3& impact_pos) { return self.Fracture(impact_pos); },
                    [](Destructible& self, const Vector3& impact_pos, const Vector3& impact_vel) { return self.Fracture(impact_pos, impact_vel); },
                    [](Destructible& self, const Vector3& impact_pos, const Vector3& impact_vel, float radius) { return self.Fracture(impact_pos, impact_vel, radius); },
                    [](Destructible& self, const Vector3& impact_pos, const Vector3& impact_vel, float radius, bool is_explosion) { return self.Fracture(impact_pos, impact_vel, radius, is_explosion); }
                ),
                "ApplyExplosion",        &Destructible::ApplyExplosion,
                "ResetAll",              &Destructible::ResetAll,
                "GetTotalActiveShards",  &Destructible::GetTotalActiveShards
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

        void register_water(sol::state_view state)
        {
            visual::usertype<Water>(state, "Water",
                sol::base_classes, sol::bases<Component>(),
                "GetWaveSize",          &Water::GetWaveSize,
                "SetWaveSize",          &Water::SetWaveSize,
                "GetSurfSize",          &Water::GetSurfSize,
                "SetSurfSize",          &Water::SetSurfSize,
                "GetClarity",           &Water::GetClarity,
                "SetClarity",           &Water::SetClarity,
                "GetBiome",             &Water::GetBiome,
                "SetBiome",             &Water::SetBiome,
                "GetSpeciesMask",       &Water::GetSpeciesMask,
                "SetSpeciesMask",       &Water::SetSpeciesMask,
                "GetPopulation",        &Water::GetPopulation,
                "SetPopulation",        &Water::SetPopulation,
                "GetChoppiness",        &Water::GetChoppiness,
                "SetChoppiness",        &Water::SetChoppiness,
                "GetSeaLevel",          &Water::GetSeaLevel,
                "SetSeaLevel",          &Water::SetSeaLevel,
                "GetTurbidity",         &Water::GetTurbidity,
                "SetTurbidity",         &Water::SetTurbidity,
                "GetCausticsIntensity", &Water::GetCausticsIntensity,
                "SetCausticsIntensity", &Water::SetCausticsIntensity
            );
        }
    }

    void register_components_world(sol::state_view state)
    {
        register_network_sync(state);
        register_destructible(state);
        register_camera(state);
        register_water(state);
    }
}
