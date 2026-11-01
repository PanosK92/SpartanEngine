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
#include "../world/Entity.h"
#include "../world/components/AudioSource.h"
#include "../world/components/Animator.h"
#include "../world/components/Camera.h"
#include "../world/components/Light.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Physics.h"
#include "../world/components/Ragdoll.h"
#include "../world/components/Render.h"
#include "../world/components/Script.h"
#include "../world/components/Spline.h"
#include "../world/components/Text3D.h"
#include "../geometry/Mesh.h"
#include "../rendering/Material.h"
#include "../rendering/Renderer.h"
#include "../resource/ResourceCache.h"
//===================================================

//= NAMESPACES ================
using namespace std;
using namespace spartan::math;
//=============================

namespace spartan::scripting
{
    namespace
    {
        void register_mesh(sol::state_view State)
        {
            State.new_enum("MeshFlags",
                "ImportRemoveRedundantData",       MeshFlags::ImportRemoveRedundantData,
                "ImportLights",                    MeshFlags::ImportLights,
                "ImportCombineMeshes",             MeshFlags::ImportCombineMeshes,
                "ImportGenerateSmoothNormals",     MeshFlags::ImportGenerateSmoothNormals,
                "PostProcessNormalizeScale",       MeshFlags::PostProcessNormalizeScale,
                "PostProcessOptimize",             MeshFlags::PostProcessOptimize,
                "PostProcessGenerateLods",         MeshFlags::PostProcessGenerateLods,
                "PostProcessPreserveLod0",         MeshFlags::PostProcessPreserveLod0,
                "PostProcessPreserveTerrainEdges", MeshFlags::PostProcessPreserveTerrainEdges
            );

            State.new_usertype<Mesh>("Mesh",
                "SaveToFile",               &Mesh::SaveToFile,
                "LoadFromFile",             &Mesh::LoadFromFile,
                "Clear",                    &Mesh::Clear,
                "GetVertexCount",           &Mesh::GetVertexCount,
                "GetIndexCount",            &Mesh::GetIndexCount,
                "GetRootEntity",            &Mesh::GetRootEntity,
                "GetDefaultFlags",          &Mesh::GetDefaultFlags
                );
        }

