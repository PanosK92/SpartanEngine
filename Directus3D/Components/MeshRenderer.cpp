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
#include "../IO/Log.h"
#include "../Core/GameObject.h"
#include "../Pools/MaterialPool.h"
#include "../Core/Helper.h"
#include "../Graphics/Frustrum.h"
#include "../Graphics/Mesh.h"
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

//= ICOMPONENT ===============================================================
void MeshRenderer::Initialize()
{
	m_material = g_materialPool->GetMaterialStandardDefault();
}

void MeshRenderer::Start()
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
	Serializer::WriteSTR(m_material ? m_material->GetID() : (string)DATA_NOT_ASSIGNED);
	Serializer::WriteBool(m_castShadows);
	Serializer::WriteBool(m_receiveShadows);
}

void MeshRenderer::Deserialize()
{
	m_material = g_materialPool->GetMaterialByID(Serializer::ReadSTR());
	m_castShadows = Serializer::ReadBool();
	m_receiveShadows = Serializer::ReadBool();
}
//==============================================================================

//= MISC =======================================================================
void MeshRenderer::SetShader() const
{
	GetMaterial()->GetShader()->Set();
}

void MeshRenderer::Render(unsigned int indexCount) const
{
	shared_ptr<Material> material = GetMaterial();

	if (!material) // Check if a material exists
	{
		LOG_WARNING("GameObject \"" + g_gameObject->GetName() + "\" has no material. It can't be rendered.");
		return;
	}

	if (!material->HasShader()) // Check if the material has a shader
	{
		LOG_WARNING("GameObject \"" + g_gameObject->GetName() + "\" has a material but not a shader associated with it. It can't be rendered.");
		return;
	}

	// Set the buffers and draw
	GetMaterial()->GetShader()->Draw(indexCount);
}

//==============================================================================

//= PROPERTIES =================================================================
void MeshRenderer::SetCastShadows(bool castShadows)
{
	m_castShadows = castShadows;
}

bool MeshRenderer::GetCastShadows() const
{
	return m_castShadows;
}

void MeshRenderer::SetReceiveShadows(bool receiveShadows)
{
	m_receiveShadows = receiveShadows;
}

bool MeshRenderer::GetReceiveShadows() const
{
	return m_receiveShadows;
}
//==============================================================================

//= MATERIAL ===================================================================
shared_ptr<Material> MeshRenderer::GetMaterial() const
{
	return m_material;
}

void MeshRenderer::SetMaterial(shared_ptr<Material> material)
{
	m_material = material;
}

bool MeshRenderer::HasMaterial() const
{
	if (!GetMaterial())
		return false;

	return true;
}
//==============================================================================