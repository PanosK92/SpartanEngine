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

//= INCLUDES ============================
#include "pch.h"
#include "Renderable.h"
#include "Camera.h"
#include "../Entity.h"
#include "../RHI/RHI_Buffer.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Material.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    namespace grid_partitioning
    {
        // this namespace organizes 3D objects into a grid layout, grouping instances into grid cells
        // it enables optimized rendering by allowing culling of non-visible chunks
    
        const uint32_t cell_size = 64; // meters
    
        struct GridKey
        {
            int32_t x, y, z;
        
            bool operator==(const GridKey& other) const
            {
                return x == other.x && y == other.y && z == other.z;
            }
        };
    
        struct GridKeyHash
        {
            size_t operator()(const GridKey& k) const
            {
                size_t result = 0;
                uint32_t ux = static_cast<uint32_t>(k.x);
                uint32_t uy = static_cast<uint32_t>(k.y);
                uint32_t uz = static_cast<uint32_t>(k.z);
                for (uint32_t i = 0; i < (sizeof(uint32_t) * 8); i++)
                {
                    result |= ((ux & (1u << i)) << (2 * i)) |
                              ((uy & (1u << i)) << (2 * i + 1)) |
                              ((uz & (1u << i)) << (2 * i + 2));
                }
                return result;
            }
    
            static GridKey get_key(const Vector3& position)
            {
                return
                {
                    static_cast<int32_t>(floor(position.x / static_cast<float>(cell_size))),
                    static_cast<int32_t>(floor(position.y / static_cast<float>(cell_size))),
                    static_cast<int32_t>(floor(position.z / static_cast<float>(cell_size)))
                };
            }
        };
    
        void reorder_instances_into_cell_chunks(vector<Matrix>& instance_transforms, vector<uint32_t>& cell_end_indices)
        {
            // populate the grid map
            unordered_map<GridKey, vector<Matrix>, GridKeyHash> grid_map;
            for (const auto& instance : instance_transforms)
            {
                Vector3 position = instance.GetTranslation();
    
                GridKey key = GridKeyHash::get_key(position);
                grid_map[key].push_back(instance);
            }
    
            // reorder instances based on grid map
            instance_transforms.clear();
            cell_end_indices.clear();
            uint32_t index = 0;
            for (const auto& [key, transforms] : grid_map)
            {
                instance_transforms.insert(instance_transforms.end(), transforms.begin(), transforms.end());
                index += static_cast<uint32_t>(transforms.size());
                cell_end_indices.push_back(index);
            }
        }
    }

    Renderable::Renderable(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material_default, bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material,         Material*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flags,            uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mesh,             Mesh*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box_mesh,     BoundingBox);
    }

    Renderable::~Renderable()
    {
        m_mesh = nullptr;
    }

    void Renderable::Serialize(FileStream* stream)
    {
        // mesh
        stream->Write(m_bounding_box_mesh);
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
        stream->Read(&m_bounding_box_mesh);
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
        // update bounding boxes on transform change
        if (Entity* entity = GetEntity())
        {
            // wait for model loading to finish and entity activation before reading its transform, as accessing it prematurely can cause NaNs and trigger an assertion
            if (entity->IsActive())
            { 
                const Matrix& transform = entity->GetMatrix();

                if (m_bounding_box_dirty || m_transform_previous != transform)
                {
                    // bounding box that contains all instances
                    if (m_instances.empty())
                    {
                        m_bounding_box = m_bounding_box_mesh * transform;
                    }
                    else // transformed instances
                    {
                        m_bounding_box = BoundingBox::Undefined;
                        m_bounding_box_instances.clear();
                        m_bounding_box_instances.reserve(m_instances.size());
                        m_bounding_box_instances.resize(m_instances.size());
                        for (uint32_t i = 0; i < static_cast<uint32_t>(m_instances.size()); i++)
                        {
                            const Matrix& instance_transform = m_instances[i];
                            m_bounding_box_instances[i]      = m_bounding_box_mesh * (transform * instance_transform); // 1. bounding box of the instance
                            m_bounding_box.Merge(m_bounding_box_instances[i]);                                         // 2. bounding box of all instances
                        }

                        // bounding boxes of instance groups
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
                                    BoundingBox bounding_box_instance = m_bounding_box_mesh * (transform * m_instances[i]);
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
            }
        }

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
            m_bounding_box_mesh = BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size()));
            SP_ASSERT(m_bounding_box_mesh != BoundingBox::Undefined);
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

        // compute world dimensions
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
                Vector3 position = Vector3(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
                min_height       = min(min_height, position.y);
                max_height       = max(max_height, position.y);
                min_width        = min(min_width, position.x);
                max_width        = max(max_width, position.x);
            }

            material->SetProperty(MaterialProperty::WorldWidth,  max_width - min_width);
            material->SetProperty(MaterialProperty::WorldHeight, max_height - min_height);
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

    uint32_t Renderable::GetInstanceGroupStartIndex(uint32_t group_index) const
    {
        return group_index == 0 ? 0 : m_instance_group_end_indices[group_index - 1];
    }
    
    uint32_t Renderable::GetInstanceGroupCount(uint32_t group_index) const
    {
        uint32_t start_index = GetInstanceGroupStartIndex(group_index);
        uint32_t end_index   = m_instance_group_end_indices[group_index];

        return end_index - start_index;
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
        if (Camera* camera = World::GetCamera())
        {
            Vector3 camera_position = camera->GetEntity()->GetPosition();
    
            if (HasInstancing())
            {
                for (uint32_t group_index = 0; group_index < GetInstanceGroupCount(); group_index++)
                {
                    const BoundingBox& bounding_box = GetBoundingBoxInstanceGroup(group_index);

                    // first, check if the bounding box is in the frustum
                    if (camera->IsInViewFrustum(bounding_box))
                    {
                        // only if in frustum, calculate distance
                        m_distance_squared[group_index] = Vector3::DistanceSquared(camera_position, bounding_box.GetClosestPoint(camera_position));
                        m_is_visible[group_index]       = m_distance_squared[group_index] <= m_max_render_distance * m_max_render_distance;
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
                const BoundingBox& bounding_box = GetBoundingBox();

                // first, check if the bounding box is in the frustum
                if (camera->IsInViewFrustum(bounding_box))
                {
                    // only if in frustum, calculate distance
                    m_distance_squared[0] = Vector3::DistanceSquared(camera_position, bounding_box.GetClosestPoint(camera_position));
                    m_is_visible[0]       = m_distance_squared[0]  <= m_max_render_distance * m_max_render_distance;
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
            m_distance_squared.fill(0.0f);
            m_is_visible.fill(true);
        }
    }

    void Renderable::UpdateLodIndices()
    {
        // note: using projected angle for LOD selection, which is more perceptually accurate
        // than screen height ratio, for example it will be more consistent across different resolutions
    
        // static thresholds for projected angle (defined in degrees, converted to radians)
        static const array<float, 4> lod_angle_thresholds =
        {
            30.0f * math::deg_to_rad,
            15.0f * math::deg_to_rad,
            7.5f  * math::deg_to_rad,
            3.2f  * math::deg_to_rad 
        };
        const uint32_t lod_count  = GetLodCount();
        const uint32_t max_lod    = lod_count - 1;
        Camera* camera            = World::GetCamera();
    
        // if no camera, use lowest detail lod for all
        if (!camera)
        {
            m_lod_indices.fill(max_lod);
            return;
        }
    
        // lambda to compute lod index using projected angle
        const Vector3 camera_position = camera->GetEntity()->GetPosition();
        auto compute_lod_index = [&](const BoundingBox& box, bool is_visible, uint32_t index)
        {
            // if not visible, use lowest detail lod
            if (!is_visible)
            {
                m_lod_indices[index] = max_lod;
                return;
            }
    
            // compute bounding sphere from aabb for radius
            float radius = box.GetExtents().Length(); // radius is length of extents vector
    
            // compute distance from camera to closest point on AABB
            Vector3 closest_point = box.GetClosestPoint(camera_position);
            Vector3 to_closest    = closest_point - camera_position;
            float distance        = to_closest.Length();

            // if camera is inside or very close to the AABB, use highest detail lod
            if (box.Contains(camera_position))
            {
                m_lod_indices[index] = 0;
                return;
            }
    
            // compute projected angle (in radians) using the sphere approximation
            float projected_angle = 2.0f * atan(radius / distance);

            //SP_LOG_INFO("Projected angle: %f, distance: %f, radius: %f", projected_angle * math::rad_to_deg, distance, radius);

            // determine lod index based on projected angle
            uint32_t lod_index = max_lod;
            for (uint32_t i = 0; i < lod_count - 1; i++)
            {
                if (projected_angle > lod_angle_thresholds[i])
                {
                    lod_index = i;
                    break;
                }
            }
    
            m_lod_indices[index] = lod_index;
        };
    
        if (HasInstancing())
        {
            for (uint32_t group_index = 0; group_index < GetInstanceGroupCount(); group_index++)
            {
                const BoundingBox& box = GetBoundingBoxInstanceGroup(group_index);
                compute_lod_index(box, IsVisible(group_index), group_index);
            }
        }
        else
        {
            const BoundingBox& box = GetBoundingBox();
            compute_lod_index(box, IsVisible(), 0);
        }
    }
}
