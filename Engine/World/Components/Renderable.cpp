/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ==================================
#include "Renderable.h"
#include "Transform.h"
#include "../../IO/FileStream.h"
#include "../../Resource/ResourceCache.h"
#include "../../Rendering/Utilities/Geometry.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/Model.h"
//=============================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	inline void build(const Geometry_Type type, Renderable* renderable)
	{	
		auto model = make_shared<Model>(renderable->GetContext());
		vector<RHI_Vertex_PosUvNorTan> vertices;
		vector<unsigned int> indices;

		// Construct geometry
		if (type == Geometry_Default_Cube)
		{
			Utility::Geometry::CreateCube(&vertices, &indices);		
			model->SetResourceName("Default_Cube");
		}
		else if (type == Geometry_Default_Quad)
		{
			Utility::Geometry::CreateQuad(&vertices, &indices);
			model->SetResourceName("Default_Cube");
		}
		else if (type == Geometry_Default_Sphere)
		{
			Utility::Geometry::CreateSphere(&vertices, &indices);
			model->SetResourceName("Default_Cube");
		}
		else if (type == Geometry_Default_Cylinder)
		{
			Utility::Geometry::CreateCylinder(&vertices, &indices);
			model->SetResourceName("Default_Cube");
		}
		else if (type == Geometry_Default_Cone)
		{
			Utility::Geometry::CreateCone(&vertices, &indices);
			model->SetResourceName("Default_Cube");
		}

		if (vertices.empty() || indices.empty())
			return;

		model->GeometryAppend(indices, vertices, nullptr, nullptr);
		model->GeometryUpdate();

		renderable->GeometrySet(
			"Default_Geometry",
			0,
			static_cast<unsigned int>(indices.size()),
			0,
			static_cast<unsigned int>(vertices.size()),
			BoundingBox(vertices),
			model
		);
	}

	Renderable::Renderable(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_geometry_type			= Geometry_Custom;	
		m_geometryIndexOffset	= 0;
		m_geometryIndexCount	= 0;
		m_geometryVertexOffset	= 0;
		m_geometryVertexCount	= 0;
		m_materialDefault		= false;
		m_castShadows			= true;
		m_receiveShadows		= true;

		REGISTER_ATTRIBUTE_VALUE_VALUE(m_materialDefault, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_material, shared_ptr<Material>);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_castShadows, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_receiveShadows, bool);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryIndexOffset, unsigned int);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryIndexCount, unsigned int);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryVertexOffset, unsigned int);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryVertexCount, unsigned int);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryName, string);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_model, shared_ptr<Model>);
		REGISTER_ATTRIBUTE_VALUE_VALUE(m_geometryAABB, BoundingBox);
		REGISTER_ATTRIBUTE_GET_SET(Geometry_Type, GeometrySet, Geometry_Type);
	}

	//= ICOMPONENT ===============================================================
	void Renderable::Serialize(FileStream* stream)
	{
		// Mesh
		stream->Write(static_cast<unsigned int>(m_geometry_type));
		stream->Write(m_geometryIndexOffset);
		stream->Write(m_geometryIndexCount);
		stream->Write(m_geometryVertexOffset);
		stream->Write(m_geometryVertexCount);
		stream->Write(m_geometryAABB);
		stream->Write(m_model ? m_model->GetResourceName() : NOT_ASSIGNED);

		// Material
		stream->Write(m_castShadows);
		stream->Write(m_receiveShadows);
		stream->Write(m_materialDefault);
		if (!m_materialDefault)
		{
			stream->Write(m_material ? m_material->GetResourceName() : NOT_ASSIGNED);
		}
	}

	void Renderable::Deserialize(FileStream* stream)
	{
		// Geometry
		m_geometry_type			= static_cast<Geometry_Type>(stream->ReadUInt());
		m_geometryIndexOffset	= stream->ReadUInt();
		m_geometryIndexCount	= stream->ReadUInt();	
		m_geometryVertexOffset	= stream->ReadUInt();
		m_geometryVertexCount	= stream->ReadUInt();
		stream->Read(&m_geometryAABB);
		string model_name;
		stream->Read(&model_name);
		m_model = m_context->GetSubsystem<ResourceCache>()->GetByName<Model>(model_name);

		// If it was a default mesh, we have to reconstruct it
		if (m_geometry_type != Geometry_Custom) 
		{
			GeometrySet(m_geometry_type);
		}

		// Material
		stream->Read(&m_castShadows);
		stream->Read(&m_receiveShadows);
		stream->Read(&m_materialDefault);
		if (m_materialDefault)
		{
			MaterialUseDefault();		
		}
		else
		{
			string material_name;
			stream->Read(&material_name);
			m_material = m_context->GetSubsystem<ResourceCache>()->GetByName<Material>(material_name);
		}
	}
	//==============================================================================

	//= GEOMETRY =====================================================================================
	void Renderable::GeometrySet(const string& name, const unsigned int index_offset, const unsigned int index_count, const unsigned int vertex_offset, const unsigned int vertex_count, const BoundingBox& aabb, shared_ptr<Model>& model)
	{	
		m_geometryName			= name;
		m_geometryIndexOffset	= index_offset;
		m_geometryIndexCount	= index_count;
		m_geometryVertexOffset	= vertex_offset;
		m_geometryVertexCount	= vertex_count;
		m_geometryAABB			= aabb;
		m_model					= model;
	}

	void Renderable::GeometrySet(const Geometry_Type type)
	{
		m_geometry_type = type;

		if (type != Geometry_Custom)
		{
			build(type, this);
		}
	}

	void Renderable::GeometryGet(vector<unsigned int>* indices, vector<RHI_Vertex_PosUvNorTan>* vertices) const
	{
		if (!m_model)
		{
			LOG_ERROR("Invalid model");
			return;
		}

		m_model->GeometryGet(m_geometryIndexOffset, m_geometryIndexCount, m_geometryVertexOffset, m_geometryVertexCount, indices, vertices);
	}

	BoundingBox Renderable::GeometryAabb()
	{
		return m_geometryAABB.Transformed(GetTransform()->GetMatrix());
	}
	//==============================================================================

	//= MATERIAL ===================================================================
	// All functions (set/load) resolve to this
	void Renderable::MaterialSet(const shared_ptr<Material>& material)
	{
		if (!material)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}
		m_material = material;
	}

	shared_ptr<Material> Renderable::MaterialSet(const string& file_path)
	{
		// Load the material
		auto material = make_shared<Material>(GetContext());
		if (!material->LoadFromFile(file_path))
		{
			LOGF_WARNING("Failed to load material from \"%s\"", file_path.c_str());
			return nullptr;
		}

		// Set it as the current material
		MaterialSet(material);

		// Return it
		return material;
	}

	void Renderable::MaterialUseDefault()
	{
		m_materialDefault = true;

		auto projectStandardAssetDir = GetContext()->GetSubsystem<ResourceCache>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);
		auto materialStandard = make_shared<Material>(GetContext());
		materialStandard->SetResourceName("Standard");
		materialStandard->SetCullMode(Cull_Back);
		materialStandard->SetColorAlbedo(Vector4(0.6f, 0.6f, 0.6f, 1.0f));
		materialStandard->SetIsEditable(false);		
		MaterialSet(materialStandard);
	}

	const string& Renderable::MaterialName()
	{
		return m_material ? m_material->GetResourceName() : NOT_ASSIGNED;
	}
	//==============================================================================
}
