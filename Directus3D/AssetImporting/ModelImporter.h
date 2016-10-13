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
#include "../Pools/MeshPool.h"
#include "../Pools/TexturePool.h"
#include "../Pools/ShaderPool.h"
#include "../Pools/ThreadPool.h"
//===============================

struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

class ModelImporter
{
public:
	ModelImporter();
	~ModelImporter();

	void Initialize(MeshPool* meshPool, TexturePool* texturePool, ShaderPool* shaderPool, MaterialPool* materialPool, ThreadPool* threadPool);
	void LoadAsync(GameObject* gameObject, const std::string& filePath);
	bool Load(GameObject* gameObject, const std::string& filePath);

private:
	std::string m_fullModelPath;
	std::string m_fullTexturePath;
	std::string m_modelName;
	GameObject* m_rootGameObject;

	/*------------------------------------------------------------------------------
									[PROCESSING]
	------------------------------------------------------------------------------*/
	void ProcessNode(aiNode* assimpNode, const aiScene* assimpScene, GameObject* rootGameObject);
	void ProcessMesh(aiMesh* assimpMesh, const aiScene* assimpScene, GameObject* parentGameObject);
	std::shared_ptr<Material> GenerateMaterialFromAiMaterial(aiMaterial* assimpMaterial);
	
	/*------------------------------------------------------------------------------
									[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	void AddTextureToMaterial(std::shared_ptr<Material> material, TextureType textureType, const std::string& texturePath);
	std::string FindTexture(std::string texturePath);
	std::string TryPathWithMultipleExtensions(const std::string& fullpath);

	MeshPool* m_meshPool;
	TexturePool* m_texturePool;
	ShaderPool* m_shaderPool;
	MaterialPool* m_materialPool;
	ThreadPool* m_threadPool;
};
