/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include "Runtime/Core/Spartan.h"
#include "Renderable.h"
#include "Transform.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Utilities/Geometry.h"
#include "../../RHI/RHI_Texture2D.h"
#include "../../Rendering/Model.h"
#include "../../RHI/RHI_Vertex.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    inline void build(const Geometry_Type type, Renderable* renderable)
    {    
        Model* model = new Model(renderable->GetContext());
        vector<RHI_Vertex_PosTexNorTan> vertices;
        vector<uint32_t> indices;

        const string project_directory = renderable->GetContext()->GetSubsystem<ResourceCache>()->GetProjectDirectory();

        // Construct geometry
        if (type == Geometry_Default_Cube)
        {
            Utility::Geometry::CreateCube(&vertices, &indices);
            model->SetResourceFilePath(project_directory + "default_cube" + EXTENSION_MODEL);
        }
        else if (type == Geometry_Default_Quad)
        {
            Utility::Geometry::CreateQuad(&vertices, &indices);
            model->SetResourceFilePath(project_directory + "default_quad" + EXTENSION_MODEL);
        }
        else if (type == Geometry_Default_Sphere)
        {
            Utility::Geometry::CreateSphere(&vertices, &indices);
            model->SetResourceFilePath(project_directory + "default_sphere" + EXTENSION_MODEL);
        }
        else if (type == Geometry_Default_Cylinder)
        {
            Utility::Geometry::CreateCylinder(&vertices, &indices);
            model->SetResourceFilePath(project_directory + "default_cylinder" + EXTENSION_MODEL);
        }
        else if (type == Geometry_Default_Cone)
        {
            Utility::Geometry::CreateCone(&vertices, &indices);
            model->SetResourceFilePath(project_directory + "default_cone" + EXTENSION_MODEL);
        }

        if (vertices.empty() || indices.empty())
            return;

        model->AppendGeometry(indices, vertices, nullptr, nullptr);
        model->UpdateGeometry();

        renderable->GeometrySet(
            "Default_Geometry",
            0,
            static_cast<uint32_t>(indices.size()),
            0,
            static_cast<uint32_t>(vertices.size()),
            BoundingBox(vertices.data(), static_cast<uint32_t>(vertices.size())),
            model
        );
    }

    Renderable::Renderable(Context* context, Entity* entity, uint64_t id /*= 0*/) : IComponent(context, entity, id)
    {
        m_geometry_type        = Geometry_Custom;
        m_geometryIndexOffset  = 0;
        m_geometryIndexCount   = 0;
        m_geometryVertexOffset = 0;
        m_geometryVertexCount  = 0;
        m_material_default     = false;
        m_cast_shadows         = true;

        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material_default,      bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material,              Material*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_cast_shadows,          bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryIndexOffset,   uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryIndexCount,    uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryVertexOffset,  uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryVertexCount,   uint32_t);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryName,          string);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_model,                 Model*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_bounding_box,          BoundingBox);
        SP_REGISTER_ATTRIBUTE_GET_SET(Geometry_Type, GeometrySet,  Geometry_Type);
    }

    void Renderable::Serialize(FileStream* stream)
    {
        // Mesh
        stream->Write(static_cast<uint32_t>(m_geometry_type));
        stream->Write(m_geometryIndexOffset);
        stream->Write(m_geometryIndexCount);
        stream->Write(m_geometryVertexOffset);
        stream->Write(m_geometryVertexCount);
        stream->Write(m_bounding_box);
        stream->Write(m_model ? m_model->GetResourceName() : "");

        // Material
        stream->Write(m_cast_shadows);
        stream->Write(m_material_default);
        if (!m_material_default)
        {
            stream->Write(m_material ? m_material->GetResourceName() : "");
        }
    }

    void Renderable::Deserialize(FileStream* stream)
    {
        // Geometry
        m_geometry_type         = static_cast<Geometry_Type>(stream->ReadAs<uint32_t>());
        m_geometryIndexOffset   = stream->ReadAs<uint32_t>();
        m_geometryIndexCount    = stream->ReadAs<uint32_t>();
        m_geometryVertexOffset  = stream->ReadAs<uint32_t>();
        m_geometryVertexCount   = stream->ReadAs<uint32_t>();
        stream->Read(&m_bounding_box);
        string model_name;
        stream->Read(&model_name);
        m_model = m_context->GetSubsystem<ResourceCache>()->GetByName<Model>(model_name).get();

        // If it was a default mesh, we have to reconstruct it
        if (m_geometry_type != Geometry_Custom) 
        {
            GeometrySet(m_geometry_type);
        }

        // Material
        stream->Read(&m_cast_shadows);
        stream->Read(&m_material_default);
        if (m_material_default)
        {
            UseDefaultMaterial();
        }
        else
        {
            string material_name;
            stream->Read(&material_name);
            m_material = m_context->GetSubsystem<ResourceCache>()->GetByName<Material>(material_name).get();
        }
    }

    void Renderable::GeometrySet(const string& name, const uint32_t index_offset, const uint32_t index_count, const uint32_t vertex_offset, const uint32_t vertex_count, const BoundingBox& bounding_box, Model* model)
    {
        // Terrible way to delete previous geometry in case it's a default one
        if (m_geometryName == "Default_Geometry")
        {
            delete m_model;
        }

        m_geometryName         = name;
        m_geometryIndexOffset  = index_offset;
        m_geometryIndexCount   = index_count;
        m_geometryVertexOffset = vertex_offset;
        m_geometryVertexCount  = vertex_count;
        m_bounding_box         = bounding_box;
        m_model                = model;
    }

    void Renderable::GeometrySet(const Geometry_Type type)
    {
        m_geometry_type = type;

        if (type != Geometry_Custom)
        {
            build(type, this);
        }
    }

    void Renderable::GeometryClear()
    {
        GeometrySet("Cleared", 0, 0, 0, 0, BoundingBox(), nullptr);
    }

    void Renderable::GeometryGet(vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices) const
    {
        if (!m_model)
        {
            LOG_ERROR("Invalid model");
            return;
        }

        m_model->GetGeometry(m_geometryIndexOffset, m_geometryIndexCount, m_geometryVertexOffset, m_geometryVertexCount, indices, vertices);
    }

    const BoundingBox& Renderable::GetAabb()
    {
        // Updated if dirty
        if (m_last_transform != GetTransform()->GetMatrix() || !m_aabb.Defined())
        {
            m_aabb = m_bounding_box.Transform(GetTransform()->GetMatrix());
            m_last_transform = GetTransform()->GetMatrix();
        }

        return m_aabb;
    }

    // All functions (set/load) resolve to this
    shared_ptr<Material> Renderable::SetMaterial(const shared_ptr<Material>& material)
    {
        SP_ASSERT(material != nullptr);

        // In order for the component to guarantee serialization/deserialization, we cache the material
        shared_ptr<Material> _material = m_context->GetSubsystem<ResourceCache>()->Cache(material);

        m_material = _material.get();

        // Set to false otherwise material won't serialize/deserialize
        m_material_default = false;

        return _material;
    }

    shared_ptr<Material> Renderable::SetMaterial(const string& file_path)
    {
        // Load the material
        auto material = make_shared<Material>(GetContext());
        if (!material->LoadFromFile(file_path))
        {
            LOG_WARNING("Failed to load material from \"%s\"", file_path.c_str());
            return nullptr;
        }

        // Set it as the current material
        return SetMaterial(material);
    }

    void Renderable::UseDefaultMaterial()
    {
        m_material_default = true;
        ResourceCache* resource_cache = GetContext()->GetSubsystem<ResourceCache>();
        const auto data_dir = resource_cache->GetResourceDirectory() + "/";
        FileSystem::CreateDirectory_(data_dir);

        // Create material
        auto material = make_shared<Material>(GetContext());
        material->SetResourceFilePath(resource_cache->GetProjectDirectory() + "standard" + EXTENSION_MATERIAL); // Set resource file path so it can be used by the resource cache
        material->SetIsEditable(false);

        // Se default texture
        const shared_ptr<RHI_Texture2D> texture = resource_cache->Load<RHI_Texture2D>(resource_cache->GetResourceDirectory(ResourceDirectory::Textures) + "/no_texture.png");
        material->SetTextureSlot(Material_Color, texture);

        // Set material
        SetMaterial(material);
        m_material_default = true;
    }

    string Renderable::GetMaterialName() const
    {
        return m_material ? m_material->GetResourceName() : "";
    }
}
