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

//= INCLUDES =====================
#include <memory>
#include <vector>
#include "../RHI/RHI_Definition.h"
#include "../Resource/IResource.h"
#include "../Math/BoundingBox.h"
#include "Material.h"
//================================

namespace Directus
{
	class ResourceCache;
	class Entity;
	class Mesh;
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

		//= RESOURCE INTERFACE =========================================
		bool LoadFromFile(const std::string& filePath) override;
		bool SaveToFile(const std::string& filePath) override;
		unsigned int GetMemoryUsage() override { return m_memoryUsage; }
		//==============================================================

		// Sets the entity that represents this model in the scene
		void SetRootentity(const std::shared_ptr<Entity>& entity) { m_rootentity = entity; }

		//= GEOMTETRY =============================================
		void Geometry_Append(
			std::vector<unsigned int>& indices,
			std::vector<RHI_Vertex_PosUvNorTan>& vertices,
			unsigned int* indexOffset = nullptr,
			unsigned int* vertexOffset = nullptr
		);
		void Geometry_Get(
			unsigned int indexOffset,
			unsigned int indexCount,
			unsigned int vertexOffset, 
			unsigned int vertexCount,
			std::vector<unsigned int>* indices,
			std::vector<RHI_Vertex_PosUvNorTan>* vertices
		);
		void Geometry_Update();
		const Math::BoundingBox& Geometry_AABB() { return m_aabb; }
		//=========================================================

		// Add resources to the model
		void AddMaterial(std::shared_ptr<Material>& material, const std::shared_ptr<Entity>& entity);
		void AddAnimation(std::shared_ptr<Animation>& animation);
		void AddTexture(std::shared_ptr<Material>& material, TextureType textureType, const std::string& filePath);

		bool IsAnimated() { return m_isAnimated; }
		void SetAnimated(bool isAnimated) { m_isAnimated = isAnimated; }

		void SetWorkingDirectory(const std::string& directory);

		std::shared_ptr<RHI_IndexBuffer> GetIndexBuffer()	{ return m_indexBuffer; }
		std::shared_ptr<RHI_VertexBuffer> GetVertexBuffer() { return m_vertexBuffer; }

	private:
		// Load the model from disk
		bool LoadFromEngineFormat(const std::string& filePath);
		bool LoadFromForeignFormat(const std::string& filePath);

		// Geometry
		bool Geometry_CreateBuffers();
		float Geometry_ComputeNormalizedScale();
		unsigned int Geometry_ComputeMemoryUsage();

		// The root entity that represents this model in the scene
		std::weak_ptr<Entity> m_rootentity;

		// Geometry
		std::shared_ptr<RHI_VertexBuffer> m_vertexBuffer;
		std::shared_ptr<RHI_IndexBuffer> m_indexBuffer;
		std::shared_ptr<Mesh> m_mesh;
		Math::BoundingBox m_aabb;
		unsigned int meshCount;

		// Material
		std::vector<std::shared_ptr<Material>> m_materials;

		// Animations
		std::vector<std::shared_ptr<Animation>> m_animations;

		// Directories relative to this model
		std::string m_modelDirectoryModel;
		std::string m_modelDirectoryMaterials;
		std::string m_modelDirectoryTextures;

		// Misc
		float m_normalizedScale;
		unsigned int m_memoryUsage;		
		bool m_isAnimated;
		ResourceCache* m_resourceManager;
		std::shared_ptr<RHI_Device> m_rhiDevice;	
	};
}