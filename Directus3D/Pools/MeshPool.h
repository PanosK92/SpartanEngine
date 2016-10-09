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
//===========================

class GameObject;

class MeshPool
{
public:
	MeshPool();
	~MeshPool();

	/*------------------------------------------------------------------------------
										[MISC]
	------------------------------------------------------------------------------*/
	void Clear();
	Mesh* Add(const std::string& name, const std::string& rootGameObjectID, const std::vector<VertexPositionTextureNormalTangent>& vertices, const std::vector<unsigned int>& indices);
	void Add(const std::vector<std::string>& filePaths);
	Mesh* GetMesh(const std::string& ID);
	std::vector<std::string> GetAllMeshFilePaths();
	std::vector<Mesh*> GetModelMeshesByModelName(const std::string& modelName);
	int GetMeshCount();

	//= DEFAULT MESHES ============================================================================================
	void GenerateDefaultMeshes();
	void CreateCube(std::vector<VertexPositionTextureNormalTangent>& vertices, std::vector<unsigned int>& indices);
	void CreateQuad(std::vector<VertexPositionTextureNormalTangent>& vertices, std::vector<unsigned int>& indices);
	Mesh* GetDefaultCube();
	Mesh* GetDefaultQuad();
	//=============================================================================================================

	//= MESH PROCESSING =============================================================================
	float GetNormalizedModelScaleByRootGameObjectID(const std::string& modelName);
	void SetModelScale(const std::string& rootGameObjectID, float scale);
	void NormalizeModelScale(GameObject* rootGameObject);
	static Mesh* GetLargestBoundingBox(const std::vector<Mesh*>& meshes);
	//===============================================================================================

private:
	std::vector<Mesh*> m_meshes;

	Mesh* m_defaultCube;
	Mesh* m_defaultQuad;
};
