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

//= INCLUDES ==================
#include <memory>
#include "../Core/SubSystem.h"
#include "ResourceCache.h"
#include "../Graphics/Mesh.h"
#include "../Core/GameObject.h"
//=============================

namespace Directus
{
		class ResourceManager : public Subsystem
		{
		public:
			ResourceManager(Context* context);
			~ResourceManager() { Unload(); }

			// Unloads all resources
			void Unload() { m_resourceCache->Unload(); }

			// Loads a resource and adds it to the resource cache
			template <class T>
			std::weak_ptr<T> Load(const std::string& filePath)
			{
				// Check if the resource is already loaded
				if (m_resourceCache->Cached(filePath))
					return GetResourceByPath<T>(filePath);

				std::shared_ptr<T> typedResource = std::make_shared<T>(g_context);
				std::shared_ptr<Resource> resource = ToResource(typedResource);

				if (resource->LoadFromFile(filePath))
					m_resourceCache->Add(resource);

				return GetResourceByPath<T>(filePath);
			}

			// Adds a typed and already loaded resource into the resource cache
			template <class T>
			std::weak_ptr<T> Add(std::shared_ptr<T> resourceIn)
			{
				// Add the resource only if it's not already there
				if (!m_resourceCache->Cached(resourceIn))
					m_resourceCache->Add(ToResource<T>(resourceIn));

				return resourceIn;
			}

			// Returns cached resource by ID
			template <class T>
			std::weak_ptr<T> GetResourceByID(const std::string& ID)
			{
				std::shared_ptr<Resource> resource = m_resourceCache->GetByID(ID);
				return ToDerived<T>(resource);
			}

			// Returns cached resource by Path
			template <class T>
			std::weak_ptr<T> GetResourceByPath(const std::string& filePath)
			{
				std::shared_ptr<Resource> resource = m_resourceCache->GetByPath(filePath);
				return ToDerived<T>(resource);
			}

			// Returns cached resource by Type
			template <class T>
			std::vector<std::weak_ptr<T>> GetAllByType()
			{
				std::vector<std::weak_ptr<T>> typedResources;
				for (const auto& resource : m_resourceCache->GetAll())
				{
					std::weak_ptr<T> typedResource = ToDerived<T>(resource);
					bool validCasting = !typedResource.expired();

					if (validCasting)
						typedResources.push_back(typedResource);
				}
				return typedResources;
			}

			//= TEMPORARY =======================================
			void NormalizeModelScale(GameObject* rootGameObject);
			//===================================================

		private:
			std::unique_ptr<ResourceCache> m_resourceCache;

			// Derived -> Resource (as a shared pointer)
			template <class T>
			std::shared_ptr<Resource> ToResource(std::shared_ptr<T> resource)
			{
				std::shared_ptr<Resource> sharedPtr = dynamic_pointer_cast<T>(resource);

				return sharedPtr;
			}

			// Resource -> Derived (as a weak pointer)
			template <class T>
			std::weak_ptr<T> ToDerived(std::shared_ptr<Resource> resource)
			{		
				std::shared_ptr<T> sharedPtr = dynamic_pointer_cast<T>(resource);
				std::weak_ptr<T> weakPtr = std::weak_ptr<T>(sharedPtr);

				return weakPtr;
			}

			//= TEMPORARY =================================================================================================
			std::vector<std::weak_ptr<Mesh>> GetModelMeshesByModelName(const std::string& rootGameObjectID);
			float GetNormalizedModelScaleByRootGameObjectID(const std::string& modelName);
			void SetModelScale(const std::string& rootGameObjectID, float scale);
			static std::weak_ptr<Mesh> GetLargestBoundingBox(const std::vector<std::weak_ptr<Mesh>>& meshes);
			//=============================================================================================================
		};
}
