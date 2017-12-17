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
#include "../Math/BoundingBox.h"
#include "Texture.h"
//===============================

namespace Directus
{
	class ResourceManager;
	class GameObject;
	class Mesh;
	class Material;
	class Animation;
	struct VertexPosTexTBN;

	namespace Math
	{
		class BoundingBox;
	}

	class DLL_API Model : public Resource
	{
	public:
		Model(Context* context);
		~Model();

		//= RESOURCE INTERFACE =============================================
		bool LoadFromFile(const std::string& filePath) override;
		bool SaveToFile(const std::string& filePath) override;
		unsigned int GetMemoryUsageKB() override { return m_memoryUsageKB; }
		//==================================================================

		// Sets the GameObject that represents this model in the scene
		void SetRootGameObject(std::weak_ptr<GameObject> gameObj) { m_rootGameObj = gameObj; }

		// Adds a mesh by creating it from scratch
		void AddMesh(const std::string& name, std::vector<VertexPosTexTBN>& vertices, std::vector<unsigned int>& indices, std::weak_ptr<GameObject> gameObject);

		// Adds a new mesh
		void AddMesh(std::weak_ptr<Mesh> mesh, std::weak_ptr<GameObject> gameObject);

		// Adds a new material
		void AddMaterial(std::weak_ptr<Material> material, std::weak_ptr<GameObject> gameObject);

		// Adds a new animation
		std::weak_ptr<Animation> AddAnimation(std::weak_ptr<Animation> animation);

		// Adds a texture (the material that uses this texture must be passed as well)
		void AddTexture(const std::weak_ptr<Material> material, TextureType textureType, const std::string& texturePath);

		std::weak_ptr<Mesh> GetMeshByName(const std::string& name);

		bool IsAnimated() { return m_isAnimated; }
		void SetAnimated(bool isAnimated) { m_isAnimated = isAnimated; }

		// Returns model's bounding box (a merge of all the bounding boxes of it's meshes)
		const Math::BoundingBox& GetBoundingBox() { return m_boundingBox; }
		float GetBoundingSphereRadius();

		// Returns the number of meshes used by this model
		unsigned int GetMeshCount() { return m_meshes.size(); }

		void SetWorkingDirectory(const std::string& directory);

	private:
		// Load the model from disk
		bool LoadFromEngineFormat(const std::string& filePath);
		bool LoadFromForeignFormat(const std::string& filePath);

		// Scale relate functions
		float ComputeNormalizeScale();
		std::weak_ptr<Mesh> ComputeLargestBoundingBox();

		// Misc
		void ComputeMemoryUsage();

		// The root GameObject that represents this model in the scene
		std::weak_ptr<GameObject> m_rootGameObj;

		// Weak references to key resources
		std::vector<std::weak_ptr<Mesh>> m_meshes;
		std::vector<std::weak_ptr<Material>> m_materials;
		std::vector<std::weak_ptr<Animation>> m_animations;

		// Directories relative to this model
		std::string m_modelDirectoryModel;
		std::string m_modelDirectoryMeshes;
		std::string m_modelDirectoryMaterials;
		std::string m_modelDirectoryTextures;

		// Misc
		Math::BoundingBox m_boundingBox;
		float m_normalizedScale;
		bool m_isAnimated;
		ResourceManager* m_resourceManager;
		unsigned int m_memoryUsageKB;
	};
}