/*
Copyright(c) 2016 Panos Karabelas

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and / or sell
copies of the Software, and to permit persons to whom the Software is furnished
to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

//= INCLUDES =================
#include "MaterialPool.h"
#include <vector>
#include "../IO/Serializer.h"
#include "../Core/Context.h"
//===========================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

MaterialPool::MaterialPool(Context* context) : Object(context) 
{
	GenerateDefaultMaterials();
}

MaterialPool::~MaterialPool()
{
	Clear();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/
// Adds a material to the pool directly from memory
shared_ptr<Material> MaterialPool::Add(shared_ptr<Material> material)
{
	if (!material)
		return nullptr;

	for (const auto& materialInPool : m_materials)
	{
		// Make sure the material is not already in the pool
		if (materialInPool->GetID() == material->GetID())
			return materialInPool;

		// Make sure that the material doesn't come from the same model
		// in which case the ID doesn't matter, if the name is the same
		if (materialInPool->GetName() == material->GetName())
			if (materialInPool->GetModelID() == material->GetModelID())
				return materialInPool;
	}

	m_materials.push_back(material);
	return m_materials.back();
}

// Adds multiple materials to the pool by reading them from files
void MaterialPool::Add(const vector<string>& filePaths)
{
	TexturePool* texturePool = g_context->GetSubsystem<TexturePool>();
	ShaderPool* shaderPool = g_context->GetSubsystem<ShaderPool>();

	for (const string& filePath : filePaths)
	{
		// Create and load the material
		auto material = make_shared<Material>(texturePool, shaderPool);
		if (material->LoadFromFile(filePath))
			m_materials.push_back(material);
	}
}

// Removes all the materials
void MaterialPool::Clear()
{
	m_materials.clear();
	m_materials.shrink_to_fit();

	GenerateDefaultMaterials();
}

shared_ptr<Material> MaterialPool::GetMaterialByID(const string& materialID)
{
	if (materialID == MATERIAL_DEFAULT_ID)
		return m_materialDefault;

	if (materialID == MATERIAL_DEFAULT_SKYBOX_ID)
		return m_materialDefaultSkybox;

	for (const auto& material : m_materials)
		if (material->GetID() == materialID)
			return material;

	return m_materialDefault;
}

shared_ptr<Material> MaterialPool::GetMaterialStandardDefault()
{
	return m_materialDefault;
}

shared_ptr<Material> MaterialPool::GetMaterialStandardSkybox()
{
	return m_materialDefaultSkybox;
}

vector<string> MaterialPool::GetAllMaterialFilePaths()
{
	vector<string> paths;

	for (const auto& material : m_materials)
		paths.push_back(material->GetFilePath());

	return paths;
}

const vector<shared_ptr<Material>>& MaterialPool::GetAllMaterials()
{
	return m_materials;
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void MaterialPool::GenerateDefaultMaterials()
{
	TexturePool* texturePool = g_context->GetSubsystem<TexturePool>();
	ShaderPool* shaderPool = g_context->GetSubsystem<ShaderPool>();

	m_materialDefault.reset();
	m_materialDefaultSkybox.reset();

	if (!m_materialDefault)
	{
		m_materialDefault = make_shared<Material>(texturePool, shaderPool);
		m_materialDefault->SetID(MATERIAL_DEFAULT_ID);
		m_materialDefault->SetName("Default");
		m_materialDefault->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		m_materialDefault->SetIsEditable(false);
	}

	if (!m_materialDefaultSkybox)
	{
		m_materialDefaultSkybox = make_shared<Material>(texturePool, shaderPool);
		m_materialDefaultSkybox->SetID(MATERIAL_DEFAULT_SKYBOX_ID);
		m_materialDefaultSkybox->SetName("Default_Skybox");
		m_materialDefaultSkybox->SetFaceCullMode(CullNone);
		m_materialDefaultSkybox->SetColorAlbedo(Vector4(1.0f, 1.0f, 1.0f, 1.0f));
		m_materialDefaultSkybox->SetIsEditable(false);
	}

	Add(m_materialDefault);
	Add(m_materialDefaultSkybox);
}
