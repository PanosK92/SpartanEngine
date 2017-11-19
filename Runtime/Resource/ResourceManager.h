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
#include "ResourceCache.h"
#include "../Core/SubSystem.h"
#include "../Core/GameObject.h"
#include "Import/ModelImporter.h"
#include "Import/ImageImporter.h"
#include "Import/FontImporter.h"
//===============================

namespace Directus
{
	class DLL_API ResourceManager : public Subsystem
	{
	public:
		ResourceManager(Context* context);
		~ResourceManager() { Clear(); }

		//= Subsystem ============
		virtual bool Initialize();
		//========================

		// Unloads all resources
		void Clear() { m_resourceCache->Unload(); }

		// Loads a resource and adds it to the resource cache
		template <class T>
		std::weak_ptr<T> Load(const std::string& filePath)
		{
			// Try to make the path relative to the engine (in case it isn't)
			std::string relativeFilePath = FileSystem::GetRelativeFilePath(filePath);

			// Check if the resource is already loaded
			auto cached = GetResourceByPath<T>(relativeFilePath);
			if (!cached.expired())
				return cached;

			// Create new resource
			std::shared_ptr<T> typed = std::make_shared<T>(m_context);

			// Assign filepath and name
			typed->SetResourceFilePath(relativeFilePath);
			typed->SetResourceName(FileSystem::GetFileNameFromFilePath(relativeFilePath));

			// Load 
			if (!typed->LoadFromFile(relativeFilePath))
			{
				LOG_WARNING("ResourceManager: Resource \"" + relativeFilePath + "\" failed to load");
				return cached;
			}

			return Add(typed);
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
				return ToDerivedWeak<T>(m_resourceCache->GetByName(base->GetResourceName()));

			// Else, add the resource and return it
			m_resourceCache->Add(base);
			return resource;
		}

		// Returns cached resource by ID
		template <class T>
		std::weak_ptr<T> GetResourceByID(const std::size_t ID)
		{
			return ToDerivedWeak<T>(m_resourceCache->GetByID(ID));
		}

		// Returns cached resource by Path
		template <class T>
		std::weak_ptr<T> GetResourceByPath(const std::string& filePath)
		{
			return ToDerivedWeak<T>(m_resourceCache->GetByPath(filePath));
		}

		// Returns cached resource by Type
		template <class T>
		std::vector<std::weak_ptr<T>> GetResourcesByType()
		{
			std::vector<std::weak_ptr<T>> typedVec;
			for (const auto& resource : m_resourceCache->GetAll())
			{
				std::weak_ptr<T> typed = ToDerivedWeak<T>(resource);
				bool validCasting = !typed.expired();

				if (validCasting)
				{
					typedVec.push_back(typed);
				}
			}
			return typedVec;
		}

		// Returns cached resource count by Type
		template <class T>
		int GetResourceCountByType()
		{
			int count = 0;
			for (const auto& resource : m_resourceCache->GetAll())
			{
				std::weak_ptr<T> typed = ToDerivedWeak<T>(resource);
				count = typed.expired() ? count : count + 1;
			}
			return count;
		}

		void SaveResourceMetadata() { m_resourceCache->SaveResourceMetadata(); }

		std::vector<std::string> GetResourceFilePaths() { return m_resourceCache->GetResourceFilePaths(); }

		void AddStandardResourceDirectory(ResourceType type, const std::string& directory);
		std::string GetStandardResourceDirectory(ResourceType type);
		void SetProjectDirectory(const std::string& directory);
		std::string GetProjectDirectory() { return m_projectDirectory; }

		// Importers
		std::weak_ptr<ModelImporter> GetModelImporter() { return m_modelImporter; }
		std::weak_ptr<ImageImporter> GetImageImporter() { return m_imageImporter; }
		std::weak_ptr<FontImporter> GetFontImporter() { return m_fontImporter; }

	private:
		std::unique_ptr<ResourceCache> m_resourceCache;
		std::map<ResourceType, std::string> m_standardResourceDirectories;
		std::string m_projectDirectory;

		// Importers
		std::shared_ptr<ModelImporter> m_modelImporter;
		std::shared_ptr<ImageImporter> m_imageImporter;
		std::shared_ptr<FontImporter> m_fontImporter;

		// Derived -> Base (as a shared pointer)
		template <class Type>
		static std::shared_ptr<Resource> ToBaseShared(std::shared_ptr<Type> derived)
		{
			std::shared_ptr<Resource> base = dynamic_pointer_cast<Resource>(derived);

			return base;
		}

		// Base -> Derived (as a weak pointer)
		template <class Type>
		static std::weak_ptr<Type> ToDerivedWeak(std::shared_ptr<Resource> base)
		{
			std::shared_ptr<Type> derivedShared = dynamic_pointer_cast<Type>(base);
			std::weak_ptr<Type> derivedWeak = std::weak_ptr<Type>(derivedShared);

			return derivedWeak;
		}
	};

	// Dummy template decleration to prevent 
	// errors when compiling the editor.
	template<int>
	void dynamic_pointer_cast();
}