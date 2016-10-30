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

//= INCLUDES ================
#include "../Graphics/Mesh.h"
#include <memory>
#include "../Core/Object.h"
//===========================

#define MESH_DEFAULT_CUBE_ID "DEFAULT_MESH_CUBE"
#define MESH_DEFAULT_QUAD_ID "DEFAULT_MESH_QUAD"

class GameObject;

class MeshPool : public Object
{
public:
	MeshPool(Context* context);
	~MeshPool();

	/*------------------------------------------------------------------------------
										[MISC]
	------------------------------------------------------------------------------*/
	void Clear();
	std::weak_ptr<Mesh> Add(std::shared_ptr<Mesh> mesh);
	std::weak_ptr<Mesh> Add(const std::string& name, const std::string& rootGameObjectID, const std::vector<VertexPositionTextureNormalTangent>& vertices, const std::vector<unsigned int>& indices);
	void Add(const std::vector<std::string>& filePaths);

	std::weak_ptr<Mesh> GetMeshByID(const std::string& ID);
	std::weak_ptr<Mesh> GetMeshByPath(const std::string& path);

	std::vector<std::string> GetAllMeshFilePaths();
	std::vector<std::weak_ptr<Mesh>> GetModelMeshesByModelName(const std::string& modelName);
	int GetMeshCount();

	//= DEFAULT MESHES ============================================================================================
	std::weak_ptr<Mesh> GetDefaultCube();
	std::weak_ptr<Mesh> GetDefaultQuad();
	//=============================================================================================================

	//= MESH PROCESSING =============================================================================
	float GetNormalizedModelScaleByRootGameObjectID(const std::string& modelName);
	void SetModelScale(const std::string& rootGameObjectID, float scale);
	void NormalizeModelScale(GameObject* rootGameObject);
	static std::weak_ptr<Mesh> GetLargestBoundingBox(const std::vector<std::weak_ptr<Mesh>>& meshes);
	//===============================================================================================

private:
	void GenerateDefaultMeshes();
	void CreateCube(std::vector<VertexPositionTextureNormalTangent>& vertices, std::vector<unsigned int>& indices);
	void CreateQuad(std::vector<VertexPositionTextureNormalTangent>& vertices, std::vector<unsigned int>& indices);

	std::vector<std::shared_ptr<Mesh>> m_meshes;
	std::shared_ptr<Mesh> m_defaultCube;
	std::shared_ptr<Mesh> m_defaultQuad;
};
