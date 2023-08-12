/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "../Rendering/Renderer.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
//=======================================

//= NAMESPACES ===============
using namespace std;
using namespace Spartan::Math;
//============================

namespace Spartan
{
    Renderable::Renderable(weak_ptr<Entity> entity) : Component(entity)
    {
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material_default,       bool);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_material,               Material*);
        SP_REGISTER_ATTRIBUTE_VALUE_VALUE(m_cast_shadows,           bool);
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
        stream->Write(m_mesh ? m_mesh->GetObjectName() : "");

        // material
        stream->Write(m_cast_shadows);
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
        string model_name;
        stream->Read(&model_name);
        m_mesh = ResourceCache::GetByName<Mesh>(model_name).get();

        // material
        stream->Read(&m_cast_shadows);
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
        m_mesh                   = mesh;
        m_bounding_box           = aabb;
        m_geometry_index_offset  = index_offset;
        m_geometry_index_count   = index_count;
        m_geometry_vertex_offset = vertex_offset;
        m_geometry_vertex_count  = vertex_count;

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

        SP_ASSERT(m_geometry_index_count  != 0);
        SP_ASSERT(m_geometry_vertex_count != 0);
        SP_ASSERT(m_bounding_box          != BoundingBox::Undefined);
    }

    void Renderable::GetGeometry(vector<uint32_t>* indices, vector<RHI_Vertex_PosTexNorTan>* vertices) const
    {
        SP_ASSERT_MSG(m_mesh != nullptr, "Invalid mesh");
        m_mesh->GetGeometry(m_geometry_index_offset, m_geometry_index_count, m_geometry_vertex_offset, m_geometry_vertex_count, indices, vertices);
    }

    const BoundingBox& Renderable::GetAabb()
    {
        // update if dirty
        if (m_last_transform != GetTransform()->GetMatrix() || m_bounding_box_transformed == BoundingBox::Undefined)
        {
            m_bounding_box_transformed = m_bounding_box.Transform(GetTransform()->GetMatrix());
            m_last_transform = GetTransform()->GetMatrix();
        }

        return m_bounding_box_transformed;
    }

    // all functions (set/load) resolve to this
    shared_ptr<Material> Renderable::SetMaterial(const shared_ptr<Material>& material)
    {
        SP_ASSERT(material != nullptr);

        // in order for the component to guarantee serialization/deserialization, we cache the material
        shared_ptr<Material> _material = ResourceCache::Cache(material);

        m_material = _material.get();

        // set to false otherwise material won't serialize/deserialize
        m_material_default = false;

        return _material;
    }

    shared_ptr<Material> Renderable::SetMaterial(const string& file_path)
    {
        // load the material
        auto material = make_shared<Material>();
        if (!material->LoadFromFile(file_path))
        {
            SP_LOG_WARNING("Failed to load material from \"%s\"", file_path.c_str());
            return nullptr;
        }

        // set it as the current material
        return SetMaterial(material);
    }

    void Renderable::SetDefaultMaterial()
    {
        m_material_default = true;
        const string data_dir = ResourceCache::GetDataDirectory() + "\\";
        FileSystem::CreateDirectory(data_dir);

        // create material
        shared_ptr<Material> material = make_shared<Material>();
        material->SetResourceFilePath(ResourceCache::GetProjectDirectory() + "standard" + EXTENSION_MATERIAL); // Set resource file path so it can be used by the resource cache
        material->SetProperty(MaterialProperty::CanBeEdited, 0.0f);
        material->SetProperty(MaterialProperty::UvTilingX, 10.0f);
        material->SetProperty(MaterialProperty::UvTilingY, 10.0f);
        material->SetProperty(MaterialProperty::ColorR, 1.0f);
        material->SetProperty(MaterialProperty::ColorG, 1.0f);
        material->SetProperty(MaterialProperty::ColorB, 1.0f);
        material->SetProperty(MaterialProperty::ColorA, 1.0f);

        // set default texture
        material->SetTexture(MaterialTexture::Color, Renderer::GetStandardTexture(Renderer_StandardTexture::Checkerboard));

        // set material
        SetMaterial(material);
        m_material_default = true;
    }

    string Renderable::GetMaterialName() const
    {
        return m_material ? m_material->GetObjectName() : "";
    }
}
