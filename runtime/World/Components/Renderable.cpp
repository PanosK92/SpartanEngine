/*
Copyright(c) 2016-2024 Panos Karabelas

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
#include "../Entity.h"
#include "../Rendering/Renderer.h"
#include "../RHI/RHI_Buffer.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/GridPartitioning.h"
//===========================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Renderable::Renderable(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material_default,       bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material,               Material*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flags,                  uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometry_index_offset,  uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometry_index_count,   uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometry_vertex_offset, uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometry_vertex_count,  uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mesh,                   Mesh*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box,           BoundingBox);
    }

    Renderable::~Renderable()
    {
        m_mesh = nullptr;
    }
    
    void Renderable::Serialize(FileStream* stream)
    {
        // mesh
        stream->Write(m_geometry_index_offset);
        stream->Write(m_geometry_index_count);
        stream->Write(m_geometry_vertex_offset);
        stream->Write(m_geometry_vertex_count);
        stream->Write(m_bounding_box);
        MeshType mesh_type = m_mesh ? m_mesh->GetType() : MeshType::Max;
        stream->Write(static_cast<uint32_t>(mesh_type));
        if (mesh_type == MeshType::Custom)
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
        m_geometry_index_offset  = stream->ReadAs<uint32_t>();
        m_geometry_index_count   = stream->ReadAs<uint32_t>();
        m_geometry_vertex_offset = stream->ReadAs<uint32_t>();
        m_geometry_vertex_count  = stream->ReadAs<uint32_t>();
        stream->Read(&m_bounding_box);
        MeshType mesh_type = static_cast<MeshType>(stream->ReadAs<uint32_t>());
        if (mesh_type == MeshType::Custom)
        {
            string model_name;
            stream->Read(&model_name);
            m_mesh = ResourceCache::GetByName<Mesh>(model_name).get();
        }
        else if (mesh_type != MeshType::Max)
        {
            SetGeometry(mesh_type);
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

    void Renderable::SetGeometry(
        Mesh* mesh,
        const Math::BoundingBox aabb /*= Math::BoundingBox::Undefined*/,
        uint32_t index_offset  /*= 0*/, uint32_t index_count  /*= 0*/,
        uint32_t vertex_offset /*= 0*/, uint32_t vertex_count /*= 0 */
    )
    {
        m_mesh                       = mesh;
        m_bounding_box = aabb;
        m_geometry_index_offset      = index_offset;
        m_geometry_index_count       = index_count;
        m_geometry_vertex_offset     = vertex_offset;
        m_geometry_vertex_count      = vertex_count;

        if (m_geometry_index_count == 0)
        {
            m_geometry_index_count = m_mesh->GetIndexCount();
        }

        if (m_geometry_vertex_count == 0)
        {
            m_geometry_vertex_count = m_mesh->GetVertexCount();
        }

        if (m_bounding_box == BoundingBox::Undefined)
        {
            m_bounding_box = m_mesh->GetAabb();
        }

        SP_ASSERT(m_geometry_index_count       != 0);
        SP_ASSERT(m_geometry_vertex_count      != 0);
        SP_ASSERT(m_bounding_box != BoundingBox::Undefined);
    }

    void Renderable::SetGeometry(const MeshType type)
    {
        SetGeometry(Renderer::GetStandardMesh(type).get());
    }

    void Renderable::GetGeometry(vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices) const
    {
        SP_ASSERT_MSG(m_mesh != nullptr, "invalid mesh");
        m_mesh->GetGeometry(m_geometry_index_offset, m_geometry_index_count, m_geometry_vertex_offset, m_geometry_vertex_count, indices, vertices);
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
            m_material->Optimize();
        }
    }

    void Renderable::SetMaterial(const string& file_path)
    {
        auto material = make_shared<Material>();

        material->LoadFromFile(file_path, false);

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
}
