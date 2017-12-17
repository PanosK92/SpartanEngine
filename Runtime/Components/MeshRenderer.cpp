/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ============================================
#include "MeshRenderer.h"
#include "Transform.h"
#include "../IO/FileStream.h"
#include "../Logging/Log.h"
#include "../Scene/GameObject.h"
#include "../Graphics/DeferredShaders/ShaderVariation.h"
#include "../Graphics/Mesh.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceManager.h"
//======================================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshRenderer::MeshRenderer()
	{
		Register(ComponentType_MeshRenderer);
		m_castShadows = true;
		m_receiveShadows = true;
		m_materialType = Material_Imported;
	}

	MeshRenderer::~MeshRenderer()
	{

	}

	//= ICOMPONENT ===============================================================
	void MeshRenderer::Initialize()
	{

	}

	void MeshRenderer::Start()
	{

	}

	void MeshRenderer::OnDisable()
	{

	}

	void MeshRenderer::Remove()
	{

	}

	void MeshRenderer::Update()
	{

	}

	void MeshRenderer::Serialize(FileStream* stream)
	{
		stream->Write((int)m_materialType);
		stream->Write(!m_material.expired() ? m_material._Get()->GetResourceFilePath() : (string)NOT_ASSIGNED);
		stream->Write(m_castShadows);
		stream->Write(m_receiveShadows);
	}

	void MeshRenderer::Deserialize(FileStream* stream)
	{
		string materialFilePath = NOT_ASSIGNED;

		m_materialType = (MaterialType)stream->ReadInt();
		stream->Read(&materialFilePath);
		stream->Read(&m_castShadows);
		stream->Read(&m_receiveShadows);

		// The Skybox material and texture is managed by the skybox component.
		// No need to load anything as it will overwrite what the skybox component did.
		if (m_materialType != Material_Skybox)
		{
			m_materialType == Material_Imported ? SetMaterialFromFile(materialFilePath) : SetMaterialByType(m_materialType);
		}
	}
	//==============================================================================

	//= MISC =======================================================================
	void MeshRenderer::Render(unsigned int indexCount)
	{
		// Check if a material exists
		if (m_material.expired()) 
		{
			LOG_WARNING("MeshRenderer: \"" + GetGameObjectName() + "\" has no material. It can't be rendered.");
			return;
		}
		// Check if the material has a shader
		if (!m_material._Get()->HasShader()) 
		{
			LOG_WARNING("MeshRenderer: \"" + GetGameObjectName() + "\" has a material but not a shader associated with it. It can't be rendered.");
			return;
		}

		// Get it's shader and render
		m_material._Get()->GetShader()._Get()->Render(indexCount);
	}

	//==============================================================================

	//= MATERIAL ===================================================================
	// All functions (set/load) resolve to this
	void MeshRenderer::SetMaterialFromMemory(weak_ptr<Material> material)
	{
		if (material.expired())
		{
			LOG_INFO("MeshRenderer: Can't set expired material");
			return;
		}

		auto resourceManager = g_context->GetSubsystem<ResourceManager>();

		// If the material doesn't already existn in the resource cache, save it to a file as well
		auto existingMaterial = resourceManager->GetResourceByName<Material>(material._Get()->GetResourceName());
		if (existingMaterial.expired())
		{
			material._Get()->SaveToFile(material._Get()->GetResourceFilePath());
		}

		m_material = resourceManager->Add<Material>(material.lock());
	}

	weak_ptr<Material> MeshRenderer::SetMaterialFromFile(const string& filePath)
	{
		// Load the material
		shared_ptr<Material> material = make_shared<Material>(g_context);
		material->LoadFromFile(filePath);

		// Set it as the current material
		SetMaterialFromMemory(material);

		// Return it
		return GetMaterial();
	}

	void MeshRenderer::SetMaterialByType(MaterialType type)
	{
		shared_ptr<Material> material;
		string projectStandardAssetDir = g_context->GetSubsystem<ResourceManager>()->GetProjectStandardAssetsDirectory();
		FileSystem::CreateDirectory_(projectStandardAssetDir);

		switch (type)
		{
		case Material_Basic:
			material = make_shared<Material>(g_context);
			material->SetResourceName("Standard");
			material->SetResourceFilePath(projectStandardAssetDir + "Standard_Material" + string(MATERIAL_EXTENSION));
			material->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			material->SetIsEditable(false);
			m_materialType = Material_Basic;
			break;

		case Material_Skybox:
			material = make_shared<Material>(g_context);
			material->SetResourceName("Standard_Skybox");
			material->SetResourceFilePath(projectStandardAssetDir + "Standard_Material_Skybox" + string(MATERIAL_EXTENSION));
			material->SetCullMode(CullFront);
			material->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			material->SetIsEditable(false);
			m_materialType = Material_Skybox;
			break;

		default:
			break;
		}

		SetMaterialFromMemory(material);
	}
	//==============================================================================

	string MeshRenderer::GetGameObjectName()
	{
		return !g_gameObject.expired() ? g_gameObject._Get()->GetName() : NOT_ASSIGNED;
	}
}