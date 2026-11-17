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
#include "../world/components/Light.h"
#include "../world/components/ParticleSystem.h"
#include "../world/components/Render.h"
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
            visual::enumeration(State, "MeshFlags",
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

            visual::usertype<Mesh>(State, "Mesh",
                "SaveToFile",               &Mesh::SaveToFile,
                "LoadFromFile",             &Mesh::LoadFromFile,
                "Clear",                    &Mesh::Clear,
                "GetVertexCount",           &Mesh::GetVertexCount,
                "GetIndexCount",            &Mesh::GetIndexCount,
                "GetRootEntity",            &Mesh::GetRootEntity,
                "GetDefaultFlags",          &Mesh::GetDefaultFlags,
                // a visual only copy of the root hierarchy that draws its own skinned instance, so several animators can share one model
                "InstantiateSkinned", [](Mesh& self) -> Entity*
                {
                    Entity* template_root = self.GetRootEntity();
                    if (!template_root)
                    {
                        return nullptr;
                    }

                    std::shared_ptr<Mesh> instance = self.CreateSkinnedInstance();
                    if (!instance)
                    {
                        return nullptr;
                    }

                    Entity* root = template_root->CloneVisualOnly();
                    if (!root)
                    {
                        return nullptr;
                    }
                    instance->SetRootEntity(root);

                    // the renders hold the instance alive from here on
                    std::vector<Entity*> nodes = { root };
                    root->GetDescendants(&nodes);
                    for (Entity* node : nodes)
                    {
                        Render* render = node ? node->GetComponent<Render>() : nullptr;
                        if (render && render->GetMesh() == &self)
                        {
                            render->SetMesh(instance.get(), render->GetSubMeshIndex());
                        }
                    }

                    return root;
                }
            );
        }

        void register_render(sol::state_view State)
        {
            visual::enumeration(State, "MeshType",
                "Cube",     MeshType::Cube,
                "Quad",     MeshType::Quad,
                "Sphere",   MeshType::Sphere,
                "Cylinder", MeshType::Cylinder,
                "Cone",     MeshType::Cone,
                "Max",      MeshType::Max
            );

            visual::enumeration(State, "RenderFlags",
                "CastsShadows",          RenderFlags::CastsShadows,
                "ExcludeFromRayTracing", RenderFlags::ExcludeFromRayTracing
            );

            visual::usertype<Render>(State, "Render",
                sol::base_classes,              sol::bases<Component>(),
                "GetDecalCount",                &Render::GetDecalCount,
                "ClearDecals",                  &Render::ClearDecals,
                "AddDecal", [](Render& self, const Vector3& position, const Quaternion& rotation, const Vector3& half_size, const Vector4& color, float roughness, float relief, float seed, Material* source)
                {
                    if (!position.IsFinite() || !half_size.IsFinite() || half_size.x <= 0 || half_size.y <= 0 || half_size.z <= 0)
                    {
                        return;
                    }
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

        void register_light(sol::state_view State)
        {
            visual::enumeration(State, "LightType",
                "Directional",              LightType::Directional,
                "Point",                    LightType::Point,
                "Spot",                     LightType::Spot,
                "Area",                     LightType::Area,
                "Max",                      LightType::Max
            );

            visual::enumeration(State, "LightIntensity",
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

            visual::enumeration(State, "LightIntensityUnit",
                "Lux",                      LightIntensityUnit::Lux,
                "Lumens",                   LightIntensityUnit::Lumens
            );

            visual::enumeration(State, "LightFlags",
                "Shadows",                  LightFlags::Shadows,
                "ShadowsScreenSpace",       LightFlags::ShadowsScreenSpace,
                "Volumetric",               LightFlags::Volumetric,
                "DayNightCycle",            LightFlags::DayNightCycle,
                "RealTimeCycle",            LightFlags::RealTimeCycle
            );

            visual::enumeration(State, "LightPreset",
                "dawn",                     LightPreset::dawn,
                "day",                      LightPreset::day,
                "dusk",                     LightPreset::dusk,
                "night",                    LightPreset::night,
                "david_lynch",              LightPreset::david_lynch,
                "custom",                   LightPreset::custom
            );

            visual::usertype<Light>(State, "Light",
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
            visual::usertype<ParticleSystem>(state, "ParticleSystem",
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
    }

    void register_components_rendering(sol::state_view state)
    {
        register_mesh(state);
        register_render(state);
        register_light(state);
        register_particle_system(state);
    }
}
