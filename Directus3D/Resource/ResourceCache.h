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

//= INCLUDES ========================
#include <vector>
#include <memory>
#include "IResource.h"
#include "../Core/Subsystem.h"
#include "../FileSystem/FileSystem.h"
#include "../Graphics/Texture.h"
#include "../Core/Context.h"
#include "../Graphics/Mesh.h"
#include "../Graphics/Material.h"
#include "../Logging/Log.h"
//===================================

//= TEMPORARY =================================================
#define MATERIAL_DEFAULT_ID "MATERIAL_DEFAULT_ID"
#define MATERIAL_DEFAULT_SKYBOX_ID "MATERIAL_DEFAULT_SKYBOX_ID"
//=============================================================

namespace Directus
{
	namespace Resource
	{
		// dynamic_pointer_cast complains
		// so I made it shut it up like this.
		template<int>
		void dynamic_pointer_cast();

		class ResourceCache : public Subsystem
		{
		public:
			ResourceCache(Context* context);
			~ResourceCache();

			void Initialize();

			// Releases all the resources
			void Clear();

			// Adds a resource to the pool
			template <class T>
			std::weak_ptr<T> AddResource(std::shared_ptr<T> resourceIn)
			{
				// Check if the resource already exists
				for (auto const& resource : m_resources)
					if (resource->GetID() == resourceIn->GetID())
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				m_resources.push_back(resourceIn);
				return std::weak_ptr<T>(dynamic_pointer_cast<T>(m_resources.back()));
			}

			// Loads a resource given the file path
			template <class T>
			std::weak_ptr<T> LoadResource(const std::string& filePath)
			{
				// Check if the resource is already loaded
				for (auto const& resource : m_resources)
					if (resource->GetFilePath() == filePath)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				// Texture
				if (FileSystem::IsSupportedImageFile(filePath))
				{
					auto texture = std::make_shared<Texture>();
					texture->LoadFromFile(filePath, g_context->GetSubsystem<Graphics>());
					m_resources.push_back(texture);
					return std::weak_ptr<T>(dynamic_pointer_cast<T>(m_resources.back()));
				}
				// Material
				if (FileSystem::IsMaterialFile(filePath))
				{
					auto material = std::make_shared<Material>(g_context);
					material->LoadFromFile(filePath);
					material->LoadUnloadedTextures();
					m_resources.push_back(material);
					return std::weak_ptr<T>(dynamic_pointer_cast<T>(m_resources.back()));
				}
				// Mesh
				if (FileSystem::IsSupportedMeshFile(filePath))
				{
					auto mesh = std::make_shared<Mesh>();
					mesh->LoadFromFile(filePath);
					m_resources.push_back(mesh);
					return std::weak_ptr<T>(dynamic_pointer_cast<T>(m_resources.back()));
				}
				
				return std::weak_ptr<T>();
			}

			// Loads resources given the file paths from a vector;
			void LoadResources(const std::vector<std::string>& filePaths);

			// Returns the file paths of all the resources
			std::vector<std::string> GetResourceFilePaths()
			{
				std::vector<std::string> filePaths;
				for (auto const& resource : m_resources)
					filePaths.push_back(resource->GetFilePath());

				return filePaths;
			}

			// Returns a resource by ID
			template <class T>
			std::weak_ptr<T> GetResourceByID(const std::string& ID)
			{
				for (auto const& resource : m_resources)
				{
					//= TEMPORARY CANCEROUS CHECKS ======
					if (ID == MATERIAL_DEFAULT_ID)
						std::weak_ptr<T>(dynamic_pointer_cast<T>(m_materialDefault));

					if (ID == MATERIAL_DEFAULT_SKYBOX_ID)
						std::weak_ptr<T>(dynamic_pointer_cast<T>(m_materialDefaultSkybox));
					//===================================

					if (resource->GetID() == ID)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));
				}

				return std::weak_ptr<T>();
			}

			// Returns a resource by file path
			template <class T>
			std::weak_ptr<T> GetResourceByPath(const std::string& filePath)
			{
				for (auto const& resource : m_resources)
					if (resource->GetFilePath() == filePath)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				return std::weak_ptr<T>();
			}

			// Returns a vector of a specific type of resources
			template <class T>
			std::vector<std::weak_ptr<T>> GetResourcesOfType()
			{
				std::vector<std::weak_ptr<T>> typedResources;
				for (const auto& resource : m_resources)
				{
					auto typedResource = std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

					if (!typedResource.expired())
						typedResources.push_back(typedResource);
				}
				return typedResources;
			}

			// Makes the resources save their metadata
			void SaveResourceMetadata();

			//= TEMPORARY ===================================================
			std::weak_ptr<ShaderVariation> CreateShaderBasedOnMaterial(
				bool albedo,
				bool roughness,
				bool metallic,
				bool normal,
				bool height,
				bool occlusion,
				bool emission,
				bool mask,
				bool cubemap
			);
			std::weak_ptr<ShaderVariation> FindMatchingShader(
				bool albedo,
				bool roughness,
				bool metallic,
				bool normal,
				bool height,
				bool occlusion,
				bool emission,
				bool mask,
				bool cubemap
			);
			std::weak_ptr<Mesh> GetDefaultCube();
			std::weak_ptr<Mesh> GetDefaultQuad();
			std::weak_ptr<Material> GetMaterialStandardDefault();
			std::weak_ptr<Material> GetMaterialStandardSkybox();
			void NormalizeModelScale(GameObject* rootGameObject);
			//======================================================

		private:
			std::vector<std::shared_ptr<IResource>> m_resources;

			//= TEMPORARY =================================================================================================
			void GenerateDefaultMeshes();
			void CreateCube(std::vector<VertexPositionTextureNormalTangent>& vertices, std::vector<unsigned int>& indices);
			void CreateQuad(std::vector<VertexPositionTextureNormalTangent>& vertices, std::vector<unsigned int>& indices);
			std::shared_ptr<Mesh> m_defaultCube;
			std::shared_ptr<Mesh> m_defaultQuad;

			//= MESH PROCESSING =============================================================================
			std::vector<std::weak_ptr<Mesh>> GetModelMeshesByModelName(const std::string& rootGameObjectID);
			float GetNormalizedModelScaleByRootGameObjectID(const std::string& modelName);
			void SetModelScale(const std::string& rootGameObjectID, float scale);
			static std::weak_ptr<Mesh> GetLargestBoundingBox(const std::vector<std::weak_ptr<Mesh>>& meshes);
			//===============================================================================================

			//= MATERIAL POOL ===============================================================================
			void GenerateDefaultMaterials();
			std::vector<std::shared_ptr<Material>> m_materials;
			std::shared_ptr<Material> m_materialDefault;
			std::shared_ptr<Material> m_materialDefaultSkybox;
			//===============================================================================================
			//=============================================================================================================
		};
	}
}