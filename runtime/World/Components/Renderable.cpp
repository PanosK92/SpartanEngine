/*
Copyright(c) 2016-2025 Panos Karabelas

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
#include "Renderable.h"
#include "Camera.h"
#include "../Entity.h"
#include "../RHI/RHI_Buffer.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/GridPartitioning.h"
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    Renderable::Renderable(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material_default, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material,         Material*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flags,            uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mesh,             Mesh*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box,     BoundingBox);
    }

    Renderable::~Renderable()
    {
        m_mesh = nullptr;
    }

    void Renderable::Serialize(FileStream* stream)
    {
        // mesh
        stream->Write(m_bounding_box);
        MeshType mesh_type = m_mesh ? m_mesh->GetType() : MeshType::Max;
        stream->Write(static_cast<uint32_t>(mesh_type));
        if (mesh_type == MeshType::Max)
        { 
            stream->Write(m_mesh ? m_mesh->GetObjectName() : "");
        }

        // material
        stream->Write(m_flags);
        stream->Write(m_material_default);
        if (!m_material_default)
        {
            stream->Write(m_material ? m_material->GetObjectName() : "");
        }
    }

    void Renderable::Deserialize(FileStream* stream)
    {
        // geometry
        stream->Read(&m_bounding_box);
        MeshType mesh_type = static_cast<MeshType>(stream->ReadAs<uint32_t>());
        if (mesh_type == MeshType::Max)
        {
            string model_name;
            stream->Read(&model_name);
            m_mesh = ResourceCache::GetByName<Mesh>(model_name).get();
        }
        else if (mesh_type != MeshType::Max)
        {
            SetMesh(mesh_type);
        }

        // material
        stream->Read(&m_flags);
        stream->Read(&m_material_default);
        if (m_material_default)
        {
            SetDefaultMaterial();
        }
        else
        {
            string material_name;
            stream->Read(&material_name);
            m_material = ResourceCache::GetByName<Material>(material_name).get();
        }
    }

    void Renderable::OnTick()
    {
        UpdateFrustumAndDistanceCulling();
        UpdateLodIndices();
    }

    void Renderable::SetMesh(Mesh* mesh, const uint32_t sub_mesh_index)
    {
        // set mesh
        {
            m_mesh             = mesh;
            m_sub_mesh_index   = sub_mesh_index;
            const MeshLod& lod = mesh->GetSubMesh(sub_mesh_index).lods[0];
            SP_ASSERT(lod.index_count  != 0);
            SP_ASSERT(lod.vertex_count != 0);
        }

        // compute and set bounding box
        {
            vector<RHI_Vertex_PosTexNorTan> vertices;
            mesh->GetGeometry(sub_mesh_index, nullptr, &vertices);
            m_bounding_box = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
            SP_ASSERT(m_bounding_box != BoundingBox::Undefined);
        }
    }

    void Renderable::SetMesh(const MeshType type)
    {
        SetMesh(Renderer::GetStandardMesh(type).get());
    }

    void Renderable::GetGeometry(vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices) const
    {
        m_mesh->GetGeometry(m_sub_mesh_index, indices, vertices);
    }

    const BoundingBox& Renderable::GetBoundingBox(const BoundingBoxType type, const uint32_t index)
    {
        if (m_bounding_box_dirty || m_transform_previous != GetEntity()->GetMatrix())
        {
            Matrix transform = GetEntity()->GetMatrix();

            // bounding box that contains all instances
            if (m_instances.empty())
            {
                m_bounding_box_transformed = m_bounding_box.Transform(transform);
            }
            else // transformed instances
            {
                m_bounding_box_transformed = BoundingBox::Undefined;
                m_bounding_box_instances.clear();
                m_bounding_box_instances.reserve(m_instances.size());
                m_bounding_box_instances.resize(m_instances.size());
                for (uint32_t i = 0; i < static_cast<uint32_t>(m_instances.size()); i++)
                {
                    const Matrix& instance_transform = m_instances[i];
                    m_bounding_box_instances[i]      = m_bounding_box.Transform(transform * instance_transform); // 1. bounding box of the instance
                    m_bounding_box_transformed.Merge(m_bounding_box_instances[i]);                               // 2. bounding box of all instances
                }

                // 3. bounding boxes of instance groups
                {
                    // loop through each group end index
                    m_bounding_box_instance_group.clear();
                    uint32_t start_index = 0;
                    for (const uint32_t group_end_index : m_instance_group_end_indices)
                    {
                        // loop through the instances in this group
                        BoundingBox bounding_box_group = BoundingBox::Undefined;
                        for (uint32_t i = start_index; i < group_end_index; i++)
                        {
                            BoundingBox bounding_box_instance = m_bounding_box.Transform(transform * m_instances[i]);
                            bounding_box_group.Merge(bounding_box_instance);
                        }

                        m_bounding_box_instance_group.push_back(bounding_box_group);
                        start_index = group_end_index;
                    }
                }
            }

            m_transform_previous = transform;
            m_bounding_box_dirty = false;
        }

        if (type == BoundingBoxType::Mesh)
            return m_bounding_box;

        if (type == BoundingBoxType::Transformed)
            return m_bounding_box_transformed;

        if (type == BoundingBoxType::TransformedInstance)
            return m_bounding_box_instances[index];

        if (type == BoundingBoxType::TransformedInstanceGroup)
            return m_bounding_box_instance_group[index];

        return BoundingBox::Undefined;
    }
    
    void Renderable::SetMaterial(const shared_ptr<Material>& material)
    {
        SP_ASSERT(material != nullptr);

        m_material_default = false;

        // cache it so it can be serialized/deserialized
        m_material = ResourceCache::Cache(material).get();

        // pack textures, generate mips, compress, upload to GPU
        if (m_material->GetResourceState() == ResourceState::Max)
        { 
            m_material->PrepareForGpu();
        }

        // compute local dimensions
        {
            // acquire vertices
            vector<RHI_Vertex_PosTexNorTan> vertices;
            GetGeometry(nullptr, &vertices);
            SP_ASSERT(!vertices.empty());
            
            float min_height = FLT_MAX;
            float max_height = -FLT_MAX;
            float min_width  = FLT_MAX;
            float max_width  = -FLT_MAX;
            Matrix transform = HasInstancing() ? GetEntity()->GetMatrix() * GetInstanceTransform(0) : GetEntity()->GetMatrix();
            for (const RHI_Vertex_PosTexNorTan& vertex : vertices)
            {
                Vector3 position = Vector3(vertex.pos[0], vertex.pos[1], vertex.pos[2]) * transform;
                min_height       = min(min_height, position.y);
                max_height       = max(max_height, position.y);
                min_width        = min(min_width, position.x);
                max_width        = max(max_width, position.x);
            }

            material->SetProperty(MaterialProperty::LocalWidth,  max_width - min_width);
            material->SetProperty(MaterialProperty::LocalHeight, max_height - min_height);
        }
    }

    void Renderable::SetMaterial(const string& file_path)
    {
        auto material = make_shared<Material>();

        material->LoadFromFile(file_path);

        SetMaterial(material);
    }

    void Renderable::SetDefaultMaterial()
    {
        SetMaterial(Renderer::GetStandardMaterial());
        m_material_default = true;
    }

    string Renderable::GetMaterialName() const
    {
        return m_material ? m_material->GetObjectName() : "";
    }

    uint32_t Renderable::GetIndexOffset(const uint32_t lod) const
    {
        return m_mesh->GetSubMesh(m_sub_mesh_index).lods[lod].index_offset;
    }

    uint32_t Renderable::GetIndexCount(const uint32_t lod) const
    {
        return m_mesh->GetSubMesh(m_sub_mesh_index).lods[lod].index_count;
    }

    uint32_t Renderable::GetVertexOffset(const uint32_t lod) const
    {
        return m_mesh->GetSubMesh(m_sub_mesh_index).lods[lod].vertex_offset;
    }

    uint32_t Renderable::GetVertexCount(const uint32_t lod) const
    {
        return m_mesh->GetSubMesh(m_sub_mesh_index).lods[lod].vertex_count;
    }

    RHI_Buffer* Renderable::GetIndexBuffer() const
	{
        if (!m_mesh)
            return nullptr;

        return m_mesh->GetIndexBuffer();
	}

    RHI_Buffer* Renderable::GetVertexBuffer() const
    {
        if (!m_mesh)
            return nullptr;

        return m_mesh->GetVertexBuffer();
    }

    const string& Renderable::GetMeshName() const
    {
        static string no_mesh = "N/A";
        if (!m_mesh)
            return no_mesh;

        return m_mesh->GetObjectName();
    }

    void Renderable::SetInstances(const vector<Matrix>& instances)
    {
        m_instances = instances;

        grid_partitioning::reorder_instances_into_cell_chunks(m_instances, m_instance_group_end_indices);

        // we are mapping 4 Vector4s as 4 rows (see vulkan_pipeline.cpp, line 246) in order to get 1 matrix (HLSL side)
        // but the matrix memory layout is column-major, so we need to transpose to get it as row-major
        vector<Matrix> instances_transposed;
        instances_transposed.reserve(m_instances.size());
        for (const auto& instance : m_instances)
        {
            instances_transposed.push_back(instance.Transposed());
        }

        m_instance_buffer = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Instance,
            sizeof(instances_transposed[0]),
            static_cast<uint32_t>(instances_transposed.size()),
            static_cast<void*>(&instances_transposed[0]),
            false,
            "instance_buffer"
        );

        m_bounding_box_dirty = true;
    }

    uint32_t Renderable::GetLodCount() const
    {
        return static_cast<uint32_t>(m_mesh->GetSubMesh(m_sub_mesh_index).lods.size());
    }

    void Renderable::SetFlag(const RenderableFlags flag, const bool enable /*= true*/)
    {
        bool enabled      = false;
        bool disabled     = false;
        bool flag_present = m_flags & flag;

        if (enable && !flag_present)
        {
            m_flags |= static_cast<uint32_t>(flag);
            enabled  = true;

        }
        else if (!enable && flag_present)
        {
            m_flags  &= ~static_cast<uint32_t>(flag);
            disabled  = true;
        }
    }

    void Renderable::UpdateFrustumAndDistanceCulling()
    {
        if (Camera* camera = Renderer::GetCamera().get())
        {
            Vector3 camera_position = camera->GetEntity()->GetPosition();
    
            if (HasInstancing())
            {
                for (uint32_t group_index = 0; group_index < GetInstanceGroupCount(); group_index++)
                {
                    const BoundingBox& bounding_box = GetBoundingBox(BoundingBoxType::TransformedInstanceGroup, group_index);

                    // first, check if the bounding box is in the frustum
                    if (camera->IsInViewFrustum(bounding_box))
                    {
                        // only if in frustum, calculate distance
                        float distance_squared    = Vector3::DistanceSquared(camera_position, bounding_box.GetClosestPoint(camera_position));
                        m_is_visible[group_index] = distance_squared <= m_max_render_distance * m_max_render_distance;
                    }
                    else
                    {
                        // outside frustum, no need for distance check
                        m_is_visible[group_index] = false;
                    }
                }
            }
            else
            {
                const BoundingBox& bounding_box = GetBoundingBox(BoundingBoxType::Transformed);

                // first, check if the bounding box is in the frustum
                if (camera->IsInViewFrustum(bounding_box))
                {
                    // only if in frustum, calculate distance
                    m_distance_squared = Vector3::DistanceSquared(camera_position, bounding_box.GetClosestPoint(camera_position));
                    m_is_visible[0]    = m_distance_squared <= m_max_render_distance * m_max_render_distance;
                }
                else
                {
                    // outside frustum, no need for distance check
                    m_is_visible[0] = false;
                }
            }
        }
        else
        {
            m_distance_squared = 0.0f;
            m_is_visible.fill(true);
        }
    }

    void Renderable::UpdateLodIndices()
    {
        // thresholds are in decreasing order, higher ratios mean higher detail (lower lod index)
        static const array<float, mesh_lod_count> lod_thresholds = {0.5f, 0.25f, 0.05f}; // 40%, 20%, 10%
        const uint32_t lod_count                                 = GetLodCount();
        Camera* camera                                           = Renderer::GetCamera().get();
    
        if (!camera)
        {
            m_lod_indices.fill(lod_count - 1); // default to lowest LOD if no camera
            return;
        }
    
        const Matrix& view_projection = camera->GetViewProjectionMatrix();
        const Vector2 screen_size     = Renderer::GetResolutionRender();
        const Vector3 camera_position = camera->GetEntity()->GetPosition();
    
        // lambda to compute LOD index for a given bounding box and visibility flag
        auto compute_lod_index = [&](const BoundingBox& box, bool is_visible, uint32_t index)
        {
            if (!is_visible)
            {
                m_lod_indices[index] = lod_count - 1;
                return;
            }
    
            if (box.Contains(camera_position))
            {
                m_lod_indices[index] = 0;
                return;
            }
    
            array<Vector3, 8> corners;
            box.GetCorners(&corners);
    
            float min_y       = numeric_limits<float>::max();
            float max_y       = numeric_limits<float>::min();
            bool any_in_front = false;
            for (const auto& corner : corners)
            {
                Vector4 clip_pos = view_projection * Vector4(corner, 1.0f);
                if (clip_pos.w > 0.0f)
                {
                    any_in_front                = true;
                    float inv_w                 = 1.0f / clip_pos.w;
                    float ndc_y                 = clip_pos.y * inv_w;
                    float y_screen              = (1.0f - ndc_y) * 0.5f * screen_size.y;
                    if (y_screen < min_y) min_y = y_screen;
                    if (y_screen > max_y) max_y = y_screen;
                }
            }
    
            if (!any_in_front)
            {
                m_lod_indices[index] = lod_count - 1;
                return;
            }
    
            float height_in_screen_space = max_y - min_y;
            float screen_height_ratio = height_in_screen_space / screen_size.y;
    
            for (uint32_t i = 0; i < lod_count; i++)
            {
                if (screen_height_ratio > lod_thresholds[i])
                {
                    m_lod_indices[index] = i;
                    return;
                }
                if (i == lod_count - 1)
                {
                    m_lod_indices[index] = lod_count - 1;
                }
            }
        };
    
        if (HasInstancing())
        {
            for (uint32_t group_index = 0; group_index < GetInstanceGroupCount(); group_index++)
            {
                const BoundingBox& box = GetBoundingBox(BoundingBoxType::TransformedInstanceGroup, group_index);
                compute_lod_index(box, IsVisible(group_index), group_index);
            }
        }
        else
        {
            const BoundingBox& box = GetBoundingBox(BoundingBoxType::Transformed);
            compute_lod_index(box, IsVisible(), 0);
        }
    }
}
