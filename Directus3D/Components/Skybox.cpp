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
// Required by DDSTextureLoader
#pragma comment(lib, "WindowsApp.lib")
//====================================

//= INCLUDES ===========================
#include "Skybox.h"
#include "../Loading/DDSTextureLoader.h"
#include "Transform.h"
#include "MeshRenderer.h"
#include "MeshFilter.h"
#include "../Core/GameObject.h"
#include "../Core/Globals.h"
#include "../Core/Texture.h"
#include "../Math/Vector3.h"
#include "../Pools/MaterialPool.h"
//======================================

//= NAMESPACES ================
using namespace DirectX;
using namespace Directus::Math;
using namespace std;
//=============================
Skybox::Skybox()
{
	m_environmentSRV = nullptr;
	m_irradianceSRV = nullptr;
}

Skybox::~Skybox()
{
	DirectusSafeRelease(m_irradianceSRV);
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

	// Load irradiance texture
	hr = CreateDDSTextureFromFile(g_graphicsDevice->GetDevice(), L"Assets/Environment/irradiance.dds", nullptr, &m_irradianceSRV);
	if (FAILED(hr))
		return;

	Texture* texture = g_texturePool->GetTextureByPath("Assets/Environment/environment.dds");
	if (!texture)
	{
		texture = g_texturePool->CreateNewTexture();
		texture->SetType(CubeMap);
		texture->SetPath("Assets/Environment/environment.dds");
		texture->SetWidth(1024);
		texture->SetHeight(1024);
		texture->SetGrayscale(false);
		texture->SetID3D11ShaderResourceView(m_environmentSRV);
	}

	g_materialPool->GetMaterialStandardSkybox()->SetTexture(texture->GetID());

	// Add the actual "box"
	MeshFilter* mesh = g_gameObject->AddComponent<MeshFilter>();
	mesh->CreateCube();

	// Add a mesh renderer
	MeshRenderer* meshRenderer = g_gameObject->AddComponent<MeshRenderer>();
	meshRenderer->SetMaterialStandardSkybox();

	g_transform->SetScale(Vector3(500, 500, 500));
}

void Skybox::Update()
{
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

ID3D11ShaderResourceView* Skybox::GetIrradianceTexture() const
{
	return m_irradianceSRV;
}
