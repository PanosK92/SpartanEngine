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
            visual::enumeration<MeshFlags>(State, "MeshFlags", {
                {"ImportRemoveRedundantData",       MeshFlags::ImportRemoveRedundantData},
                {"ImportLights",                    MeshFlags::ImportLights},
                {"ImportCombineMeshes",             MeshFlags::ImportCombineMeshes},
                {"ImportGenerateSmoothNormals",     MeshFlags::ImportGenerateSmoothNormals},
                {"PostProcessNormalizeScale",       MeshFlags::PostProcessNormalizeScale},
                {"PostProcessOptimize",             MeshFlags::PostProcessOptimize},
                {"PreserveSourceGeometry",          MeshFlags::PreserveSourceGeometry},
                // Older project packages mask these Lua constants while loading character meshes.
                // Both are inert. All static meshes are cooked from the authored surface.
                {"PostProcessGenerateLods",         static_cast<MeshFlags>(0)},
                {"PostProcessPreserveLod0",         static_cast<MeshFlags>(0)},
                {"PostProcessPreserveTerrainEdges", MeshFlags::PostProcessPreserveTerrainEdges}
            });

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
            visual::enumeration<MeshType>(State, "MeshType", {
                {"Cube",     MeshType::Cube},
                {"Quad",     MeshType::Quad},
                {"Sphere",   MeshType::Sphere},
                {"Cylinder", MeshType::Cylinder},
                {"Cone",     MeshType::Cone},
                {"Max",      MeshType::Max}
            });

            visual::enumeration<RenderFlags>(State, "RenderFlags", {
                {"CastsShadows",          RenderFlags::CastsShadows},
                {"ExcludeFromRayTracing", RenderFlags::ExcludeFromRayTracing}
            });

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

    }

    void register_components_rendering(sol::state_view state)
    {
        register_mesh(state);
        register_render(state);
        register_components_effects(state);
    }
}
