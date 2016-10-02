/*
Copyright(c) 2016 Panos Karabelas

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

//= LINKING ==========================
// Required by DDSTextureLoader when using Windows 10 SDK
//#pragma comment(lib, "WindowsApp.lib")
//====================================

//= INCLUDES =============================
#include "Skybox.h"
#include "../AssetImporting/DDSTextureImporter.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "../Core/GameObject.h"
#include "../Core/Helper.h"
#include "../Graphics/Texture.h"
#include "../Math/Vector3.h"
#include "../Pools/MaterialPool.h"
#include "../Core/Scene.h"
//========================================

//= NAMESPACES ================
using namespace DirectX;
using namespace Directus::Math;
using namespace std;
//=============================
Skybox::Skybox()
{
	m_environmentSRV = nullptr;
}

Skybox::~Skybox()
{
	SafeRelease(m_environmentSRV);
}

/*------------------------------------------------------------------------------
								[INTERFACE]
------------------------------------------------------------------------------*/
void Skybox::Initialize()
{
	// I had some trouble getting the FreeImage to load and create DDS textures, for now I load the .dds textures manually and then add them
	// to a material which then get's assigned to the MeshRenderer.
	HRESULT hr = CreateDDSTextureFromFile(g_graphicsDevice->GetDevice(), L"Assets/Environment/environment.dds", nullptr, &m_environmentSRV);
	if (FAILED(hr))
		return;

	Texture* texture = g_texturePool->GetTextureByPath("Assets/Environment/environment.dds");
	if (!texture)
	{
		texture = new Texture();
		texture->SetType(CubeMap);
		texture->SetFilePathImage("Assets/Environment/environment.dds");
		texture->SetWidth(1200);
		texture->SetHeight(1200);
		texture->SetGrayscale(false);
		texture->SetID3D11ShaderResourceView(m_environmentSRV);
	}
	g_texturePool->Add(texture);

	g_materialPool->GetMaterialStandardSkybox()->SetTexture(texture->GetID());

	// Add the actual "box"
	MeshFilter* mesh = g_gameObject->AddComponent<MeshFilter>();
	mesh->CreateCube();

	// Add a mesh renderer
	MeshRenderer* meshRenderer = g_gameObject->AddComponent<MeshRenderer>();
	meshRenderer->SetMaterialStandardSkybox();
	meshRenderer->SetCastShadows(false);
	meshRenderer->SetReceiveShadows(false);

	g_transform->SetScale(Vector3(1000, 1000, 1000));
}

void Skybox::Start()
{

}

void Skybox::Remove()
{

}

void Skybox::Update()
{
	GameObject* camera = g_scene->GetMainCamera();
	if (camera)
		g_transform->SetPosition(camera->GetTransform()->GetPosition());
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
ID3D11ShaderResourceView* Skybox::GetEnvironmentTexture() const
{
	return m_environmentSRV;
}