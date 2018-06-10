/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES =====================
#include <memory>
#include <vector>
#include "RI/Backend_Def.h"
#include "../Resource/IResource.h"
#include "../Math/BoundingBox.h"
//================================

namespace Directus
{
	class ResourceManager;
	class GameObject;
	class Mesh;
	class Material;
	class Animation;

	namespace Math
	{
		class BoundingBox;
	}

	class ENGINE_CLASS Model : public IResource
	{
	public:
		Model(Context* context);
		~Model();

		//= RESOURCE INTERFACE ====================================
		bool LoadFromFile(const std::string& filePath) override;
		bool SaveToFile(const std::string& filePath) override;
		unsigned int GetMemory() override { return m_memoryUsage; }
		//=========================================================

		// Sets the GameObject that represents this model in the scene
		void SetRootGameObject(const std::weak_ptr<GameObject>& gameObj) { m_rootGameObj = gameObj; }

		// Adds a mesh by creating it from scratch
		void AddMesh(const std::string& name, std::vector<RI_Vertex_PosUVTBN>& vertices, std::vector<unsigned int>& indices, const std::weak_ptr<GameObject>& gameObject);

		// Adds a new mesh
		void AddMesh(const std::weak_ptr<Mesh>& mesh, const std::weak_ptr<GameObject>& gameObject, bool autoCache = true);

		// Adds a new material
		void AddMaterial(const std::weak_ptr<Material>& material, const std::weak_ptr<GameObject>& gameObject, bool autoCache = true);

		// Adds a new animation
		std::weak_ptr<Animation> AddAnimation(std::weak_ptr<Animation> animation);

		// Adds a texture (the material that uses this texture must be passed as well)
		void AddTexture(const std::weak_ptr<Material>& material, TextureType textureType, const std::string& filePath);

		std::weak_ptr<Mesh> GetMeshByName(const std::string& name);

		bool IsAnimated() { return m_isAnimated; }
		void SetAnimated(bool isAnimated) { m_isAnimated = isAnimated; }

		// Returns model's bounding box (a merge of all the bounding boxes of it's meshes)
		const Math::BoundingBox& GetBoundingBox() { return m_boundingBox; }
		float GetBoundingSphereRadius();

		// Returns the number of meshes used by this model
		unsigned int GetMeshCount() { return (unsigned int)m_meshes.size(); }

		void SetWorkingDirectory(const std::string& directory);

	private:
		// Load the model from disk
		bool LoadFromEngineFormat(const std::string& filePath);
		bool LoadFromForeignFormat(const std::string& filePath);

		void AddStandardComponents(const std::weak_ptr<GameObject>& gameObject, const std::weak_ptr<Mesh>& mesh);

		// Scale relate functions
		float ComputeNormalizeScale();
		std::weak_ptr<Mesh> ComputeLargestBoundingBox();

		// Misc
		void ComputeMemoryUsage();
		bool DetermineMeshUniqueness(Mesh* mesh);

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
		unsigned int m_memoryUsage;
	};
}