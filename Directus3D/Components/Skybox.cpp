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
#include "../Graphics/Texture.h"
#include "../Math/Vector3.h"
#include "../Pools/MaterialPool.h"
#include "../Core/Scene.h"
#include "../IO/Log.h"
//========================================

//= NAMESPACES ================
using namespace DirectX;
using namespace Directus::Math;
using namespace std;
//=============================
Skybox::Skybox()
{
	m_cubeMapTexture = nullptr;
}

Skybox::~Skybox()
{

}

/*------------------------------------------------------------------------------
								[INTERFACE]
------------------------------------------------------------------------------*/
void Skybox::Initialize()
{
	ID3D11ShaderResourceView* cubeMapSRV = nullptr;
	HRESULT hr = CreateDDSTextureFromFile(g_context->GetSubsystem<Graphics>()->GetDevice(), L"Assets/Environment/environment.dds", nullptr, &cubeMapSRV);
	if (FAILED(hr))
		return;

	m_cubeMapTexture = make_shared<Texture>();
	m_cubeMapTexture->SetType(CubeMap);
	m_cubeMapTexture->SetFilePathTexture("Assets/Environment/environment.dds");
	m_cubeMapTexture->SetWidth(1200);
	m_cubeMapTexture->SetHeight(1200);
	m_cubeMapTexture->SetGrayscale(false);
	m_cubeMapTexture->SetID3D11ShaderResourceView(cubeMapSRV);

	// Add the actual "box"
	g_gameObject->AddComponent<MeshFilter>()->SetDefaultMesh(Cube);
	
	// Add a mesh renderer
	auto meshRenderer = g_gameObject->AddComponent<MeshRenderer>();
	meshRenderer->SetCastShadows(false);
	meshRenderer->SetReceiveShadows(false);
	meshRenderer->SetMaterial(g_context->GetSubsystem<MaterialPool>()->GetMaterialStandardSkybox());
	meshRenderer->GetMaterial()->SetTexture(m_cubeMapTexture);

	g_transform->SetScale(Vector3(1000, 1000, 1000));

	g_gameObject->SetHierarchyVisibility(true);
}

void Skybox::Start()
{

}

void Skybox::Remove()
{

}

void Skybox::Update()
{
	GameObject* camera = g_context->GetSubsystem<Scene>()->GetMainCamera();

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
	return m_cubeMapTexture ? m_cubeMapTexture->GetID3D11ShaderResourceView() : nullptr;
}