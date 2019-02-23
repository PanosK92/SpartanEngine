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
	inline void Build(GeometryType type, Renderable* renderable)
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

		model->Geometry_Append(indices, vertices, nullptr, nullptr);
		model->Geometry_Update();

		renderable->Geometry_Set(
			"Default_Geometry",
			0,
			(unsigned int)indices.size(),
			0,
			(unsigned int)vertices.size(),
			BoundingBox(vertices),
			model
		);
	}

	Renderable::Renderable(Context* context, Entity* entity, Transform* transform) : IComponent(context, entity, transform)
	{
		m_geometryType			= Geometry_Custom;	
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
		REGISTER_ATTRIBUTE_GET_SET(Geometry_Type, Geometry_Set, GeometryType);
	}

	Renderable::~Renderable()
	{

	}

	//= ICOMPONENT ===============================================================
	void Renderable::Serialize(FileStream* stream)
	{
		// Mesh
		stream->Write((int)m_geometryType);
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
		m_geometryType			= (GeometryType)stream->ReadInt();
		m_geometryIndexOffset	= stream->ReadUInt();
		m_geometryIndexCount	= stream->ReadUInt();	
		m_geometryVertexOffset	= stream->ReadUInt();
		m_geometryVertexCount	= stream->ReadUInt();
		stream->Read(&m_geometryAABB);
		string modelName;
		stream->Read(&modelName);
		m_model = m_context->GetSubsystem<ResourceCache>()->GetByName<Model>(modelName);

		// If it was a default mesh, we have to reconstruct it
		if (m_geometryType != Geometry_Custom) 
		{
			Geometry_Set(m_geometryType);
		}

		// Material
		stream->Read(&m_castShadows);
		stream->Read(&m_receiveShadows);
		stream->Read(&m_materialDefault);
		if (m_materialDefault)
		{
			Material_UseDefault();		
		}
		else
		{
			string materialName;
			stream->Read(&materialName);
			m_material = m_context->GetSubsystem<ResourceCache>()->GetByName<Material>(materialName);
		}
	}
	//==============================================================================

	//= GEOMETRY =====================================================================================
	void Renderable::Geometry_Set(const string& name, unsigned int indexOffset, unsigned int indexCount, unsigned int vertexOffset, unsigned int vertexCount, const BoundingBox& AABB, shared_ptr<Model>& model)
	{	
		m_geometryName			= name;
		m_geometryIndexOffset	= indexOffset;
		m_geometryIndexCount	= indexCount;
		m_geometryVertexOffset	= vertexOffset;
		m_geometryVertexCount	= vertexCount;
		m_geometryAABB			= AABB;
		m_model					= model;
	}

	void Renderable::Geometry_Set(GeometryType type)
	{
		m_geometryType = type;

		if (type != Geometry_Custom)
		{
			Build(type, this);
		}
	}

	void Renderable::Geometry_Get(vector<unsigned int>* indices, vector<RHI_Vertex_PosUvNorTan>* vertices)
	{
		if (!m_model)
		{
			LOG_ERROR("Invalid model");
			return;
		}

		m_model->Geometry_Get(m_geometryIndexOffset, m_geometryIndexCount, m_geometryVertexOffset, m_geometryVertexCount, indices, vertices);
	}

	BoundingBox Renderable::Geometry_AABB()
	{
		return m_geometryAABB.Transformed(GetTransform()->GetMatrix());
	}
	//==============================================================================

	//= MATERIAL ===================================================================
	// All functions (set/load) resolve to this
	void Renderable::Material_Set(const shared_ptr<Material>& material)
	{
		if (!material)
		{
			LOG_ERROR_INVALID_PARAMETER();
			return;
		}
		m_material = material;
	}

	shared_ptr<Material> Renderable::Material_Set(const string& filePath)
	{
		// Load the material
		auto material = make_shared<Material>(GetContext());
		if (!material->LoadFromFile(filePath))
		{
			LOGF_WARNING("Failed to load material from \"%s\"", filePath.c_str());
			return nullptr;
		}

		// Set it as the current material
		Material_Set(material);

		// Return it
		return material;
	}

	void Renderable::Material_UseDefault()
	{
		m_materialDefault = true;

		auto projectStandardAssetDir = GetContext()->GetSubsystem<ResourceCache>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);
		auto materialStandard = make_shared<Material>(GetContext());
		materialStandard->SetResourceName("Standard");
		materialStandard->SetCullMode(Cull_Back);
		materialStandard->SetColorAlbedo(Vector4(0.6f, 0.6f, 0.6f, 1.0f));
		materialStandard->SetIsEditable(false);		
		Material_Set(materialStandard);
	}

	const string& Renderable::Material_Name()
	{
		return m_material ? m_material->GetResourceName() : NOT_ASSIGNED;
	}
	//==============================================================================
}
