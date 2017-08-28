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
#include <map>
#include "../Core/SubSystem.h"
#include "ResourceCache.h"
#include "../Graphics/Mesh.h"
#include "../Core/GameObject.h"
#include "Import/ModelImporter.h"
#include "Import/ImageImporter.h"
//===============================

namespace Directus
{
	class DLL_API ResourceManager : public Subsystem
	{
	public:
		ResourceManager(Context* context);
		~ResourceManager() { Unload(); }

		//= Subsystem ============
		virtual bool Initialize();
		//========================

		// Unloads all resources
		void Unload() { m_resourceCache->Unload(); }

		// Loads a resource and adds it to the resource cache
		template <class T>
		std::weak_ptr<T> Load(const std::string& filePath)
		{
			// Check if the resource is already loaded
			if (m_resourceCache->CachedByFilePath(filePath))
				return GetResourceByPath<T>(filePath);

			std::shared_ptr<T> typed = std::make_shared<T>(m_context);
			std::shared_ptr<Resource> base = ToBaseShared(typed);

			if (base->LoadFromFile(filePath))
			{
				m_resourceCache->Add(base);
			}
			else
			{
				LOG_WARNING("Resource \"" + filePath + "\" failed to load");
			}

			// Returne typed weak
			return GetResourceByPath<T>(filePath);
		}

		// Adds a resource into the resource cache
		template <class T>
		std::weak_ptr<T> Add(std::shared_ptr<T> resource)
		{
			if (!resource)
				return std::weak_ptr<T>();

			std::shared_ptr<Resource> base = ToBaseShared(resource);

			// If the resource is already loaded, return the existing one
			if (m_resourceCache->CachedByName(base))
				return ToTypedWeak<T>(m_resourceCache->GetByName(base->GetResourceName()));

			// Else, add the resource and return it
			m_resourceCache->Add(base);
			return resource;
		}

		// Returns cached resource by ID
		template <class T>
		std::weak_ptr<T> GetResourceByID(const std::string& ID)
		{
			return ToTypedWeak<T>(m_resourceCache->GetByID(ID));
		}

		// Returns cached resource by Path
		template <class T>
		std::weak_ptr<T> GetResourceByPath(const std::string& filePath)
		{
			return ToTypedWeak<T>(m_resourceCache->GetByPath(filePath));
		}

		// Returns cached resource by Type
		template <class T>
		std::vector<std::weak_ptr<T>> GetResourcesByType()
		{
			std::vector<std::weak_ptr<T>> typedVec;
			for (const auto& resource : m_resourceCache->GetAll())
			{
				std::weak_ptr<T> typed = ToTypedWeak<T>(resource);
				bool validCasting = !typed.expired();

				if (validCasting)
				{
					typedVec.push_back(typed);
				}
			}
			return typedVec;
		}

		void SaveResourceMetadata() { m_resourceCache->SaveResourceMetadata(); }

		std::vector<std::string> GetResourceFilePaths() { return m_resourceCache->GetResourceFilePaths(); }

		void AddResourceDirectory(ResourceType type, const std::string& directory);
		std::string GetResourceDirectory(ResourceType type);

		// Importers
		std::weak_ptr<ModelImporter> GetModelImporter() { return m_modelImporter; }
		std::weak_ptr<ImageImporter> GetImageImporter() { return m_imageImporter; }

	private:
		std::unique_ptr<ResourceCache> m_resourceCache;
		std::map<ResourceType, std::string> m_resourceDirectories;

		// Importers
		std::shared_ptr<ModelImporter> m_modelImporter;
		std::shared_ptr<ImageImporter> m_imageImporter;

		// Type -> Resource (as a shared pointer)
		template <class Type>
		std::shared_ptr<Resource> ToBaseShared(std::shared_ptr<Type> resource)
		{
			std::shared_ptr<Resource> sharedPtr = dynamic_pointer_cast<Type>(resource);

			return sharedPtr;
		}

		// Resource -> Type (as a weak pointer)
		template <class Type>
		std::weak_ptr<Type> ToTypedWeak(std::shared_ptr<Resource> resource)
		{
			std::shared_ptr<Type> sharedPtr = dynamic_pointer_cast<Type>(resource);
			std::weak_ptr<Type> weakPtr = std::weak_ptr<Type>(sharedPtr);

			return weakPtr;
		}
	};
}