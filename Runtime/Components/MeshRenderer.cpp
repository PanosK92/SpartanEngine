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

//= INCLUDES ===================================
#include "MeshRenderer.h"
#include "Transform.h"
#include "../IO/StreamIO.h"
#include "../Logging/Log.h"
#include "../Core/GameObject.h"
#include "../Graphics/Shaders/ShaderVariation.h"
#include "../Graphics/Mesh.h"
#include "../FileSystem/FileSystem.h"
#include "../Resource/ResourceManager.h"
//==============================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	MeshRenderer::MeshRenderer()
	{
		m_castShadows = true;
		m_receiveShadows = true;
		m_materialType = Material_Imported;
	}

	MeshRenderer::~MeshRenderer()
	{

	}

	//= ICOMPONENT ===============================================================
	void MeshRenderer::Reset()
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

	void MeshRenderer::Serialize()
	{
		StreamIO::WriteInt((int)m_materialType);
		StreamIO::WriteSTR(!m_material.expired() ? m_material._Get()->GetResourceFilePath() : (string)NOT_ASSIGNED);
		StreamIO::WriteBool(m_castShadows);
		StreamIO::WriteBool(m_receiveShadows);
	}

	void MeshRenderer::Deserialize()
	{
		m_materialType = (MaterialType)StreamIO::ReadInt();
		string materialFilePath = StreamIO::ReadSTR();
		m_castShadows = StreamIO::ReadBool();
		m_receiveShadows = StreamIO::ReadBool();

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
			LOG_WARNING("GameObject \"" + GetGameObjectName() + "\" has no material. It can't be rendered.");
			return;
		}

		if (!m_material._Get()->HasShader()) // Check if the material has a shader
		{
			LOG_WARNING("GameObject \"" + GetGameObjectName() + "\" has a material but not a shader associated with it. It can't be rendered.");
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
			LOG_INFO("Can't set expired material");
			return;
		}

		m_material = g_context->GetSubsystem<ResourceManager>()->Add(material.lock());
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

		switch (type)
		{
		case Material_Basic:
			material = make_shared<Material>(g_context);
			material->SetResourceName("Basic");
			material->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			material->SetIsEditable(false);
			m_materialType = Material_Basic;
			break;

		case Material_Skybox:
			material = make_shared<Material>(g_context);
			material->SetResourceName("Skybox");
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

	weak_ptr<Material> MeshRenderer::SetMaterialByID(unsigned int ID)
	{
		// Get the material from the resource cache
		weak_ptr<Material> material = g_context->GetSubsystem<ResourceManager>()->GetResourceByID<Material>(ID);	
		if (material.expired())
		{
			LOG_WARNING("Failed to set material. Material with ID \"" + to_string(ID) + "\" doesn't exist");
			return weak_ptr<Material>();
		}

		// Set it as the current material
		SetMaterialFromMemory(material);

		// Return it
		return GetMaterial();
	}
	//==============================================================================

	string MeshRenderer::GetGameObjectName()
	{
		return !g_gameObject.expired() ? g_gameObject._Get()->GetName() : NOT_ASSIGNED;
	}
}