        void register_audio_source(sol::state_view State)
        {
            State.new_usertype<AudioSource>("AudioSource",
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

        void register_render(sol::state_view State)
        {
            State.new_enum("MeshType",
                "Cube",     MeshType::Cube,
                "Quad",     MeshType::Quad,
                "Sphere",   MeshType::Sphere,
                "Cylinder", MeshType::Cylinder,
                "Cone",     MeshType::Cone,
                "Max",      MeshType::Max
            );

            State.new_enum("RenderFlags",
                "CastsShadows",          RenderFlags::CastsShadows,
                "ExcludeFromRayTracing", RenderFlags::ExcludeFromRayTracing
            );

            State.new_usertype<Render>("Render",
                sol::base_classes,              sol::bases<Component>(),
                "GetDecalCount",                &Render::GetDecalCount,
                "ClearDecals",                  &Render::ClearDecals,
                "AddDecal", [](Render& self, const Vector3& position, const Quaternion& rotation, const Vector3& half_size, const Vector4& color, float roughness, float relief, float seed, Material* source)
                {
                    if (!position.IsFinite() || !half_size.IsFinite() || half_size.x <= 0 || half_size.y <= 0 || half_size.z <= 0) return;
                    DecalParameters decal;
                    decal.world_to_decal = Matrix(position, rotation, half_size).Inverted();
                    decal.color = Vector4(color.x, color.y, color.z, std::clamp(color.w, 0.0f, 1.0f));
                    decal.surface = Vector4(std::clamp(roughness, 0.0f, 1.0f), std::clamp(relief, 0.0f, 0.01f), seed, 0.0f);
                    self.AddDecal(decal, source);
                },
                "GetMaterialName",              &Render::GetMaterialName,
                "GetBoundingBox",               &Render::GetBoundingBox,
                "GetMaterial",                  &Render::GetMaterial,
                "SetMesh", sol::overload(
                    [](Render& self, Mesh* mesh)                          { self.SetMesh(mesh); },
                    [](Render& self, Mesh* mesh, uint32_t sub_mesh_index) { self.SetMesh(mesh, sub_mesh_index); },
                    [](Render& self, MeshType type)                       { self.SetMesh(type); }
                ),
                "SetMaterial", sol::overload(
                    [](Render& self, std::shared_ptr<Material> material)
                    {
                        if (material)
                        {
                            self.SetMaterial(material);
                        }
                    },
                    [](Render& self, Material* material)
                    {
                        // accepts the raw pointer that GetMaterial returns by resolving the cached shared_ptr
                        if (material)
                        {
                            if (std::shared_ptr<Material> cached = ResourceCache::GetByName<Material>(material->GetObjectName()))
                            {
                                self.SetMaterial(cached);
                            }
                        }
                    },
                    [](Render& self, const std::string& file_path)
                    {
                        self.SetMaterial(file_path);
                    }
                ),
                "SetDefaultMaterial",           &Render::SetDefaultMaterial,
                "SetMaxRenderDistance",         &Render::SetMaxRenderDistance,
                "SetMaxShadowDistance",         &Render::SetMaxShadowDistance,
                "SetFlag", [](Render& self, RenderFlags flag, bool enable) { self.SetFlag(flag, enable); }
            );
        }

        void register_physics(sol::state_view State)
        {

            State.new_enum("BodyType",
                "Box",          BodyType::Box,
                "Sphere",       BodyType::Sphere,
                "Plane",        BodyType::Plane,
                "Capsule",      BodyType::Capsule,
                "Mesh",         BodyType::Mesh,
                "MeshConvex",   BodyType::MeshConvex,
                "Controller",   BodyType::Controller,
                "Custom",       BodyType::Custom,
                "Cloth",        BodyType::Cloth,
                "Heightfield",  BodyType::Heightfield,
                "Max",          BodyType::Max);


            State.new_usertype<Physics>("Physics",
                sol::base_classes,              sol::bases<Component>(),

                "GetMass",                      &Physics::GetMass,
                "SetMass",                      &Physics::SetMass,
                "GetFriction",                  &Physics::GetFriction,
                "SetFriction",                  &Physics::SetFriction,
                "GetFrictionRolling",           &Physics::GetFrictionRolling,
                "SetFrictionRolling",           &Physics::SetFrictionRolling,
                "GetRestitution",               &Physics::GetRestitution,
                "SetRestitution",               &Physics::SetRestitution,

                "SetLinearVelocity",            &Physics::SetLinearVelocity,
                "GetLinearVelocity",            &Physics::GetLinearVelocity,
                "SetAngularVelocity",           &Physics::SetAngularVelocity,

                "SetCenterOfMass",              &Physics::SetCenterOfMass,
                "GetCenterOfMass",              &Physics::GetCenterOfMass,

                "GetCapsuleVolume",             &Physics::GetCapsuleVolume,
                "GetCapsuleRadius",             &Physics::GetCapsuleRadius,

                "GetClothStiffness",            &Physics::GetClothStiffness,
                "SetClothStiffness",            &Physics::SetClothStiffness,
                "GetClothDamping",              &Physics::GetClothDamping,
                "SetClothDamping",              &Physics::SetClothDamping,
                "GetClothIterations",           &Physics::GetClothIterations,
                "SetClothIterations",           &Physics::SetClothIterations,
                "GetClothWindEnabled",          &Physics::GetClothWindEnabled,
                "SetClothWindEnabled",          &Physics::SetClothWindEnabled,

                "IsGrounded",                   &Physics::IsGrounded,
                "GetGroundEntity",              &Physics::GetGroundEntity,
                "GetBodyType",                  &Physics::GetBodyType,
                "SetBodyType",                  &Physics::SetBodyType,
                "IsStatic",                     &Physics::IsStatic,
                "SetStatic",                    &Physics::SetStatic,
                "SetBodyTransform",             [](Physics& physics, const Vector3& position, const Quaternion& rotation) { physics.SetBodyTransform(position, rotation, false); }
                );

        }

        void register_light(sol::state_view State)
        {
            State.new_enum("LightType",
                "Directional",              LightType::Directional,
                "Point",                    LightType::Point,
                "Spot",                     LightType::Spot,
                "Area",                     LightType::Area,
                "Max",                      LightType::Max
            );

            State.new_enum("LightIntensity",
                "bulb_stadium",             LightIntensity::bulb_stadium,
                "bulb_500_watt",            LightIntensity::bulb_500_watt,
                "bulb_150_watt",            LightIntensity::bulb_150_watt,
                "bulb_100_watt",            LightIntensity::bulb_100_watt,
                "bulb_60_watt",             LightIntensity::bulb_60_watt,
                "bulb_25_watt",             LightIntensity::bulb_25_watt,
                "bulb_flashlight",          LightIntensity::bulb_flashlight,
                "black_hole",               LightIntensity::black_hole,
                "custom",                   LightIntensity::custom
            );

            State.new_enum("LightIntensityUnit",
                "Lux",                      LightIntensityUnit::Lux,
                "Lumens",                   LightIntensityUnit::Lumens
            );

            State.new_enum("LightFlags",
                "Shadows",                  LightFlags::Shadows,
                "ShadowsScreenSpace",       LightFlags::ShadowsScreenSpace,
                "Volumetric",               LightFlags::Volumetric,
                "DayNightCycle",            LightFlags::DayNightCycle,
                "RealTimeCycle",            LightFlags::RealTimeCycle
            );

            State.new_enum("LightPreset",
                "dawn",                     LightPreset::dawn,
                "day",                      LightPreset::day,
                "dusk",                     LightPreset::dusk,
                "night",                    LightPreset::night,
                "david_lynch",              LightPreset::david_lynch,
                "custom",                   LightPreset::custom
            );


            State.new_usertype<Light>("Light",
                sol::base_classes,              sol::bases<Component>(),

                "SetTemperature",               &Light::SetTemperature,
                "GetTemperature",               &Light::GetTemperature,


                "SetColor",                     &Light::SetColor,
                "GetColor",                     &Light::GetColor,
                "GetIntensityPhotometric",      &Light::GetIntensityPhotometric,
                "GetIntensityLumens",           &Light::GetIntensityLumens,
                "GetIntensityUnit",             &Light::GetIntensityUnit,
                "GetIntensityRadiometric",      &Light::GetIntensityRadiometric,
                "GetIntensityWatt",             &Light::GetIntensityWatt,

                "SetIntensity",                 sol::overload(
                    [](Light& Self, float Lumens) { Self.SetIntensity(Lumens); },
                    [](Light& Self, LightIntensity Intensity) { Self.SetIntensity(Intensity); }),

                "SetAngle",                     &Light::SetAngle,
                "GetAngle",                     &Light::GetAngle,

                "SetRange",                     &Light::SetRange,
                "GetRange",                     &Light::GetRange,

                "SetIesProfile",                &Light::SetIesProfile,
                "GetIesProfile",                &Light::GetIesProfile,
                "GetIesLumens",                 &Light::GetIesLumens,
                "GetIesPeakCandela",            &Light::GetIesPeakCandela,

                "SetAreaWidth",                 &Light::SetAreaWidth,
                "GetAreaWidth",                 &Light::GetAreaWidth,
                "SetAreaHeight",                &Light::SetAreaHeight,
                "GetAreaHeight",                &Light::GetAreaHeight,
                "FitToMesh",                    &Light::FitToMesh,

                "IsInViewFrustrum",             &Light::IsInViewFrustum,

                "SetDrawDistance",              &Light::SetDrawDistance,
                "GetDrawDistance",              &Light::GetDrawDistance,

                "SetShadowDistance",            &Light::SetShadowDistance,
                "GetShadowDistance",            &Light::GetShadowDistance,
                "SetVolumetricDistance",        &Light::SetVolumetricDistance,
                "GetVolumetricDistance",        &Light::GetVolumetricDistance,

                "GetLightType",                 &Light::GetLightType,
                "SetLightType",                 &Light::SetLightType,

                "SetFlag",                      [](Light& Self, LightFlags flag, bool enable) { Self.SetFlag(flag, enable); },
                "GetFlag",                      &Light::GetFlag,
                "SetPreset",                    &Light::SetPreset,
                "GetPreset",                    &Light::GetPreset,
                "SetTimeOfDay",                 [](Light& Self, float time_of_day) { Self.SetTimeOfDay(time_of_day); },
                "SetCloudCoverage",             &Light::SetCloudCoverage,
                "GetCloudCoverage",             &Light::GetCloudCoverage,
                "SetRain",                      &Light::SetRain,
                "GetRain",                      &Light::GetRain,

                "GetSliceCount",                &Light::GetSliceCount,

                "GetAtlasOffset",               &Light::GetAtlasOffset,
                "GetAtlasScale",                &Light::GetAtlasScale,
                "GetBoundingBox",               &Light::GetBoundingBox

            );
        }

        void register_particle_system(sol::state_view state)
        {
            state.new_usertype<ParticleSystem>("ParticleSystem",
                sol::base_classes,        sol::bases<Component>(),

                "GetEmissionRate",        &ParticleSystem::GetEmissionRate,
                "SetEmissionRate",        &ParticleSystem::SetEmissionRate,
                "GetMaxParticles",        &ParticleSystem::GetMaxParticles,
                "SetMaxParticles",        &ParticleSystem::SetMaxParticles,
                "GetLifetime",            &ParticleSystem::GetLifetime,
                "SetLifetime",            &ParticleSystem::SetLifetime,
                "GetStartSpeed",          &ParticleSystem::GetStartSpeed,
                "SetStartSpeed",          &ParticleSystem::SetStartSpeed,
                "GetStartSize",           &ParticleSystem::GetStartSize,
                "SetStartSize",           &ParticleSystem::SetStartSize,
                "GetEndSize",             &ParticleSystem::GetEndSize,
                "SetEndSize",             &ParticleSystem::SetEndSize,
                "GetGravityModifier",     &ParticleSystem::GetGravityModifier,
                "SetGravityModifier",     &ParticleSystem::SetGravityModifier,
                "GetEmissionRadius",      &ParticleSystem::GetEmissionRadius,
                "SetEmissionRadius",      &ParticleSystem::SetEmissionRadius,
                "SetEmissionDirection",   [](ParticleSystem& self, float x, float y, float z) { self.SetEmissionDirection(Vector3(x, y, z)); },
                "GetEmissionConeAngle",   &ParticleSystem::GetEmissionConeAngle,
                "SetEmissionConeAngle",   &ParticleSystem::SetEmissionConeAngle,
                "GetDirectionalBlend",    &ParticleSystem::GetDirectionalBlend,
                "SetDirectionalBlend",    &ParticleSystem::SetDirectionalBlend,
                "GetBlendMode",           [](ParticleSystem& self) { return static_cast<uint32_t>(self.GetBlendMode()); },
                "SetBlendMode",           [](ParticleSystem& self, uint32_t mode) { self.SetBlendMode(static_cast<ParticleBlendMode>(mode)); },
                "GetLightingMode",        [](ParticleSystem& self) { return static_cast<uint32_t>(self.GetLightingMode()); },
                "SetLightingMode",        [](ParticleSystem& self, uint32_t mode) { self.SetLightingMode(static_cast<ParticleLightingMode>(mode)); },
                "GetRenderMode",          [](ParticleSystem& self) { return static_cast<uint32_t>(self.GetRenderMode()); },
                "SetRenderMode",          [](ParticleSystem& self, uint32_t mode) { self.SetRenderMode(static_cast<ParticleRenderMode>(mode)); },
                "GetEmissiveStrength",    &ParticleSystem::GetEmissiveStrength,
                "SetEmissiveStrength",    &ParticleSystem::SetEmissiveStrength,
                "GetSoftDepthScale",      &ParticleSystem::GetSoftDepthScale,
                "SetSoftDepthScale",      &ParticleSystem::SetSoftDepthScale,
                "GetVolumeDensity",       &ParticleSystem::GetVolumeDensity,
                "SetVolumeDensity",       &ParticleSystem::SetVolumeDensity,
                "GetVolumeAnisotropy",    &ParticleSystem::GetVolumeAnisotropy,
                "SetVolumeAnisotropy",    &ParticleSystem::SetVolumeAnisotropy,
                "GetVolumeShadowing",     &ParticleSystem::GetVolumeShadowing,
                "SetVolumeShadowing",     &ParticleSystem::SetVolumeShadowing,
                "GetDrag",                &ParticleSystem::GetDrag,
                "SetDrag",                &ParticleSystem::SetDrag,
                "GetTurbulenceStrength",  &ParticleSystem::GetTurbulenceStrength,
                "SetTurbulenceStrength",  &ParticleSystem::SetTurbulenceStrength,
                "GetWindInfluence",       &ParticleSystem::GetWindInfluence,
                "SetWindInfluence",       &ParticleSystem::SetWindInfluence,
                "GetVelocityInheritance", &ParticleSystem::GetVelocityInheritance,
                "SetVelocityInheritance", &ParticleSystem::SetVelocityInheritance,
                "GetVelocityStretch",     &ParticleSystem::GetVelocityStretch,
                "SetVelocityStretch",     &ParticleSystem::SetVelocityStretch,
                "SetVortexCenter",        [](ParticleSystem& self, float x, float y, float z) { self.SetVortexCenter(Vector3(x, y, z)); },
                "SetVortexAxis",          [](ParticleSystem& self, float x, float y, float z) { self.SetVortexAxis(Vector3(x, y, z)); },
                "GetVortexStrength",      &ParticleSystem::GetVortexStrength,
                "SetVortexStrength",      &ParticleSystem::SetVortexStrength,
                "GetVortexRadius",        &ParticleSystem::GetVortexRadius,
                "SetVortexRadius",        &ParticleSystem::SetVortexRadius,
                "GetThermalStrength",     &ParticleSystem::GetThermalStrength,
                "SetThermalStrength",     &ParticleSystem::SetThermalStrength,
                "GetThermalDecay",        &ParticleSystem::GetThermalDecay,
                "SetThermalDecay",        &ParticleSystem::SetThermalDecay,
                "GetRollupStrength",      &ParticleSystem::GetRollupStrength,
                "SetRollupStrength",      &ParticleSystem::SetRollupStrength,
                "GetWakeStrength",        &ParticleSystem::GetWakeStrength,
                "SetWakeStrength",        &ParticleSystem::SetWakeStrength,
                "GetChurnStrength",       &ParticleSystem::GetChurnStrength,
                "SetChurnStrength",       &ParticleSystem::SetChurnStrength,
                "GetCollisionClearance",  &ParticleSystem::GetCollisionClearance,
                "SetCollisionClearance",  &ParticleSystem::SetCollisionClearance,
                "GetSpawnBurst",          &ParticleSystem::GetSpawnBurst,
                "SetSpawnBurst",          &ParticleSystem::SetSpawnBurst,
                "TriggerBurst",           &ParticleSystem::TriggerBurst,
                "GetFlipbookRows",        &ParticleSystem::GetFlipbookRows,
                "SetFlipbookRows",        &ParticleSystem::SetFlipbookRows,
                "GetFlipbookColumns",     &ParticleSystem::GetFlipbookColumns,
                "SetFlipbookColumns",     &ParticleSystem::SetFlipbookColumns,
                "GetFlipbookFps",         &ParticleSystem::GetFlipbookFps,
                "SetFlipbookFps",         &ParticleSystem::SetFlipbookFps,
                "GetEffectPath",          &ParticleSystem::GetEffectPath,
                "LoadEffect",             &ParticleSystem::LoadEffect,
                "SaveEffect",             &ParticleSystem::SaveEffect,
                "SetStartColor",          [](ParticleSystem& self, float r, float g, float b, float a) { self.SetStartColor(Color(r, g, b, a)); },
                "SetEndColor",            [](ParticleSystem& self, float r, float g, float b, float a) { self.SetEndColor(Color(r, g, b, a)); },
                "SetTexture",             sol::overload(
                    [](ParticleSystem& self, const string& file_path) { self.SetTexture(file_path); },
                    [](ParticleSystem& self) { self.SetTexture(""); })
                );
        }

        void register_spline(sol::state_view state)
        {
            state.new_usertype<Spline>("Spline",
                "GetPoint",             &Spline::GetPoint,
                "GetTangent",           &Spline::GetTangent,
                "GetLength",            [](Spline* self) { return self->GetLength(); },
                "GetTAtDistance",       [](Spline* self, float distance) { return self->GetTAtDistance(distance); },
                "GetControlPointCount", &Spline::GetControlPointCount,
                "GetClosedLoop",        &Spline::GetClosedLoop,
                "GetRoadWidth",         &Spline::GetRoadWidth,
                "GetCurveAlpha",        &Spline::GetCurveAlpha,
                "SetCurveAlpha",        &Spline::SetCurveAlpha,
                "AddControlPoint",      &Spline::AddControlPoint,
                "RemoveLastControlPoint", &Spline::RemoveLastControlPoint,
                "ResampleControlPoints", &Spline::ResampleControlPoints,
                "SimplifyControlPoints", &Spline::SimplifyControlPoints,
                "GenerateRoadMesh",     &Spline::GenerateRoadMesh,
                "ClearRoadMesh",        &Spline::ClearRoadMesh
            );
        }

        void register_text_3d(sol::state_view state)
        {
            state.new_enum(
                "Text3DAlignment",
                "Left",
                Text3DAlignment::Left,
                "Center",
                Text3DAlignment::Center,
                "Right",
                Text3DAlignment::Right
            );

            state.new_usertype<Text3D>(
                "Text3D",
                sol::base_classes,
                sol::bases<Component>(),
                "GetText",
                &Text3D::GetText,
                "SetText",
                &Text3D::SetText,
                "GetFontPath",
                &Text3D::GetFontPath,
                "SetFontPath",
                &Text3D::SetFontPath,
                "GetSize",
                &Text3D::GetSize,
                "SetSize",
                &Text3D::SetSize,
                "GetDepth",
                &Text3D::GetDepth,
                "SetDepth",
                &Text3D::SetDepth,
                "GetWeight",
                &Text3D::GetWeight,
                "SetWeight",
                &Text3D::SetWeight,
                "GetLetterSpacing",
                &Text3D::GetLetterSpacing,
                "SetLetterSpacing",
                &Text3D::SetLetterSpacing,
                "GetLineSpacing",
                &Text3D::GetLineSpacing,
                "SetLineSpacing",
                &Text3D::SetLineSpacing,
                "GetResolution",
                &Text3D::GetResolution,
                "SetResolution",
                &Text3D::SetResolution,
                "GetAlignment",
                &Text3D::GetAlignment,
                "SetAlignment",
                &Text3D::SetAlignment,
                "GenerateMesh",
                &Text3D::GenerateMesh
            );
        }

        void register_animator(sol::state_view state)
        {
            state.new_usertype<Animator>("Animator",
                sol::base_classes, sol::bases<Component>(),
                "Play",               &Animator::Play,
                "Stop",               &Animator::Stop,
                "Pause",              &Animator::Pause,
                "Resume",             &Animator::Resume,
                "IsPlaying",          &Animator::IsPlaying,
                "GetSpeed",           &Animator::GetSpeed,
                "SetSpeed",           &Animator::SetSpeed,
                "GetLoop",            &Animator::GetLoop,
                "SetLoop",            &Animator::SetLoop,
                "GetBlendDuration",   &Animator::GetBlendDuration,
                "SetBlendDuration",   &Animator::SetBlendDuration,
                "GetCurrentClip",     &Animator::GetCurrentClip,
                "GetFootIkEnabled",   &Animator::GetFootIkEnabled,
                "SetFootIkEnabled",   &Animator::SetFootIkEnabled,
                "GetFootIkWeight",    &Animator::GetFootIkWeight,
                "SetFootIkWeight",    &Animator::SetFootIkWeight,
                "GetFootIkGroundOffset", &Animator::GetFootIkGroundOffset,
                "HasFootIkSupportGround", &Animator::HasFootIkSupportGround,
                "GetFootIkSupportGroundY", &Animator::GetFootIkSupportGroundY
            );
        }

        void register_ragdoll(sol::state_view state)
        {
            state.new_usertype<Ragdoll>("Ragdoll",
                sol::base_classes, sol::bases<Component>(),
                "IsDead", &Ragdoll::IsDead,
                "IsFrozen", &Ragdoll::IsFrozen,
                "Activate", &Ragdoll::Activate,
                "Wake", &Ragdoll::Wake,
                "SetHitBodyEnabled", &Ragdoll::SetHitBodyEnabled,
                "IsHitBodyEnabled", &Ragdoll::IsHitBodyEnabled
            );
        }

        void register_camera(sol::state_view state)
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
    }

    void register_components(sol::state_view state)
    {
        register_mesh(state);
        register_audio_source(state);
        register_render(state);
        register_physics(state);
        register_light(state);
        register_particle_system(state);
        register_spline(state);
        register_text_3d(state);
        register_animator(state);
        register_ragdoll(state);
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
