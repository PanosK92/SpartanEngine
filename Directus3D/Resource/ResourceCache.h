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

//= INCLUDES ===================================
#include <vector>
#include <memory>
#include "IResource.h"
#include "../Core/Subsystem.h"
#include "../Graphics/Shaders/ShaderVariation.h"
#include "../Graphics/Mesh.h"
//==============================================

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
			~ResourceCache() { Clear(); }

			void Initialize();

			// Releases all the resources
			void Clear();

			// Adds a resource to the pool
			template <class T>
			std::weak_ptr<T> AddResource(std::shared_ptr<T> resourceIn)
			{
				if (!resourceIn)
					return std::weak_ptr<T>();

				// Check if the resource already exists, if so, return the existing one
				for (const auto& resource : m_resources)
					if (resource->GetID() == resourceIn->GetID())
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				// Add the resource and return it
				m_resources.push_back(resourceIn);
				return std::weak_ptr<T>(dynamic_pointer_cast<T>(m_resources.back()));
			}

			// Loads a resource given the file path
			template <class T>
			std::weak_ptr<T> LoadResource(const std::string& filePath)
			{
				// Check if the resource is already loaded, if so, return it.
				for (const auto& resource : m_resources)
					if (resource->GetFilePath() == filePath)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				std::shared_ptr<T> typedResource = std::make_shared<T>(g_context);
				std::shared_ptr<IResource> resource = std::shared_ptr<IResource>(dynamic_pointer_cast<T>(typedResource));

				if (resource->LoadFromFile(filePath))
					m_resources.push_back(resource);

				return typedResource;
			}

			// Returns the file paths of all the resources
			std::vector<std::string> GetResourceFilePaths()
			{
				std::vector<std::string> filePaths;
				for (const auto& resource : m_resources)
					if (resource->GetFilePath() != DATA_NOT_ASSIGNED)
						filePaths.push_back(resource->GetFilePath());

				return filePaths;
			}

			// Returns a resource by ID
			template <class T>
			std::weak_ptr<T> GetResourceByID(const std::string& ID)
			{
				for (const auto& resource : m_resources)
					if (resource->GetID() == ID)
						return std::weak_ptr<T>(dynamic_pointer_cast<T>(resource));

				return std::weak_ptr<T>();
			}

			// Returns a resource by file path
			template <class T>
			std::weak_ptr<T> GetResourceByPath(const std::string& filePath)
			{
				for (const auto& resource : m_resources)
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
			void SaveResourceMetadata()
			{
				for (const auto& resource : m_resources)
					resource->SaveMetadata();
			}

			//= TEMPORARY =======================================
			void NormalizeModelScale(GameObject* rootGameObject);
			//===================================================

		private:
			std::vector<std::shared_ptr<IResource>> m_resources;

			//= TEMPORARY =================================================================================================
			std::vector<std::weak_ptr<Mesh>> GetModelMeshesByModelName(const std::string& rootGameObjectID);
			float GetNormalizedModelScaleByRootGameObjectID(const std::string& modelName);
			void SetModelScale(const std::string& rootGameObjectID, float scale);
			static std::weak_ptr<Mesh> GetLargestBoundingBox(const std::vector<std::weak_ptr<Mesh>>& meshes);
			//=============================================================================================================
		};
	}
}