/*
Copyright(c) 2015-2026 Panos Karabelas

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

//= INCLUDES ================================
#include "pch.h"
#include "../../profiling/WorldWork.h"
#include <sstream>
#include "Render.h"
#include "Camera.h"
#include "Physics.h"
#include "../Entity.h"
#include "../rhi/RHI_Buffer.h"
#include "../rhi/RHI_Device.h"
#include "../rhi/RHI_AccelerationStructure.h"
#include "../../file_system/FileSystem.h"
#include "../../resource/ResourceCache.h"
#include "../../rendering/Renderer.h"
#include "../../rendering/Material.h"
#include "../../rendering/GeometryBuffer.h"
#include "../../geometry/Mesh.h"
#include "../../geometry/GeneratedCache.h"
#include "../../core/ThreadPool.h"
SP_WARNINGS_OFF
#include <sol/sol.hpp>
#include "../io/pugixml.hpp"
SP_WARNINGS_ON
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    void Render::AddDecal(const DecalParameters& world_decal, Material* source_material)
    {
        if (!world_decal.world_to_decal.IsFinite() || HasInstancing()) return;
        DecalParameters decal = world_decal;
        decal.world_to_decal = m_entity_ptr->GetMatrix() * world_decal.world_to_decal;
        // Bounded history, composited in emission order. ClearDecals also supports washing/reset.
        if (m_decals.size() == decal_capacity)
            m_decals.erase(m_decals.begin());
        m_decals.push_back({decal, source_material});
        m_scene->has_decals = true;
    }

    bool Render::ExcludesTerrainBlend() const
    {
        return HasFlag(RenderFlags::ExcludeFromTerrainBlend) || m_entity_ptr->IsDynamic();
    }

    namespace
    {
        const MeshLod* get_mesh_lod(Mesh* mesh, uint32_t sub_mesh_index, uint32_t lod)
        {
            if (!mesh || sub_mesh_index >= mesh->GetSubMeshCount())
            {
                return nullptr;
            }

            const SubMesh& sub_mesh = mesh->GetSubMesh(sub_mesh_index);
            if (lod >= sub_mesh.lods.size())
            {
                return nullptr;
            }

            return &sub_mesh.lods[lod];
        }
    }

    Render::Render(Entity* entity) : Component(entity), m_scene(std::make_unique<RenderSceneData>())
    {
        m_scene->entity = entity;
        m_scene->render = this;
        SetEntityActive(entity->GetActive());
        World::InvalidateRenderSceneData();
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material_default, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_owned_mesh, shared_ptr<Mesh>);
        RegisterAttribute("m_material", "Material*", [this]() { return m_scene->material; },
            [this](const std::any& value) { m_scene->material = std::any_cast<Material*>(value); });
        RegisterAttribute("m_flags", "uint32_t", [this]() { return m_scene->flags; },
            [this](const std::any& value) { m_scene->flags = std::any_cast<uint32_t>(value); });
        RegisterAttribute("m_mesh", "Mesh*", [this]() { return m_scene->mesh; },
            [this](const std::any& value) { m_scene->mesh = std::any_cast<Mesh*>(value); });
        RegisterAttribute("m_bounding_box", "BoundingBox", [this]() { return m_scene->bounding_box; },
            [this](const std::any& value) { m_scene->bounding_box = std::any_cast<BoundingBox>(value); });
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box_mesh, BoundingBox);
        RegisterAttribute("m_sub_mesh_index", "uint32_t", [this]() { return m_scene->sub_mesh_index; },
            [this](const std::any& value) { m_scene->sub_mesh_index = std::any_cast<uint32_t>(value); });
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box_dirty, bool);
        RegisterAttribute("m_instances", "vector<Instance>", [this]() { return m_scene->instances; },
            [this](const std::any& value) { m_scene->instances = std::any_cast<vector<Instance>>(value); });
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_transform_previous, Matrix);
        RegisterAttribute("m_max_distance_render", "float", [this]() { return m_scene->max_distance_render; },
            [this](const std::any& value) { m_scene->max_distance_render = std::any_cast<float>(value); });
        RegisterAttribute("m_max_distance_shadow", "float", [this]() { return m_scene->max_distance_shadow; },
            [this](const std::any& value) { m_scene->max_distance_shadow = std::any_cast<float>(value); });
        RegisterAttribute("m_distance_squared", "float", [this]() { return m_scene->distance_squared; },
            [this](const std::any& value) { m_scene->distance_squared = std::any_cast<float>(value); });
        RegisterAttribute("m_is_visible", "bool", [this]() { return m_scene->is_visible; },
            [this](const std::any& value) { m_scene->is_visible = std::any_cast<bool>(value); });
        RegisterAttribute("m_lod_index", "uint32_t", [this]() { return m_scene->lod_index; },
            [this](const std::any& value) { m_scene->lod_index = std::any_cast<uint32_t>(value); });
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_previous_lights, uint64_t);
    }

    Render::~Render()
    {
        World::InvalidateRenderSceneData();
        GetEntity()->WakePhysicsPreTick();
        m_scene->mesh = nullptr;
    }

    void Render::Save(pugi::xml_node& node)
    {
        // mesh, skip procedural meshes as they are not in the resource cache, their owning component regenerates them after load
        const bool is_standard_mesh = m_scene->mesh && m_scene->mesh->GetObjectName().rfind("standard_", 0) == 0;
        const bool is_procedural    = m_scene->mesh && m_scene->mesh->GetObjectName() == "ocean";
        const bool is_resolvable    = m_scene->mesh && !is_procedural && (is_standard_mesh || ResourceCache::GetByName<Mesh>(m_scene->mesh->GetObjectName()) != nullptr);
        node.append_attribute("mesh_name")      = is_resolvable ? m_scene->mesh->GetObjectName().c_str() : "";
        node.append_attribute("mesh_path")      = is_resolvable && !is_standard_mesh ? m_scene->mesh->GetResourceFilePath().c_str() : "";
        node.append_attribute("sub_mesh_index") = m_scene->sub_mesh_index;

        // material
        node.append_attribute("material_name")    = m_scene->material && !m_material_default ? m_scene->material->GetObjectName().c_str() : "";
        node.append_attribute("material_path")    = m_scene->material && !m_material_default ? m_scene->material->GetResourceFilePath().c_str() : "";
        node.append_attribute("material_default") = m_material_default;

        // per-render material overrides, only set fields are written so unset ones keep inheriting from the material
        auto save_override = [&node](const char* name, float v)
        {
            if (MaterialOverride::is_set(v))
            {
                node.append_attribute(name) = v;
            }
        };
        save_override("ovr_uv_tiling_x",    m_material_override.uv_tiling_x);
        save_override("ovr_uv_tiling_y",    m_material_override.uv_tiling_y);
        save_override("ovr_uv_offset_x",    m_material_override.uv_offset_x);
        save_override("ovr_uv_offset_y",    m_material_override.uv_offset_y);
        save_override("ovr_uv_rotation",    m_material_override.uv_rotation);
        save_override("ovr_uv_invert_x",    m_material_override.uv_invert_x);
        save_override("ovr_uv_invert_y",    m_material_override.uv_invert_y);
        save_override("ovr_uv_world_space", m_material_override.uv_world_space);

        // flags
        node.append_attribute("flags") = m_scene->flags;

        // distances
        node.append_attribute("max_render_distance") = m_scene->max_distance_render;
        node.append_attribute("max_shadow_distance") = m_scene->max_distance_shadow;

        // instances
        if (!m_scene->instances.empty())
        {
            pugi::xml_node instances_node = node.append_child("Instances");
            for (const auto& instance : m_scene->instances)
            {
                pugi::xml_node t_node = instances_node.append_child("Transform");
                math::Matrix matrix = instance.GetMatrix();
                std::stringstream ss;
                ss << matrix.m00 << " " << matrix.m01 << " " << matrix.m02 << " " << matrix.m03 << " "
                   << matrix.m10 << " " << matrix.m11 << " " << matrix.m12 << " " << matrix.m13 << " "
                   << matrix.m20 << " " << matrix.m21 << " " << matrix.m22 << " " << matrix.m23 << " "
                   << matrix.m30 << " " << matrix.m31 << " " << matrix.m32 << " " << matrix.m33;
                t_node.append_attribute("matrix") = ss.str().c_str();
            }
        }
    }

    void Render::Load(pugi::xml_node& node)
    {
        // mesh
        const string mesh_name = node.attribute("mesh_name").as_string();
        const string mesh_path = node.attribute("mesh_path").as_string();
        m_scene->sub_mesh_index       = node.attribute("sub_mesh_index").as_uint();
        if (!mesh_name.empty())
        {
            // check for standard meshes first (owned by Renderer, not ResourceCache)
            if (mesh_name == "standard_cube")
            {
                m_scene->mesh = Renderer::GetStandardMesh(MeshType::Cube).get();
            }
            else if (mesh_name == "standard_quad")
            {
                m_scene->mesh = Renderer::GetStandardMesh(MeshType::Quad).get();
            }
            else if (mesh_name == "standard_sphere")
            {
                m_scene->mesh = Renderer::GetStandardMesh(MeshType::Sphere).get();
            }
            else if (mesh_name == "standard_cylinder")
            {
                m_scene->mesh = Renderer::GetStandardMesh(MeshType::Cylinder).get();
            }
            else if (mesh_name == "standard_cone")
            {
                m_scene->mesh = Renderer::GetStandardMesh(MeshType::Cone).get();
            }
            else if (mesh_name == "ocean")
            {
                // procedural clipmap owned by water, rebuilt in water::initialize
            }
            else
            {
                shared_ptr<Mesh> mesh;
                if (!mesh_path.empty())
                {
                    mesh = ResourceCache::GetByPath<Mesh>(mesh_path);
                    if (!mesh && FileSystem::IsFile(mesh_path))
                    {
                        mesh = ResourceCache::Load<Mesh>(mesh_path);
                    }
                }
                if (!mesh)
                {
                    mesh = ResourceCache::GetByName<Mesh>(mesh_name);
                }
                if (mesh)
                {
                    m_scene->mesh = mesh.get();
                }
                else
                {
                    SP_LOG_WARNING("Render::Load - mesh '%s' not found in cache", mesh_name.c_str());
                }
            }
        }
        else if (!mesh_path.empty())
        {
            shared_ptr<Mesh> mesh =
                ResourceCache::GetByPath<Mesh>(mesh_path);
            if (!mesh && FileSystem::IsFile(mesh_path))
            {
                mesh = ResourceCache::Load<Mesh>(mesh_path);
            }
            if (mesh)
            {
                m_scene->mesh = mesh.get();
            }
            else
            {
                SP_LOG_WARNING(
                    "Render::Load - mesh path '%s' was not found",
                    mesh_path.c_str()
                );
            }
        }

        // material
        m_material_default         = node.attribute("material_default").as_bool(true);
        const string material_name = node.attribute("material_name").as_string();
        const string material_path = node.attribute("material_path").as_string();
        if (!material_name.empty() && !m_material_default)
        {
            shared_ptr<Material> material;
            if (!material_path.empty())
            {
                material = ResourceCache::GetByPath<Material>(
                    material_path
                );
                if (!material && FileSystem::IsFile(material_path))
                {
                    material = ResourceCache::Load<Material>(
                        material_path
                    );
                }
            }
            if (!material)
            {
                material = ResourceCache::GetByName<Material>(
                    material_name
                );
            }
            if (material)
            {
                SetMaterial(material);
            }
        }
        else if (m_material_default)
        {
            // defer default material assignment - renderer may not be ready during load
            m_needs_default_material = true;
        }

        // flags
        m_scene->flags = node.attribute("flags").as_uint();

        // distances
        m_scene->max_distance_render = node.attribute("max_render_distance").as_float(FLT_MAX);
        m_scene->max_distance_shadow = node.attribute("max_shadow_distance").as_float(FLT_MAX);

        // per-render material overrides, missing attributes keep the nan default which means inherit
        auto load_override = [&node](const char* name, float& target)
        {
            if (auto attr = node.attribute(name))
            {
                target = attr.as_float();
            }
        };
        load_override("ovr_uv_tiling_x",    m_material_override.uv_tiling_x);
        load_override("ovr_uv_tiling_y",    m_material_override.uv_tiling_y);
        load_override("ovr_uv_offset_x",    m_material_override.uv_offset_x);
        load_override("ovr_uv_offset_y",    m_material_override.uv_offset_y);
        load_override("ovr_uv_rotation",    m_material_override.uv_rotation);
        load_override("ovr_uv_invert_x",    m_material_override.uv_invert_x);
        load_override("ovr_uv_invert_y",    m_material_override.uv_invert_y);
        load_override("ovr_uv_world_space", m_material_override.uv_world_space);

        // instances
        m_scene->instances.clear();
        pugi::xml_node instances_node = node.child("Instances");
        if (instances_node)
        {
            for (pugi::xml_node t_node : instances_node.children("Transform"))
            {
                std::stringstream ss(t_node.attribute("matrix").as_string());
                math::Matrix matrix;
                float m[16];
                for (int i = 0; i < 16; ++i)
                {
                    ss >> m[i];
                }
                if (!ss.fail())
                {
                    matrix = math::Matrix(m[0], m[1], m[2], m[3],
                        m[4], m[5], m[6], m[7],
                        m[8], m[9], m[10], m[11],
                        m[12], m[13], m[14], m[15]);
                    Instance instance;
                    instance.SetMatrix(matrix);
                    m_scene->instances.emplace_back(instance);
                }
            }
        }

        // use the mesh lod aabb, copying every vertex here used to dominate entity load time
        if (m_scene->mesh && GetLodCount() > 0)
        {
            m_bounding_box_mesh = GetLodAabb(0);
        }

        // update instance buffer and bounding boxes
        if (!m_scene->instances.empty())
        {
            SetInstances(m_scene->instances);
        }
        else if (m_scene->mesh)
        {
            Tick();
        }
    }

    void Render::Tick()
    {
        CountWorldWork(WorldWork::render_tick_calls);
        const bool was_visible = m_scene->is_visible;
        // deferred default material assignment (renderer may not be ready during load)
        if (m_needs_default_material)
        {
            if (Renderer::GetStandardMaterial())
            {
                SetDefaultMaterial();
                m_needs_default_material = false;
            }
        }

        UpdateAabb();
        UpdateFrustumAndDistanceCulling();

        CountWorldWork(WorldWork::render_visible, m_scene->is_visible);
        CountWorldWork(WorldWork::visibility_changes, was_visible != m_scene->is_visible);
        // lod only matters for visible geometry, off-screen props skip the coverage math
        if (m_scene->is_visible)
        {
            UpdateLodIndices();
        }
    }

    void Render::UpdateFrustumAndDistanceCulling()
    {
        CountWorldWork(WorldWork::cull_calls);
        Camera* camera = World::GetCamera();
        if (!camera)
        {
            m_scene->distance_squared = 0.0f;
            m_scene->is_visible       = true;
            return;
        }

        const BoundingBox& bounding_box = GetBoundingBox();
        const Vector3 center  = bounding_box.GetCenter();
        const Vector3 extents = bounding_box.GetExtents();

        // a non finite bbox would crash the frustum culler assert, treat it as invisible
        if (center.IsNaN() || extents.IsNaN())
        {
            SP_LOG_WARNING("non finite bbox on '%s', marking invisible", GetEntity() ? GetEntity()->GetObjectName().c_str() : "?");
            m_scene->is_visible       = false;
            m_scene->distance_squared = 0.0f;
            return;
        }

        const Vector3 camera_position = camera->GetEntity()->GetPosition();
        const float max_distance = m_scene->max_distance_render;
        const float max_distance_sq = max_distance * max_distance;

        if (!m_scene->is_visible && m_scene->distance_squared > max_distance_sq)
        {
            const float cam_move_sq = Vector3::DistanceSquared(camera_position, m_scene->cull_camera_position);
            if (cam_move_sq < 4.0f)
            {
                CountWorldWork(WorldWork::cull_cached_skips);
                return;
            }
        }
        CountWorldWork(WorldWork::cull_tests);
        m_scene->cull_camera_position = camera_position;

        const float radius = max(extents.x, max(extents.y, extents.z)) * 1.7320508f;
        const float reject_distance = max_distance + radius;
        const float center_distance_sq = Vector3::DistanceSquared(camera_position, center);
        if (center_distance_sq > reject_distance * reject_distance)
        {
            m_scene->is_visible       = false;
            m_scene->distance_squared = center_distance_sq;
            return;
        }

        if (!camera->IsInViewFrustum(bounding_box))
        {
            m_scene->is_visible = false;
            m_scene->distance_squared = center_distance_sq;
            return;
        }

        m_scene->distance_squared = Vector3::DistanceSquared(camera_position, bounding_box.GetClosestPoint(camera_position));
        m_scene->is_visible       = m_scene->distance_squared <= max_distance_sq;
    }

    void Render::RegisterForScripting(sol::state_view State)
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

    sol::reference Render::AsLua(sol::state_view state)
    {
        return sol::make_reference(state, this);
    }

    void Render::SetMesh(Mesh* mesh, const uint32_t sub_mesh_index)
    {
        if (!mesh)
        {
            SP_LOG_WARNING("Render::SetMesh called with null mesh");
            return;
        }

        if (m_owned_mesh.get() != mesh) m_owned_mesh.reset();
        // set mesh
        m_scene->mesh           = mesh;
        m_scene->sub_mesh_index = sub_mesh_index;

        // Mesh construction already computed this bound. Reuse it so each
        // imported instance does not copy and scan the same vertex buffer.
        if (GetLodCount() > 0)
        {
            m_bounding_box_mesh = GetLodAabb(0);
            m_bounding_box_dirty = true;
        }

        Tick(); // update bounding boxes, frustum and distance culling
    }

    void Render::SetOwnedMesh(const shared_ptr<Mesh>& mesh)
    {
        SetMesh(mesh.get());
        m_owned_mesh = mesh;
    }

    void Render::SetMesh(const MeshType type)
    {
        SetMesh(Renderer::GetStandardMesh(type).get());
    }

    void Render::ClearMesh()
    {
        m_scene->mesh              = nullptr;
        m_owned_mesh.reset();
        m_scene->sub_mesh_index    = 0;
        m_bounding_box_mesh = BoundingBox::Unit;
        m_bounding_box_dirty = true;
        m_scene->lod_index          = 0;
    }

    void Render::GetGeometry(vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices) const
    {
        // a null mesh is a valid transient state, procedural meshes like roads are generated after load
        if (!m_scene->mesh)
        {
            return;
        }
        m_scene->mesh->GetGeometry(m_scene->sub_mesh_index, indices, vertices);
    }

    void Render::SetMaterial(const shared_ptr<Material>& material)
    {
        SP_ASSERT(material != nullptr);

        m_material_default = false;

        // cache it so it can be serialized/deserialized
        m_scene->material = ResourceCache::Cache(material).get();
        if (m_scene->material == nullptr)
        {
            SP_LOG_ERROR("Material was unable to be cached, and failed to be set.")
            return;
        }

        // pack textures, generate mips, compress, upload to GPU
        if (m_scene->material->GetResourceState() == ResourceState::Max)
        {
            m_scene->material->PrepareForGpu();
        }

        // use the cached mesh bounds, copying vertices dominates large prefab loads
        if (m_scene->mesh && GetLodCount() > 0)
        {
            const Vector3 size = GetLodAabb(0).GetSize();
            material->SetProperty(
                MaterialProperty::WorldWidth,
                size.x
            );
            material->SetProperty(
                MaterialProperty::WorldHeight,
                size.y
            );
        }
    }

    void Render::SetMaterial(const string& file_path)
    {
        auto material = make_shared<Material>();

        material->LoadFromFile(file_path);

        SetMaterial(material);
    }

    void Render::SetDefaultMaterial()
    {
        SetMaterial(Renderer::GetStandardMaterial());
        m_material_default = true;
    }

    string Render::GetMaterialName() const
    {
        return m_scene->material ? m_scene->material->GetObjectName() : "";
    }

    uint32_t Render::GetIndexOffset(const uint32_t lod) const
    {
        if (const MeshLod* mesh_lod = get_mesh_lod(m_scene->mesh, m_scene->sub_mesh_index, lod))
        {
            return m_scene->mesh->GetGlobalIndexOffset() + mesh_lod->index_offset;
        }

        return 0;
    }

    uint32_t Render::GetIndexCount(const uint32_t lod) const
    {
        if (const MeshLod* mesh_lod = get_mesh_lod(m_scene->mesh, m_scene->sub_mesh_index, lod))
        {
            return mesh_lod->index_count;
        }

        return 0;
    }

    uint32_t Render::GetVertexOffset(const uint32_t lod) const
    {
        if (const MeshLod* mesh_lod = get_mesh_lod(m_scene->mesh, m_scene->sub_mesh_index, lod))
        {
            return m_scene->mesh->GetGlobalVertexOffset() + mesh_lod->vertex_offset;
        }

        return 0;
    }

    uint32_t Render::GetVertexCount(const uint32_t lod) const
    {
        if (const MeshLod* mesh_lod = get_mesh_lod(m_scene->mesh, m_scene->sub_mesh_index, lod))
        {
            return mesh_lod->vertex_count;
        }

        return 0;
    }

    uint32_t Render::GetMeshletOffset(const uint32_t lod) const
    {
        if (const MeshLod* mesh_lod = get_mesh_lod(m_scene->mesh, m_scene->sub_mesh_index, lod))
        {
            return mesh_lod->meshlet_offset;
        }

        return 0;
    }

    uint32_t Render::GetMeshletCount(const uint32_t lod) const
    {
        if (const MeshLod* mesh_lod = get_mesh_lod(m_scene->mesh, m_scene->sub_mesh_index, lod))
        {
            return mesh_lod->meshlet_count;
        }

        return 0;
    }

    uint32_t Render::GetGlobalMeshletOffset() const
    {
        return m_scene->mesh ? m_scene->mesh->GetGlobalMeshletOffset() : 0;
    }

    const BoundingBox& Render::GetLodAabb(const uint32_t lod) const
    {
        if (const MeshLod* mesh_lod = get_mesh_lod(m_scene->mesh, m_scene->sub_mesh_index, lod))
        {
            return mesh_lod->aabb;
        }

        return BoundingBox::Unit;
    }

    RHI_Buffer* Render::GetIndexBuffer() const
	{
        if (!m_scene->mesh)
        {
            return nullptr;
        }

        return m_scene->mesh->GetIndexBuffer();
	}

    RHI_Buffer* Render::GetVertexBuffer() const
    {
        if (!m_scene->mesh)
        {
            return nullptr;
        }

        return m_scene->mesh->GetVertexBuffer();
    }

    const string& Render::GetMeshName() const
    {
        static string no_mesh = "N/A";
        if (!m_scene->mesh)
        {
            return no_mesh;
        }

        return m_scene->mesh->GetObjectName();
    }

    void Render::BuildAccelerationStructure()
    {
        if (!m_scene->mesh)
        {
            return;
        }

        m_scene->mesh->BuildAccelerationStructure(m_scene->sub_mesh_index, m_allow_blas_update);
    }

    void Render::RefitAccelerationStructure()
    {
        if (!m_scene->mesh)
        {
            return;
        }

        m_scene->mesh->RefitBlas(m_scene->sub_mesh_index);
    }

    void Render::InvalidateAccelerationStructure()
    {
        if (m_scene->mesh)
        {
            m_scene->mesh->InvalidateBlas(m_scene->sub_mesh_index);
        }
    }

    uint64_t Render::GetAccelerationStructureDeviceAddress() const
    {
        if (!m_scene->mesh)
        {
            return 0;
        }

        RHI_AccelerationStructure* blas = m_scene->mesh->GetBlas(m_scene->sub_mesh_index);
        if (!blas)
        {
            return 0;
        }

        return blas->GetDeviceAddress();
    }

    Matrix Render::GetInstance(const uint32_t index, const bool to_world)
    {
        return to_world ? m_scene->instances[index].GetMatrix() * GetEntity()->GetMatrix() : m_scene->instances[index].GetMatrix();
    }

    Vector3 Render::GetInstancePosition(uint32_t index, const Matrix& world) const
    {
        const Instance& instance = m_scene->instances[index];
        return world * Vector3(instance.position_x, instance.position_y, instance.position_z);
    }

    void Render::SetInstances(const vector<Instance>& instances, bool refresh_bounds)
    {
        // a scattered prop owns one physics actor per instance, the slot lookup is free when there is none
        if (Physics* physics = GetEntity()->GetComponent<Physics>())
        {
            physics->OnInstancesChanged();
        }

        if (instances.empty())
        {
            // offset 0 makes the draw read identity, the owned slot stays so a refill can reuse it
            m_scene->instances.clear();
            m_instance_bounds.clear();
            m_instance_wind_padding.clear();
            m_instance_bounds_groups.clear();
            m_instance_bounds_order.clear();
            m_global_instance_offset = 0;
            m_bounding_box_dirty     = true;
            return;
        }

        m_scene->instances = instances;
        const uint32_t count = static_cast<uint32_t>(m_scene->instances.size());

        // rewrite the slot we already own when the new set fits, appending every time leaks the pool
        // until the global geometry buffer has to reallocate and re-upload the entire world
        bool reused = false;
        if (m_global_instance_slot != 0 && count <= m_global_instance_slot_capacity)
        {
            reused = GeometryBuffer::UpdateInstances(m_scene->instances.data(), m_global_instance_slot, count);
        }

        if (!reused)
        {
            // append into the global instance pool so the indirect path can read instance attrs by offset + sv_instanceid
            m_global_instance_slot          = GeometryBuffer::AppendInstances(m_scene->instances.data(), count);
            m_global_instance_slot_capacity = count;
        }

        m_global_instance_offset = m_global_instance_slot;
        m_bounding_box_dirty     = true;
        if (refresh_bounds) Tick(); // update bounding boxes, frustum and distance culling
    }

    void Render::RefreshBounds(const vector<Render*>& renders)
    {
        if (renders.empty()) return;
        // Transforms and inherited active states are already resolved by the
        // scene setters. Do not mutate the scene until the workers join.
        ThreadPool::ParallelLoop([&](uint32_t begin, uint32_t end)
        {
            for (uint32_t i = begin; i < end; ++i) renders[i]->UpdateAabb();
        }, static_cast<uint32_t>(renders.size()));
        for (Render* render : renders) render->Tick();
    }

    void Render::SetInstances(const vector<Matrix>& transforms)
    {
        if (transforms.empty())
        {
            SetInstances(vector<Instance>{});
            return;
        }

        // convert matrices to instances
        vector<Instance> instances;
        instances.reserve(transforms.size());
        for (const auto& transform : transforms)
        {
            Instance instance;
            instance.SetMatrix(transform);
            instances.emplace_back(instance);
        }

        // call instance overload
        SetInstances(instances);
    }

    uint32_t Render::GetLodCount() const
    {
        if (!m_scene->mesh)
        {
            return 0;
        }

        // bounds checked, shutdown can clear submeshes while a dangling raw pointer still looks non null
        return m_scene->mesh->GetLodCount(m_scene->sub_mesh_index);
    }

    void Render::SetFlag(const RenderFlags flag, const bool enable /*= true*/)
    {
        bool enabled      = false;
        bool disabled     = false;
        bool flag_present = m_scene->flags & flag;

        if (enable && !flag_present)
        {
            m_scene->flags |= static_cast<uint32_t>(flag);
            enabled  = true;

        }
        else if (!enable && flag_present)
        {
            m_scene->flags  &= ~static_cast<uint32_t>(flag);
            disabled  = true;
        }
    }

    void Render::SetBoundingBoxOverride(const BoundingBox& world_box)
    {
        GetEntity()->WakePhysicsPreTick();
        m_scene->bounding_box = world_box;
        m_bounding_box_override = true;
        m_bounding_box_dirty = false;
        UpdateFrustumAndDistanceCulling();
        UpdateLodIndices();
    }

    void Render::ClearBoundingBoxOverride()
    {
        if (!m_bounding_box_override)
        {
            return;
        }

        m_bounding_box_override = false;
        m_bounding_box_dirty = true;
        UpdateAabb();
        UpdateFrustumAndDistanceCulling();
        UpdateLodIndices();
    }

    void Render::UpdateAabb()
    {
        CountWorldWork(WorldWork::bounds_checks);
        if (m_bounding_box_override)
        {
            return;
        }

        Entity* entity = GetEntity();
        const bool active = entity && entity->GetActive();
        const uint64_t revision = entity ? entity->GetTransformRevision() : 0;
        // Static bounds need no matrix copy, validation or comparison every frame.
        // Parent movement increments descendant revisions; active changes still
        // switch between the entity matrix and the historical identity behavior.
        if (!m_bounding_box_dirty && revision == m_bounds_transform_revision && active == m_bounds_entity_active)
            return;
        const Matrix transform = active ? entity->GetMatrix() : Matrix::Identity;

        // refuse to fold a non finite transform into the world bbox, doing so would
        // poison m_scene->bounding_box with NaN and trip the frustum culler assert downstream
        if (!transform.IsFinite())
        {
            SP_LOG_WARNING("non finite world matrix on '%s', keeping last bbox", GetEntity() ? GetEntity()->GetObjectName().c_str() : "?");
            return;
        }

        m_bounds_transform_revision = revision;
        m_bounds_entity_active = active;
        if (m_bounding_box_dirty || m_transform_previous != transform)
        {
            GetEntity()->WakePhysicsPreTick();
            CountWorldWork(WorldWork::bounds_rebuilt);
            CountWorldWork(WorldWork::bounds_instances_rebuilt, m_scene->instances.size());
            if (m_scene->instances.empty()) // non-instanced
            {
                m_scene->bounding_box = m_bounding_box_mesh * transform;
            }
            else // instanced
            {
                // Cache initial spatial preparation, not live transform changes.
                // Packed transforms, mesh bounds and the parent matrix fully define
                // these bounds, wind envelopes and Morton groups.
                const bool cache_bounds = m_instance_bounds.empty() && m_scene->instances.size() >= 256;
                generated_cache::Hash bounds_hash;
                std::filesystem::path bounds_path;
                vector<Vector3> cached_box;
                if (cache_bounds)
                {
                    bounds_hash.Add(uint32_t{1}); // bounds/wind/grouping algorithm version
                    bounds_hash.Add(m_scene->instances);
                    bounds_hash.Add(m_bounding_box_mesh.GetMin());
                    bounds_hash.Add(m_bounding_box_mesh.GetMax());
                    bounds_hash.Add(transform);
                    bounds_path = generated_cache::Path(World::GetResourceDirectory(), "instance_bounds", bounds_hash.value);
                }
                const bool cached = cache_bounds && generated_cache::Load(bounds_path, bounds_hash.value,
                    cached_box, m_instance_bounds, m_instance_wind_padding, m_instance_bounds_order, m_instance_bounds_groups);
                if (cached && cached_box.size() == 2 && m_instance_bounds.size() == m_scene->instances.size() &&
                    m_instance_wind_padding.size() == m_scene->instances.size() && m_instance_bounds_order.size() == m_scene->instances.size() &&
                    m_instance_bounds_groups.size() == (m_scene->instances.size() + 31) / 32)
                {
                    m_scene->bounding_box = BoundingBox(cached_box[0], cached_box[1]);
                    m_transform_previous = transform;
                    m_bounding_box_dirty = false;
                    return;
                }
                m_scene->bounding_box = BoundingBox(Vector3::Infinity, Vector3::InfinityNeg);
                m_instance_bounds.resize(m_scene->instances.size());
                m_instance_wind_padding.resize(m_scene->instances.size());
                for (size_t i = 0; i < m_scene->instances.size(); ++i)
                {
                    const Matrix world_instance = m_scene->instances[i].GetMatrix() * transform;
                    const BoundingBox bounds = m_bounding_box_mesh * world_instance;
                    m_instance_bounds[i] = bounds;
                    // Same conservative envelope as tree_wind_cull_padding in common_culling.hlsl.
                    m_instance_wind_padding[i] = ((bounds.GetCenter() - world_instance.GetTranslation()).Length()
                        + bounds.GetExtents().Length()) * 0.07f + 0.03f;
                    m_scene->bounding_box.Merge(bounds);
                }

                // Spatial groups accelerate shadow queries without reordering the
                // authored instances, GPU pool or physics instance identifiers.
                const auto spread_bits = [](uint32_t x)
                {
                    x = (x | (x << 16)) & 0x030000FFu;
                    x = (x | (x << 8)) & 0x0300F00Fu;
                    x = (x | (x << 4)) & 0x030C30C3u;
                    return (x | (x << 2)) & 0x09249249u;
                };
                const Vector3 origin = m_scene->bounding_box.GetMin();
                const Vector3 size = m_scene->bounding_box.GetSize();
                vector<uint64_t> keys(m_scene->instances.size());
                for (uint32_t i = 0; i < m_scene->instances.size(); ++i)
                {
                    const Vector3 p = m_instance_bounds[i].GetCenter() - origin;
                    const uint32_t x = static_cast<uint32_t>(clamp(p.x / max(size.x, 0.001f), 0.0f, 1.0f) * 1023.0f);
                    const uint32_t y = static_cast<uint32_t>(clamp(p.y / max(size.y, 0.001f), 0.0f, 1.0f) * 1023.0f);
                    const uint32_t z = static_cast<uint32_t>(clamp(p.z / max(size.z, 0.001f), 0.0f, 1.0f) * 1023.0f);
                    const uint32_t morton = spread_bits(x) | (spread_bits(y) << 1) | (spread_bits(z) << 2);
                    keys[i] = (static_cast<uint64_t>(morton) << 32) | i;
                }
                sort(keys.begin(), keys.end());
                m_instance_bounds_order.resize(keys.size());
                m_instance_bounds_groups.clear();
                constexpr uint32_t group_size = 32;
                for (uint32_t begin = 0; begin < keys.size(); begin += group_size)
                {
                    const uint32_t count = min(group_size, static_cast<uint32_t>(keys.size()) - begin);
                    BoundingBox group_bounds;
                    for (uint32_t j = begin; j < begin + count; ++j)
                    {
                        const uint32_t i = static_cast<uint32_t>(keys[j]);
                        m_instance_bounds_order[j] = i;
                        const BoundingBox& bounds = m_instance_bounds[i];
                        const float padding = m_instance_wind_padding[i];
                        const Vector3 pad(padding, padding, padding);
                        group_bounds.Merge(BoundingBox(bounds.GetMin() - pad, bounds.GetMax() + pad));
                    }
                    m_instance_bounds_groups.push_back({group_bounds, begin, count});
                }
                if (cache_bounds)
                {
                    cached_box = {m_scene->bounding_box.GetMin(), m_scene->bounding_box.GetMax()};
                    generated_cache::Save(bounds_path, bounds_hash.value, cached_box, m_instance_bounds,
                        m_instance_wind_padding, m_instance_bounds_order, m_instance_bounds_groups);
                }
            }
            m_transform_previous = transform;
            m_bounding_box_dirty = false;
        }
    }

    void Render::UpdateLodIndices()
    {
        CountWorldWork(WorldWork::lod_updates);
        // screen coverage handles distance, object size and fov uniformly with no per-type special cases

        const uint32_t lod_count = GetLodCount();
        if (lod_count == 0)
        {
            m_scene->lod_index = 0;
            return;
        }

        Camera* camera = World::GetCamera();
        if (!camera)
        {
            m_scene->lod_index = lod_count - 1;
            return;
        }

        const BoundingBox& box        = GetBoundingBox();
        const Vector3 camera_position = camera->GetEntity()->GetPosition();

        // camera inside bounding box = maximum detail
        if (box.Contains(camera_position))
        {
            m_scene->lod_index = 0;
            return;
        }

        // distance from camera to closest point on bounding box
        Vector3 closest_point = box.GetClosestPoint(camera_position);
        float distance        = max((closest_point - camera_position).Length(), 0.001f);

        // an instanced renderable's box spans every instance it carries, so a tile of trees measures
        // hundreds of metres across and scores full coverage from any distance, which pins the whole
        // tile at lod 0 forever, the coverage has to come from the size of one instance while the box
        // keeps supplying the distance
        Vector3 measured_extents = box.GetExtents();
        if (HasInstancing() && GetEntity() && !m_scene->instances.empty())
        {
            const Vector3 entity_scale = GetEntity()->GetScale();
            const Vector3 inst_scale   = m_scene->instances[0].GetMatrix().GetScale();
            const Vector3 mesh_extents = GetLodAabb(0).GetExtents();
            measured_extents           = Vector3(
                mesh_extents.x * abs(entity_scale.x * inst_scale.x),
                mesh_extents.y * abs(entity_scale.y * inst_scale.y),
                mesh_extents.z * abs(entity_scale.z * inst_scale.z)
            );
        }

        // compute screen-space coverage: fraction of vertical screen space the object covers
        // screen_fraction = (object_diameter) / (visible_height_at_distance)
        // visible_height_at_distance = 2 * distance * tan(fov_v / 2)
        float bounding_diameter = measured_extents.Length() * 2.0f;
        float tan_half_fov      = tan(camera->GetFovVerticalRad() * 0.5f);
        float screen_fraction   = bounding_diameter / (2.0f * distance * tan_half_fov);

        // Keep full detail for close objects. Five-percent coverage kept large
        // trees at LOD 0 hundreds of metres away, overwhelming the geometry passes.
        // Keep these coverage thresholds in sync with sphere_lod_index.
        static constexpr array<float, 5> screen_thresholds =
        {
            0.20f,
            0.10f,
            0.048f,
            0.024f,
            0.012f
        };

        // hysteresis against lod popping, a change requires passing the threshold by 10 percent in either direction
        constexpr float hysteresis = 1.1f;

        uint32_t new_lod = lod_count - 1;
        for (uint32_t i = 0; i < min(lod_count, static_cast<uint32_t>(screen_thresholds.size())); i++)
        {
            float threshold = screen_thresholds[i];

            // apply hysteresis based on relationship to current lod
            if (i < m_scene->lod_index)
            {
                // upgrading to higher detail: raise the bar
                threshold *= hysteresis;
            }
            else if (i == m_scene->lod_index)
            {
                // staying at current lod: lower the bar (easier to stay)
                threshold /= hysteresis;
            }

            if (screen_fraction >= threshold)
            {
                new_lod = i;
                break;
            }
        }

        m_scene->lod_index = clamp(new_lod, 0u, lod_count - 1);
    }
}
