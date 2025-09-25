f/*
Copyright(c) 2015-2025 Panos Karabelas

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
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Renderer.h"
#include "../../Rendering/Material.h"
SP_WARNINGS_OFF
#include "../IO/pugixml.hpp"
SP_WARNINGS_ON
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace spartan::math;
//============================

namespace spartan
{
    Renderable::Renderable(Entity* entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material_default,  bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material,          Material*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_flags,             uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_mesh,              Mesh*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box,      BoundingBox);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box_mesh, BoundingBox);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_sub_mesh_index,    uint32_t);
    }

    Renderable::~Renderable()
    {
        m_mesh = nullptr;
    }

    void Renderable::Save(pugi::xml_node& node)
    {
        // mesh
        node.append_attribute("mesh_name")      = m_mesh ? m_mesh->GetObjectName().c_str() : "";
        node.append_attribute("sub_mesh_index") = m_sub_mesh_index;
    
        // material
        node.append_attribute("material_name")    = m_material && !m_material_default ? m_material->GetObjectName().c_str() : "";
        node.append_attribute("material_default") = m_material_default;
    
        // flags
        node.append_attribute("flags") = m_flags;
    
        // distances
        node.append_attribute("max_render_distance") = m_max_distance_render;
        node.append_attribute("max_shadow_distance") = m_max_distance_shadow;
    
        // instances
        pugi::xml_node instances_node = node.append_child("Instances");
        for (const auto& transform : m_instances)
        {
            pugi::xml_node t_node = instances_node.append_child("Transform");
            const float* data = transform.Data();
            stringstream ss;
            for (int i = 0; i < 16; i++)
            {
                ss << data[i] << (i < 15 ? " " : "");
            }
            t_node.append_attribute("matrix") = ss.str().c_str();
        }
    }
    
    void Renderable::Load(pugi::xml_node& node)
    {
        // mesh
        const string mesh_name = node.attribute("mesh_name").as_string();
        m_sub_mesh_index       = node.attribute("sub_mesh_index").as_uint();
        if (!mesh_name.empty())
        {
            m_mesh = ResourceCache::GetByName<Mesh>(mesh_name).get();
        }
    
        // material
        m_material_default         = node.attribute("material_default").as_bool(true);
        const string material_name = node.attribute("material_name").as_string();
        if (!material_name.empty() && !m_material_default)
        {
            m_material = ResourceCache::GetByName<Material>(material_name).get();
        }
        else if (m_material_default)
        {
            //SetDefaultMaterial(); /// cause crash, disable for now
        }
    
        // flags
        m_flags = node.attribute("flags").as_uint();
    
        // distances
        m_max_distance_render = node.attribute("max_render_distance").as_float(FLT_MAX);
        m_max_distance_shadow = node.attribute("max_shadow_distance").as_float(FLT_MAX);
    
        // instances
        m_instances.clear();
        pugi::xml_node instances_node = node.child("Instances");
        if (instances_node)
        {
            for (pugi::xml_node t_node : instances_node.children("Transform"))
            {
                stringstream ss(t_node.attribute("matrix").as_string());
                float m[16];
                for (int i = 0; i < 16; i++)
                {
                    ss >> m[i];
                }
                m_instances.emplace_back(math::Matrix(m));
            }
        }
    
        // update instance buffer and bounding boxes
        if (!m_instances.empty())
        {
            SetInstances(m_instances);
        }
        else if (m_mesh)
        {
            Tick();
        }
    }

    void Renderable::Tick()
    {
        UpdateAabb();
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
        }

        Tick(); // update bounding boxes, frustum and distance culling
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
            
            float height_min = FLT_MAX;
            float max_height = -FLT_MAX;
            float min_width  = FLT_MAX;
            float max_width  = -FLT_MAX;

            Matrix transform = HasInstancing() ? GetEntity()->GetMatrix() * GetInstanceTransform(0) : GetEntity()->GetMatrix();
            for (const RHI_Vertex_PosTexNorTan& vertex : vertices)
            {
                Vector3 position = Vector3(vertex.pos[0], vertex.pos[1], vertex.pos[2]);
                height_min       = min(height_min, position.y);
                max_height       = max(max_height, position.y);
                min_width        = min(min_width, position.x);
                max_width        = max(max_width, position.x);
            }

            material->SetProperty(MaterialProperty::WorldWidth,  max_width - min_width);
            material->SetProperty(MaterialProperty::WorldHeight, max_height - height_min);
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

    void Renderable::SetInstances(const vector<Matrix>& transforms)
    {
        if (transforms.empty())
        {
            m_instances.clear();
            m_instance_buffer    = nullptr;
            m_bounding_box_dirty = true;
            return;
        }

        // keep non-transposed matrices for editor/logic (local space)
        m_instances = transforms;

        // transpose the matrices because the vulkan pipeline binding is defined in a row-major way (4 vector4s)
        vector<Matrix> instances_transposed;
        instances_transposed.reserve(transforms.size());

        for (const auto& instance : transforms)
        {
            instances_transposed.push_back(instance.Transposed());
        }

        // create buffer using transposed data
        m_instance_buffer = make_shared<RHI_Buffer>(
            RHI_Buffer_Type::Instance,
            sizeof(Matrix),
            static_cast<uint32_t>(instances_transposed.size()),
            static_cast<void*>(instances_transposed.data()),
            false,
            ("instance_buffer_" + GetObjectName()).c_str()
        );

        m_bounding_box_dirty = true;

        Tick(); // update bounding boxes, frustum and distance culling
    }

    uint32_t Renderable::GetLodCount() const
    {
        if (!m_mesh)
            return 0;

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

    void Renderable::UpdateAabb()
    {
        const Matrix transform = (GetEntity() && GetEntity()->GetActive()) ? GetEntity()->GetMatrix() : Matrix::Identity;

        if (m_bounding_box_dirty || m_transform_previous != transform)
        {
            if (m_instances.empty()) // non-instanced
            {
                m_bounding_box = m_bounding_box_mesh * transform;
            }
            else // instanced
            {
                m_bounding_box = BoundingBox(Vector3::Infinity, Vector3::InfinityNeg);

                for (const Matrix& local_instance : m_instances)
                {
                    Matrix world_instance = local_instance * transform;
                    m_bounding_box.Merge(m_bounding_box_mesh * world_instance);
                }
            }

            m_transform_previous = transform;
            m_bounding_box_dirty = false;
        }
    }

    void Renderable::UpdateFrustumAndDistanceCulling()
    {
        if (Camera* camera = World::GetCamera())
        {
            Vector3 camera_position = camera->GetEntity()->GetPosition();
    
            const BoundingBox& bounding_box = GetBoundingBox();

            // first, check if the bounding box is in the frustum
            if (camera->IsInViewFrustum(bounding_box))
            {
                // only if in frustum, calculate distance
                m_distance_squared = Vector3::DistanceSquared(camera_position, bounding_box.GetClosestPoint(camera_position));
                m_is_visible       = m_distance_squared <= m_max_distance_render * m_max_distance_render;
            }
            else
            {
                // outside frustum, no need for distance check
                m_is_visible = false;
            }
        }
        else
        {
            m_distance_squared = 0.0f;
            m_is_visible       = true;
        }
    }

    void Renderable::UpdateLodIndices()
    {
        // note: using projected angle for LOD selection, which is more perceptually accurate
        // than screen height ratio, for example it will be more consistent across different resolutions

        // thresholds for projected angle (defined in degrees, converted to radians)
        static const array<float, 4> lod_angle_thresholds =
        {
            23.0f * math::deg_to_rad,
            11.5f * math::deg_to_rad,
            5.7f  * math::deg_to_rad,
            2.9f  * math::deg_to_rad
        };

        const uint32_t lod_count = GetLodCount();
        const uint32_t max_lod = lod_count > 0 ? lod_count - 1 : 0;
        Camera* camera = World::GetCamera();

        // if no camera, use lowest detail lod for all
        if (!camera)
        {
            m_lod_index = max_lod;
            return;
        }

        const BoundingBox& box        = GetBoundingBox();
        const Vector3 camera_position = camera->GetEntity()->GetPosition();

        // if not visible, use lowest detail lod
        if (!IsVisible())
        {
            m_lod_index = max_lod;
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
            m_lod_index = 0;
            return;
        }

        // compute projected angle (in radians) using the sphere approximation
        float projected_angle = 2.0f * atan(radius / distance);

        // determine lod index based on projected angle
        uint32_t lod_index = max_lod;
        for (uint32_t i = 0; i < max_lod; i++)
        {
            if (projected_angle > lod_angle_thresholds[i])
            {
                lod_index = i;
                break;
            }
        }

        m_lod_index = lod_index;
    }
}
