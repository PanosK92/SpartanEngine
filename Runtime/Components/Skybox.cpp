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

//= INCLUDES =========================
#include "Skybox.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "../Core/GameObject.h"
#include "../Graphics/Texture.h"
#include "../Math/Vector3.h"
#include "../Core/Scene.h"
#include "../Resource/ResourceCache.h"
#include "../Resource/ResourceManager.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Skybox::Skybox()
	{
		m_cubeMapTexture = nullptr;
		m_anchorTrans = nullptr;
	}

	Skybox::~Skybox()
	{

	}

	/*------------------------------------------------------------------------------
									[INTERFACE]
	------------------------------------------------------------------------------*/
	void Skybox::Reset()
	{
		if (g_gameObject.expired())
		{
			return;
		}

		// Get cubemap directory
		auto resourceMng = g_context->GetSubsystem<ResourceManager>();
		string cubamapDirectory = resourceMng->GetResourceDirectory(Cubemap_Resource);

		m_cubeMapTexture = make_shared<Texture>(g_context);
		m_cubeMapTexture->LoadFromFile(cubamapDirectory + "environment.dds");
		m_cubeMapTexture->SetTextureType(CubeMap_Texture);
		m_cubeMapTexture->SetWidth(1024);
		m_cubeMapTexture->SetHeight(1024);
		m_cubeMapTexture->SetGrayscale(false);

		// Add the actual "box"
		g_gameObject.lock()->AddComponent<MeshFilter>()->SetMesh(MeshFilter::Cube);

		// Add a mesh renderer
		auto meshRenderer = g_gameObject.lock()->AddComponent<MeshRenderer>();
		meshRenderer->SetCastShadows(false);
		meshRenderer->SetReceiveShadows(false);
		meshRenderer->SetMaterial(Material_Skybox);
		meshRenderer->GetMaterial().lock()->SetTexture(m_cubeMapTexture);
		g_transform->SetScale(Vector3(1000, 1000, 1000));

		g_gameObject.lock()->SetHierarchyVisibility(false);
	}

	void Skybox::Start()
	{

	}

	void Skybox::OnDisable()
	{

	}

	void Skybox::Remove()
	{

	}

	void Skybox::Update()
	{
		if (m_anchor.expired())
		{
			m_anchor = g_context->GetSubsystem<Scene>()->GetMainCamera();
			m_anchorTrans = m_anchor.lock()->GetTransform();
		}

		if (!m_anchorTrans)
			return;

		g_transform->SetPosition(m_anchorTrans->GetPosition());
	}

	void Skybox::Serialize()
	{

	}

	void Skybox::Deserialize()
	{

	}

	/*------------------------------------------------------------------------------
									[MISC]
	------------------------------------------------------------------------------*/
	void** Skybox::GetEnvironmentTexture()
	{
		return m_cubeMapTexture ? m_cubeMapTexture->GetShaderResource() : nullptr;
	}
}