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

//= INCLUDES ==============================
#include "Skybox.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "../GameObject.h"
#include "../../Graphics/Texture.h"
#include "../../Math/Vector3.h"
#include "../../Scene/Scene.h"
#include "../../Resource/ResourceCache.h"
#include "../../Resource/ResourceManager.h"
//=========================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

namespace Directus
{
	Skybox::Skybox()
	{

	}

	Skybox::~Skybox()
	{

	}

	/*------------------------------------------------------------------------------
									[INTERFACE]
	------------------------------------------------------------------------------*/
	void Skybox::Initialize()
	{
		auto resourceMng = GetContext()->GetSubsystem<ResourceManager>();

		// Load cubemap texture
		string cubemapDirectory = resourceMng->GetStandardResourceDirectory(Resource_Cubemap);
		string texPath = cubemapDirectory + "environment.dds";
		m_cubemapTexture = make_shared<Texture>(GetContext());
		m_cubemapTexture->SetResourceName(FileSystem::GetFileNameFromFilePath(texPath));
		m_cubemapTexture->LoadFromFile(texPath);
		m_cubemapTexture->SetType(TextureType_CubeMap);
		m_cubemapTexture->SetWidth(1024);
		m_cubemapTexture->SetHeight(1024);
		m_cubemapTexture->SetGrayscale(false);

		// Add the actual "box"
		GetGameObject()->AddComponent<MeshFilter>().lock()->SetMesh(MeshType_Cube);

		// Add a mesh renderer
		shared_ptr<MeshRenderer> meshRenderer = GetGameObject()->AddComponent<MeshRenderer>().lock();
		meshRenderer->SetCastShadows(false);
		meshRenderer->SetReceiveShadows(false);
		meshRenderer->SetMaterialByType(Material_Skybox);
		shared_ptr<Material> material = meshRenderer->GetMaterial().lock();
		if (material)
		{
			material->SetTexture(m_cubemapTexture);
		}
		GetTransform()->SetScale(Vector3(1000, 1000, 1000));
	}

	void Skybox::Update()
	{
		if (m_anchor.expired())
			return;

		GetTransform()->SetPosition(m_anchor.lock()->GetTransform()->GetPosition());
	}

	/*------------------------------------------------------------------------------
									[MISC]
	------------------------------------------------------------------------------*/
	void** Skybox::GetEnvironmentTexture()
	{
		return m_cubemapTexture ? m_cubemapTexture->GetShaderResource() : nullptr;
	}
}