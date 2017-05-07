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
#include "../IO/Serializer.h"
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
		m_materialType = Imported;
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
		Serializer::WriteInt((int)m_materialType);
		Serializer::WriteSTR(!m_material.expired() ? m_material.lock()->GetResourceFilePath() : (string)DATA_NOT_ASSIGNED);
		Serializer::WriteBool(m_castShadows);
		Serializer::WriteBool(m_receiveShadows);
	}

	void MeshRenderer::Deserialize()
	{
		m_materialType = (MaterialType)Serializer::ReadInt();
		string filePath = Serializer::ReadSTR();
		m_castShadows = Serializer::ReadBool();
		m_receiveShadows = Serializer::ReadBool();

		// The Skybox material and texture is managed by the skybox component.
		// No need to load anything as it will overwrite what the skybox component did.
		if (m_materialType != Skybox)
			m_materialType == Imported ? LoadMaterial(filePath) : SetMaterial(m_materialType);
	}
	//==============================================================================

	//= MISC =======================================================================
	void MeshRenderer::Render(unsigned int indexCount)
	{
		auto materialWeakPTr = GetMaterial();
		auto materialSharedPtr = materialWeakPTr.lock();

		if (!materialSharedPtr) // Check if a material exists
		{
			LOG_WARNING("GameObject \"" + g_gameObject->GetName() + "\" has no material. It can't be rendered.");
			return;
		}

		if (!materialSharedPtr->HasShader()) // Check if the material has a shader
		{
			LOG_WARNING("GameObject \"" + g_gameObject->GetName() + "\" has a material but not a shader associated with it. It can't be rendered.");
			return;
		}

		// Set the buffers and draw
		materialSharedPtr->GetShader().lock()->Render(indexCount);
	}

	//==============================================================================

	//= MATERIAL ===================================================================
	// All functions (set/load) resolve to this
	void MeshRenderer::SetMaterial(weak_ptr<Material> material)
	{
		if (material.expired())
			return;

		m_material = g_context->GetSubsystem<ResourceManager>()->Add(material.lock());
	}

	void MeshRenderer::SetMaterial(MaterialType type)
	{
		shared_ptr<Material> material;

		switch (type)
		{
		case Basic:
			material = make_shared<Material>(g_context);
			material->SetResourceName("Basic");
			material->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			material->SetIsEditable(false);
			m_materialType = Basic;
			break;

		case Skybox:
			material = make_shared<Material>(g_context);
			material->SetResourceName("Skybox");
			material->SetCullMode(CullFront);
			material->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
			material->SetIsEditable(false);
			m_materialType = Skybox;
			break;

		default:
			break;
		}

		SetMaterial(material);
	}

	weak_ptr<Material> MeshRenderer::SetMaterial(const string& ID)
	{
		auto material = g_context->GetSubsystem<ResourceManager>()->GetResourceByID<Material>(ID);
		SetMaterial(material);
		return GetMaterial();
	}

	weak_ptr<Material> MeshRenderer::LoadMaterial(const string& filePath)
	{
		auto material = g_context->GetSubsystem<ResourceManager>()->Load<Material>(filePath);
		SetMaterial(material);
		return GetMaterial();
	}
	//==============================================================================
}