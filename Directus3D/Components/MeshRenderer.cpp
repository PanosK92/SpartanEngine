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

//= INCLUDES =====================
#include "MeshRenderer.h"
#include "Transform.h"
#include "../IO/Serializer.h"
#include "../Math/Matrix.h"
#include "../IO/Log.h"
#include "../Core/GameObject.h"
#include "../Pools/MaterialPool.h"
//================================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;

//=============================

MeshRenderer::MeshRenderer()
{
	m_castShadows = true;
	m_receiveShadows = true;
	m_material = nullptr;
}

MeshRenderer::~MeshRenderer()
{
}

/*------------------------------------------------------------------------------
							[INTERFACE]
------------------------------------------------------------------------------*/

void MeshRenderer::Initialize()
{
}

void MeshRenderer::Update()
{
}

void MeshRenderer::Save()
{
	Serializer::SaveSTR(m_material->GetID());
	Serializer::SaveBool(m_castShadows);
	Serializer::SaveBool(m_receiveShadows);
}

void MeshRenderer::Load()
{
	m_material = g_materialPool->GetMaterialByID(Serializer::LoadSTR());
	m_castShadows = Serializer::LoadBool();
	m_receiveShadows = Serializer::LoadBool();
}

/*------------------------------------------------------------------------------
									[MISC]
------------------------------------------------------------------------------*/
void MeshRenderer::Render(unsigned int indexCount, Matrix viewMatrix, Matrix projectionMatrix, Light* directionalLight)
{
	if (!HasMaterial()) // If there is a material
	{
		LOG("GameObject \"" + g_gameObject->GetName() + "\" has no material. It can't be rendered.");
		return;
	}

	if (!GetMaterial()->HasShader()) // And a shader is associated with it
	{
		LOG("GameObject \"" + g_gameObject->GetName() + "\" has a material but not a shader associated with it. It can't be rendered.");
		return;
	}

	// Render
	GetMaterial()->GetShader()->Set();
	GetMaterial()->GetShader()->Render(
		             indexCount,
		             g_transform->GetWorldMatrix(),
		             viewMatrix, projectionMatrix,
		             directionalLight,
		             GetMaterial()
	             );
}

/*------------------------------------------------------------------------------
								[PROPERTIES]
------------------------------------------------------------------------------*/
void MeshRenderer::SetCastShadows(bool castShadows)
{
	m_castShadows = castShadows;
}

bool MeshRenderer::GetCastShadows()
{
	return m_castShadows;
}

void MeshRenderer::SetReceiveShadows(bool receiveShadows)
{
	m_receiveShadows = receiveShadows;
}

bool MeshRenderer::GetReceiveShadows()
{
	return m_receiveShadows;
}

/*------------------------------------------------------------------------------
									[MATERIAL]
------------------------------------------------------------------------------*/
Material* MeshRenderer::GetMaterial()
{
	return m_material;
}

void MeshRenderer::SetMaterial(Material* material)
{
	// Add the material to the pool
	m_material = g_materialPool->AddMaterial(material);
}

void MeshRenderer::SetMaterialStandardDefault()
{
	m_material = g_materialPool->GetMaterialStandardDefault();
}

void MeshRenderer::SetMaterialStandardSkybox()
{
	m_material = g_materialPool->GetMaterialStandardSkybox();
}

bool MeshRenderer::HasMaterial()
{
	if (!GetMaterial())
		return false;

	return true;
}
