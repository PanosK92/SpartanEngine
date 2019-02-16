/*
Copyright(c) 2016-2019 Panos Karabelas

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

//= INCLUDES ========================
#include "../../Core/EngineDefs.h"
#include "../../RHI/RHI_Definition.h"
#include <memory>
#include <string>
#include <vector>
//===================================

struct aiNode;
struct aiScene;
struct aiMaterial;
struct aiMesh;

namespace Directus
{
	class Mesh;
	class Context;
	class Material;
	class Entity;
	class Model;
	class Transform;
	class World;

	class ENGINE_CLASS ModelImporter
	{
	public:
		ModelImporter(Context* context);
		~ModelImporter() {}

		bool Load(std::shared_ptr<Model> model, const std::string& filePath);

	private:
		// PROCESSING
		void ReadNodeHierarchy(const aiScene* assimpScene, aiNode* assimpNode, std::shared_ptr<Model>& model, Entity* parentNode = nullptr, Entity* newNode = nullptr);
		void ReadAnimations(const aiScene* scene, std::shared_ptr<Model>& model);
		void LoadMesh(const aiScene* assimpScene, aiMesh* assimpMesh, std::shared_ptr<Model>& model, Entity* parententity);
		void AssimpMesh_ExtractVertices(aiMesh* assimpMesh, std::vector<RHI_Vertex_PosUvNorTan>* vertices);
		void AssimpMesh_ExtractIndices(aiMesh* assimpMesh, std::vector<unsigned int>* indices);
		std::shared_ptr<Material> AiMaterialToMaterial(aiMaterial* assimpMaterial, std::shared_ptr<Model>& model);

		Context* m_context;
		World* m_world;
	};
}