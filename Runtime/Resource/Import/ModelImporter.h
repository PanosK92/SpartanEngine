/*
Copyright(c) 2016-2017 Panos Karabelas

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

//= INCLUDES ======================
#include "../../Graphics/Texture.h"
//=================================

struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

namespace Directus
{
	class Material;
	class GameObject;

	class ModelImporter
	{
	public:
		ModelImporter();
		~ModelImporter();

		bool Initialize(Context* context);
		void LoadAsync(const std::string& filePath);
		bool Load(const std::string& filePath);
		std::weak_ptr<GameObject> GetModelRoot();

	private:
		bool m_isLoading;
		std::string m_filePath;
		std::string m_fullTexturePath;
		std::string m_modelName;
		std::weak_ptr<GameObject> m_rootGameObject;
		Context* m_context;

		/*------------------------------------------------------------------------------
										[PROCESSING]
		------------------------------------------------------------------------------*/
		void ProcessNode(const aiScene* assimpScene, aiNode* assimpNode, std::weak_ptr<GameObject> parentNode, std::weak_ptr<GameObject> newNode);
		void ProcessMesh(aiMesh* assimpMesh, const aiScene* assimpScene, std::weak_ptr<GameObject> parentGameObject);
		std::shared_ptr<Material> GenerateMaterialFromAiMaterial(aiMaterial* assimpMaterial);

		/*------------------------------------------------------------------------------
										[HELPER FUNCTIONS]
		------------------------------------------------------------------------------*/
		void AddTextureToMaterial(std::weak_ptr<Material> material, TextureType textureType, const std::string& texturePath);
		std::string FindTexture(std::string texturePath);
		std::string TryPathWithMultipleExtensions(const std::string& fullpath);
	};
}