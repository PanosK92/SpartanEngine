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
#include "../IO/Log.h"
#include "../IO/Serializer.h"
#include "../IO/FileSystem.h"
//===========================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

MaterialPool::MaterialPool(TexturePool* texturePool, ShaderPool* shaderPool)
{
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;

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
shared_ptr<Material> MaterialPool::Add(shared_ptr<Material> materialIn)
{
	if (!materialIn)
	{
		LOG_WARNING("The material is null, it can't be added to the pool.");
		return nullptr;
	}

	// check for existing material from the same model
	for (auto material : m_materials)
	{
		if (material->GetName() == materialIn->GetName())
			if (material->GetModelID() == materialIn->GetModelID())
				return material;
	}

	m_materials.push_back(materialIn);
	return m_materials.back();
}

// Adds multiple materials to the pool by reading them from files
void MaterialPool::Add(const vector<string>& filePaths)
{
	for (const string& filePath : filePaths)
	{
		// Make sure the path is valid
		if (!FileSystem::FileExists(filePath))
			continue;

		// Make sure it's actually a material file
		if (FileSystem::GetExtensionFromPath(filePath) != MATERIAL_EXTENSION)
			continue;

		// Create and load the material
		shared_ptr<Material> material = make_shared<Material>(m_texturePool, m_shaderPool);
		if (material->LoadFromFile(filePath))
			m_materials.push_back(material);
	}
}

// Removes all the materials
void MaterialPool::Clear()
{
	m_materials.clear();
	m_materials.shrink_to_fit();
}

shared_ptr<Material> MaterialPool::GetMaterialByID(const string& materialID)
{
	if (materialID == MATERIAL_DEFAULT_ID)
		return m_materialDefault;

	if (materialID == MATERIAL_DEFAULT_SKYBOX_ID)
		return m_materialDefaultSkybox;

	for (auto material : m_materials)
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

	for (auto material : m_materials)
		paths.push_back(material->GetFilePath());

	return paths;
}

vector<shared_ptr<Material>> MaterialPool::GetAllMaterials()
{
	vector<shared_ptr<Material>> materials = m_materials;
	materials.push_back(m_materialDefault);
	materials.push_back(m_materialDefaultSkybox);
	return materials;
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void MaterialPool::GenerateDefaultMaterials()
{
	if (!m_materialDefault)
	{
		m_materialDefault = make_shared<Material>(m_texturePool, m_shaderPool);
		m_materialDefault->SetID(MATERIAL_DEFAULT_ID);
		m_materialDefault->SetName("Standard_Default");
		m_materialDefault->SetID("Standard_Material_0");
		m_materialDefault->SetColorAlbedo(Vector4(1, 1, 1, 1));
		m_materialDefault->SetIsEditable(false);
	}

	if (!m_materialDefaultSkybox)
	{
		m_materialDefaultSkybox = make_shared<Material>(m_texturePool, m_shaderPool);
		m_materialDefaultSkybox->SetID(MATERIAL_DEFAULT_SKYBOX_ID);
		m_materialDefaultSkybox->SetName("Standard_Skybox");
		m_materialDefaultSkybox->SetID("Standard_Material_1");
		m_materialDefaultSkybox->SetFaceCullMode(CullNone);
		m_materialDefaultSkybox->SetColorAlbedo(Vector4(1, 1, 1, 1));
		m_materialDefaultSkybox->SetIsEditable(false);
	}
}
