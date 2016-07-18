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
//===========================

//= NAMESPACES ================
using namespace std;
using namespace Directus::Math;
//=============================

MaterialPool::MaterialPool(TexturePool* texturePool, ShaderPool* shaderPool)
{
	m_texturePool = texturePool;
	m_shaderPool = shaderPool;
}

MaterialPool::~MaterialPool()
{
	Clear();
}

/*------------------------------------------------------------------------------
								[MISC]
------------------------------------------------------------------------------*/

// Removes all the materials
void MaterialPool::Clear()
{
	m_materials.clear();
	m_materials.shrink_to_fit();
}

Material* MaterialPool::AddMaterial(Material* material)
{
	unique_ptr<Material> smartPtrMaterial(material);

	if (!material)
	{
		LOG("The material is null, it can't be added to the pool.", Log::Warning);
		return nullptr;
	}

	// check for existing material from the same model
	for (int i = 0; i < m_materials.size(); i++)
	{
		if (m_materials[i]->GetName() == material->GetName())
			if (m_materials[i]->GetModelID() == material->GetModelID())
				return m_materials[i].get();
	}

	// if nothing of the above was true, add the 
	// material to the pool and return it
	m_materials.push_back(move(smartPtrMaterial));
	return m_materials.back().get();
}

Material* MaterialPool::GetMaterialByID(string materialID)
{
	for (auto i = 0; i < m_materials.size(); i++)
	{
		if (m_materials[i]->GetID() == materialID)
			return m_materials[i].get();
	}

	return nullptr;
}

Material* MaterialPool::GetMaterialStandardDefault()
{
	if (m_materials.empty())
		AddStandardMaterials();

	return GetMaterialByID("Standard_Material_0");
}

Material* MaterialPool::GetMaterialStandardSkybox()
{
	if (m_materials.empty())
		AddStandardMaterials();

	return GetMaterialByID("Standard_Material_1");
}

/*------------------------------------------------------------------------------
								[I/O]
------------------------------------------------------------------------------*/
void MaterialPool::Serialize()
{
	// save material count
	Serializer::SaveInt(int(m_materials.size()));

	// save materials
	for (unsigned int i = 0; i < m_materials.size(); i++)
		m_materials[i]->Serialize();
}

void MaterialPool::Deserialize()
{
	Clear();

	// load material count
	int materialCount = Serializer::LoadInt();

	// load materials
	for (int i = 0; i < materialCount; i++)
	{
		unique_ptr<Material> mat(new Material(m_texturePool, m_shaderPool));
		mat->Deserialize();
		m_materials.push_back(move(mat));
	}
}

/*------------------------------------------------------------------------------
							[HELPER FUNCTIONS]
------------------------------------------------------------------------------*/
void MaterialPool::RemoveMaterial(string materialID)
{
	Material* material = GetMaterialByID(materialID);

	// make sure the material is not null
	if (!material) return;

	vector<unique_ptr<Material>>::iterator it;
	for (it = m_materials.begin(); it != m_materials.end();)
	{
		if ((*it)->GetID() == material->GetID())
		{
			it = m_materials.erase(it);
			return;
		}
		++it;
	}
}

void MaterialPool::AddStandardMaterials()
{
	unique_ptr<Material> defaultMaterial(new Material(m_texturePool, m_shaderPool));
	defaultMaterial->SetName("Standard_Default");
	defaultMaterial->SetID("Standard_Material_0");
	defaultMaterial->SetColorAlbedo(Vector4(1, 1, 1, 1));
	m_materials.push_back(move(defaultMaterial));

	// A texture must be loaded for that one, if all goes smooth
	// it's done by the skybox component
	unique_ptr<Material> skyboxMaterial(new Material(m_texturePool, m_shaderPool));
	skyboxMaterial->SetName("Standard_Skybox");
	skyboxMaterial->SetID("Standard_Material_1");
	skyboxMaterial->SetFaceCullMode(CullFront);
	skyboxMaterial->SetColorAlbedo(Vector4(1, 1, 1, 1));
	m_materials.push_back(move(skyboxMaterial));
}
