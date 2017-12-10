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
#include <string>

//= INCLUDES =================
#include "../../Core/Helper.h"
#include <memory>
//============================

struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

namespace Directus
{
	enum TextureType;
	class Mesh;
	class Context;
	class Material;
	class GameObject;
	class Model;
	class Transform;

	class DLL_API ModelImporter
	{
	public:
		ModelImporter(Context* context);
		~ModelImporter();

		void LoadAsync(Model* model, const std::string& filePath);
		void ReadAnimations(Model* model, const aiScene* scene);
		bool Load(Model* model, const std::string& filePath);

		const std::string& GetStatus() { return m_status; }
		float GetPercentage() { return (float)m_jobsTotal / (float)m_jobsDone; }
		bool IsLoading() { return m_isLoading; }

	private:
		// PROCESSING
		void ReadNodeHierarchy(Model* model, const aiScene* assimpScene, aiNode* assimpNode, const std::weak_ptr<GameObject>& parentNode, std::weak_ptr<GameObject>& newNode);
		void LoadMesh(Model* model, aiMesh* assimpMesh, const aiScene* assimpScene, const std::weak_ptr<GameObject>& parentGameObject);
		void LoadAiMeshVertices(aiMesh* assimpMesh, std::shared_ptr<Mesh> mesh);
		void LoadAiMeshIndices(aiMesh* assimpMesh, std::shared_ptr<Mesh> mesh);
		std::shared_ptr<Material> AiMaterialToMaterial(Model* model, aiMaterial* assimpMaterial);

		// HELPER FUNCTIONS
		std::string ValidateTexturePath(const std::string& texturePath);
		std::string TryPathWithMultipleExtensions(const std::string& fullpath);
		void CalculateNodeCount(aiNode* node, int& count);
		void ResetStats();
	
		Model* m_model;
		std::string m_modelPath;

		// Statistics	
		std::string m_status;
		int m_jobsDone;
		int m_jobsTotal;
		bool m_isLoading;
		
		Context* m_context;
	};
}