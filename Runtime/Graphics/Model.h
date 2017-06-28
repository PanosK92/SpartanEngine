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

//= INCLUDES ====================
#include <memory>
#include <vector>
#include "../Resource/Resource.h"
//===============================

namespace Directus
{
	class ResourceManager;
	class GameObject;
	class Mesh;
	struct VertexPosTexNorTan;

	class Model : public Resource
	{
	public:
		Model(Context* context);
		~Model();

		//= RESOURCE INTERFACE ================================
		virtual bool LoadFromFile(const std::string& filePath);
		virtual bool SaveToFile(const std::string& filePath);
		//======================================================

		void SetRootGameObject(std::weak_ptr<GameObject> gameObj) { m_rootGameObj = gameObj; }
		std::weak_ptr<Mesh> AddMesh(const std::string& gameObjID, const std::string& name, std::vector<VertexPosTexNorTan> vertices, std::vector<unsigned int> indices);
		std::weak_ptr<Mesh> GetMeshByID(const std::string& id);

		std::string CopyFileToLocalDirectory(const std::string& filePath);
		std::string GetOriginalFilePath() { return m_originalFilePath; }
		std::string GetOriginalDirectory() { return FileSystem::GetDirectoryFromFilePath(m_originalFilePath); }

		void NormalizeScale();
		void SetScale(float scale);

	private:
		bool LoadFromEngineFormat(const std::string& filePath);
		bool LoadFromForeignFormat(const std::string& filePath);

		float GetNormalizedScale();
		std::weak_ptr<Mesh> GetLargestBoundingBox();

		std::weak_ptr<GameObject> m_rootGameObj;
		std::vector<std::shared_ptr<Mesh>> m_meshes;
		ResourceManager* m_resourceManager;	
		std::string m_originalFilePath;
	};
}