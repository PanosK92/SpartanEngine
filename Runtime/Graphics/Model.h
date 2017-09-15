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

		//= RESOURCE INTERFACE ================================
		virtual bool LoadFromFile(const std::string& filePath);
		virtual bool SaveToFile(const std::string& filePath);
		//======================================================

		// Sets the  GameObject that represents this model in the scene
		void SetRootGameObject(std::weak_ptr<GameObject> gameObj) { m_rootGameObj = gameObj; }

		// Adds a mesh by creating from scratch
		std::weak_ptr<Mesh> AddMeshAsNewResource(unsigned int gameObjID, const std::string& name, std::vector<VertexPosTexTBN> vertices, std::vector<unsigned int> indices);

		// Adds a new mesh
		void AddMeshAsNewResource(std::shared_ptr<Mesh> mesh);

		// Adds a new material
		std::weak_ptr<Material> AddMaterialAsNewResource(std::shared_ptr<Material> material);

		// Adds a new animation
		std::weak_ptr<Animation> AddAnimationAsNewResource(std::shared_ptr<Animation> animation);

		std::weak_ptr<Mesh> GetMeshByID(unsigned int id);
		std::weak_ptr<Mesh> GetMeshByName(const std::string& name);

		bool IsAnimated() { return m_isAnimated; }
		void SetAnimated(bool isAnimated) { m_isAnimated = isAnimated; }

		std::string CopyTextureToLocalDirectory(const std::string& from);

		const Math::BoundingBox& GetBoundingBox() { return m_boundingBox; }
		float GetBoundingSphereRadius();

	private:
		// Load the model from disk
		bool LoadFromEngineFormat(const std::string& filePath);
		bool LoadFromForeignFormat(const std::string& filePath);

		//= SCALING / DIMENSIONS =======================
		void SetScale(float scale);
		float ComputeNormalizeScale();
		std::weak_ptr<Mesh> ComputeLargestBoundingBox();
		void ComputeDimensions();
		//==============================================

		// The root GameObject that represents this model in the scene
		std::weak_ptr<GameObject> m_rootGameObj;

		// Bounding box
		Math::BoundingBox m_boundingBox;
		float m_normalizedScale;

		// References to key resources
		std::vector<std::shared_ptr<Mesh>> m_meshes;
		std::vector<std::weak_ptr<Material>> m_materials;
		std::vector<std::weak_ptr<Animation>> m_animations;

		// Misc
		bool m_isAnimated;

		// Dependencies
		ResourceManager* m_resourceManager;
	};
}