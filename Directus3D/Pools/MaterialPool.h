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

#pragma once

//= INCLUDES ====================
#include "../Graphics/Material.h"
//===============================

#define MATERIAL_DEFAULT_ID "MATERIAL_DEFAULT_ID"
#define MATERIAL_DEFAULT_SKYBOX_ID "MATERIAL_DEFAULT_SKYBOX_ID"

class MaterialPool : public Object
{
public:
	MaterialPool(Context* context);
	~MaterialPool();

	/*------------------------------------------------------------------------------
									[MISC]
	------------------------------------------------------------------------------*/	
	std::weak_ptr<Material> Add(std::weak_ptr<Material> material);
	std::weak_ptr<Material> Add(const std::string& filePath);
	void Add(const std::vector<std::string>& filePaths);

	void SaveMaterialMetadata();
	void Clear();

	std::weak_ptr<Material> GetMaterialByID(const std::string& materialID);
	std::weak_ptr<Material> GetMaterialStandardDefault();
	std::weak_ptr<Material> GetMaterialStandardSkybox();
	std::vector<std::string> GetAllMaterialFilePaths();
	const std::vector<std::shared_ptr<Material>>& GetAllMaterials();

private:
	void GenerateDefaultMaterials();

	std::vector<std::shared_ptr<Material>> m_materials;
	std::shared_ptr<Material> m_materialDefault;
	std::shared_ptr<Material> m_materialDefaultSkybox;	
};
