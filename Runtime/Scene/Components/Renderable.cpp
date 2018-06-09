/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ==============================================
#include "Renderable.h"
#include "Transform.h"
#include "../../Rendering/Material.h"
#include "../../Rendering/DeferredShaders/ShaderVariation.h"
#include "../../Rendering/GeometryUtility.h"
#include "../../Rendering/Mesh.h"
#include "../../Logging/Log.h"
#include "../../IO/FileStream.h"
#include "../../FileSystem/FileSystem.h"
#include "../../Resource/ResourceManager.h"
//=========================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Renderable::Renderable(Context* context, GameObject* gameObject, Transform* transform) : IComponent(context, gameObject, transform)
	{
		// Mesh
		m_meshType				= MeshType_Imported;
		m_meshRef				= nullptr;
		// Material
		m_usingStandardMaterial = false;
		m_materialRef			= nullptr;
		// Properties
		m_castShadows			= true;
		m_receiveShadows		= true;
	}

	Renderable::~Renderable()
	{

	}

	//= ICOMPONENT ===============================================================
	void Renderable::Serialize(FileStream* stream)
	{
		// Mesh
		stream->Write((int)m_meshType);
		stream->Write(!m_meshRefWeak.expired() ? m_meshRefWeak.lock()->GetResourceName() : (string)NOT_ASSIGNED);

		// Material
		stream->Write(m_castShadows);
		stream->Write(m_receiveShadows);
		stream->Write(m_usingStandardMaterial);
		if (!m_usingStandardMaterial)
		{
			stream->Write(!m_materialRefWeak.expired() ? m_materialRefWeak.lock()->GetResourceName() : NOT_ASSIGNED);
		}
	}

	void Renderable::Deserialize(FileStream* stream)
	{
		// Mesh
		m_meshType		= (MeshType)stream->ReadInt();
		string meshName	= NOT_ASSIGNED;
		stream->Read(&meshName);

		if (m_meshType == MeshType_Imported) // If it was an imported mesh, get it from the resource cache
		{
			m_meshRefWeak	= GetContext()->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName);
			m_meshRef		= m_meshRefWeak.lock().get();
			if (m_meshRefWeak.expired())
			{
				LOG_WARNING("Renderable: Failed to load mesh \"" + meshName + "\".");
			}
		}
		else // If it was a standard mesh, reconstruct it
		{
			UseStandardMesh(m_meshType);
		}

		// Material
		stream->Read(&m_castShadows);
		stream->Read(&m_receiveShadows);
		stream->Read(&m_usingStandardMaterial);
		if (m_usingStandardMaterial)
		{
			UseStandardMaterial();		
		}
		else
		{
			string materialName;
			stream->Read(&materialName);
			m_materialRefWeak	= m_context->GetSubsystem<ResourceManager>()->GetResourceByName<Material>(materialName);
			m_materialRef		= m_materialRefWeak.lock().get();
		}
	}
	//==============================================================================

	void Renderable::Render(unsigned int indexCount)
	{
		// Check if a material exists
		if (m_materialRefWeak.expired()) 
		{
			LOG_WARNING("Renderable: \"" + GetGameObjectName() + "\" has no material. It can't be rendered.");
			return;
		}
		// Check if the material has a shader
		if (!m_materialRefWeak.lock()->HasShader()) 
		{
			LOG_WARNING("Renderable: \"" + GetGameObjectName() + "\" has a material but not a shader associated with it. It can't be rendered.");
			return;
		}

		// Get it's shader and render
		m_materialRefWeak.lock()->GetShader().lock()->Render(indexCount);
	}

	//= MESH =====================================================================================
	void Renderable::SetMesh(const weak_ptr<Mesh>& mesh, bool autoCache /* true */)
	{
		m_meshRefWeak	= mesh;
		m_meshRef		= m_meshRefWeak.lock().get();

		// We do allow for a Renderable with no mesh
		if (m_meshRefWeak.expired())
			return;

		m_meshRefWeak	= autoCache ? mesh.lock()->Cache<Mesh>() : mesh;
		m_meshRef		= m_meshRefWeak.lock().get();
	}

	// Sets a default mesh (cube, quad)
	void Renderable::UseStandardMesh(MeshType type)
	{
		m_meshType = type;

		// Create a name for this standard mesh
		string meshName;
		if (type == MeshType_Cube)
		{
			meshName = "Standard_Cube";
		}
		else if (type == MeshType_Quad)
		{
			meshName = "Standard_Quad";
		}
		else if (type == MeshType_Sphere)
		{
			meshName = "Standard_Sphere";
		}
		else if (type == MeshType_Cylinder)
		{
			meshName = "Standard_Cylinder";
		}
		else if (type == MeshType_Cone)
		{
			meshName = "Standard_Cone";
		}

		// Check if this mesh is already loaded, if so, use the existing one
		if (auto existingMesh = GetContext()->GetSubsystem<ResourceManager>()->GetResourceByName<Mesh>(meshName).lock())
		{
			// Cache it and keep a reference
			SetMesh(existingMesh->Cache<Mesh>(), false);
			return;
		}

		// Construct vertices/indices
		vector<VertexPosTexTBN> vertices;
		vector<unsigned int> indices;
		if (type == MeshType_Cube)
		{
			GeometryUtility::CreateCube(&vertices, &indices);
		}
		else if (type == MeshType_Quad)
		{
			GeometryUtility::CreateQuad(&vertices, &indices);
		}
		else if (type == MeshType_Sphere)
		{
			GeometryUtility::CreateSphere(&vertices, &indices);
		}
		else if (type == MeshType_Cylinder)
		{
			GeometryUtility::CreateCylinder(&vertices, &indices);
		}
		else if (type == MeshType_Cone)
		{
			GeometryUtility::CreateCone(&vertices, &indices);
		}

		// Create a file path (in the project directory) for this standard mesh
		string projectStandardAssetDir = GetContext()->GetSubsystem<ResourceManager>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);

		// Create a mesh
		auto mesh = make_shared<Mesh>(GetContext());
		mesh->SetVertices(vertices);
		mesh->SetIndices(indices);
		mesh->SetResourceName(meshName);
		mesh->Construct();

		// Cache it and keep a reference
		SetMesh(mesh->Cache<Mesh>(), false);
	}

	bool Renderable::SetBuffers()
	{
		if (m_meshRefWeak.expired())
			return false;

		m_meshRefWeak.lock()->SetBuffers();
		return true;
	}

	string Renderable::GetMeshName()
	{
		return !m_meshRefWeak.expired() ? m_meshRefWeak.lock()->GetResourceName() : NOT_ASSIGNED;
	}
	//==============================================================================

	//= MATERIAL ===================================================================
	// All functions (set/load) resolve to this
	void Renderable::SetMaterialFromMemory(const weak_ptr<Material>& materialWeak, bool autoCache /* true */)
	{
		// Validate material
		auto material = materialWeak.lock();
		if (!material)
		{
			LOG_WARNING("Renderable::SetMaterialFromMemory(): Provided material is null, can't execute function");
			return;
		}

		if (autoCache) // Cache it
		{
			if (auto cachedMat = material->Cache<Material>().lock())
			{
				m_materialRefWeak = cachedMat;
				m_materialRef = m_materialRefWeak.lock().get();
				if (cachedMat->HasFilePath())
				{
					m_materialRef->SaveToFile(material->GetResourceFilePath());
					m_usingStandardMaterial = false;
				}
			}
		}
		else
		{
			m_materialRefWeak = material;
			m_materialRef = m_materialRefWeak.lock().get();
		}
	}

	weak_ptr<Material> Renderable::SetMaterialFromFile(const string& filePath)
	{
		// Load the material
		auto material = make_shared<Material>(GetContext());
		if (!material->LoadFromFile(filePath))
		{
			LOG_WARNING("Renderable::SetMaterialFromFile(): Failed to load material from \"" + filePath + "\"");
			return weak_ptr<Material>();
		}

		// Set it as the current material
		SetMaterialFromMemory(material);

		// Return it
		return GetMaterial_RefWeak();
	}

	void Renderable::UseStandardMaterial()
	{
		m_usingStandardMaterial = true;

		auto projectStandardAssetDir = GetContext()->GetSubsystem<ResourceManager>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);
		auto materialStandard = make_shared<Material>(GetContext());
		materialStandard->SetResourceName("Standard");
		materialStandard->SetCullMode(CullBack);
		materialStandard->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		materialStandard->SetIsEditable(false);		
		SetMaterialFromMemory(materialStandard->Cache<Material>(), false);
	}

	string Renderable::GetMaterialName()
	{
		return !GetMaterial_RefWeak().expired() ? GetMaterial_RefWeak().lock()->GetResourceName() : NOT_ASSIGNED;
	}
	//==============================================================================

	//= BOUNDING BOX ===================================================================================================
	const BoundingBox& Renderable::GetBoundingBox() const
	{
		return !m_meshRefWeak.expired() ? m_meshRefWeak.lock()->GetBoundingBox() : BoundingBox::Zero;
	}

	BoundingBox Renderable::GetBoundingBoxTransformed()
	{
		BoundingBox boundingBox = !m_meshRefWeak.expired() ? m_meshRefWeak.lock()->GetBoundingBox() : BoundingBox::Zero;
		return boundingBox.Transformed(GetTransform()->GetWorldTransform());
	}
	//==================================================================================================================
}
