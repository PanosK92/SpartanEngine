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

//= INCLUDES ============
#include "../Core/Mesh.h"
#include <memory>
//=======================

class GameObject;

class MeshPool
{
public:
	MeshPool();
	~MeshPool();

	/*------------------------------------------------------------------------------
										[MISC]
	------------------------------------------------------------------------------*/
	void DeleteAll();
	Mesh* AddMesh(std::string name, std::string rootGameObjectID, std::string gameObjectID, std::vector<VertexPositionTextureNormalTangent> vertices, std::vector<unsigned int> indices);
	Mesh* GetMesh(std::string ID);
	std::vector<Mesh*> GetModelMeshesByModelName(std::string modelName);

	//= MESH PROCESSING =============================================================================
	float GetNormalizedModelScaleByRootGameObjectID(std::string modelName);
	void SetMeshScale(Mesh* meshData, float scale);
	void SetModelScale(std::string rootGameObjectID, float scale);
	void NormalizeModelScale(GameObject* rootGameObject);
	Mesh* GetLargestBoundingBox(const std::vector<Mesh*>& meshes);
	Directus::Math::Vector3 GetMeshExtent(Mesh* mesh);
	Directus::Math::Vector3 GetMeshExtent(const Directus::Math::Vector3& min, const Directus::Math::Vector3& max);
	Directus::Math::Vector3 GetMeshCenter(const Directus::Math::Vector3& min, const Directus::Math::Vector3& max);
	void GetMinMax(Mesh* mesh, Directus::Math::Vector3& min, Directus::Math::Vector3& max);
	//===============================================================================================

	/*------------------------------------------------------------------------------
										[I/O]
	------------------------------------------------------------------------------*/
	void Serialize();
	void Deserialize();

private:
	std::vector<Mesh*> m_meshPool;

	/*------------------------------------------------------------------------------
									[HELPER FUNCTIONS]
	------------------------------------------------------------------------------*/
	static void SaveVertex(const VertexPositionTextureNormalTangent& vertex);
	static VertexPositionTextureNormalTangent LoadVertex();
};
