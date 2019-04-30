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

//= INCLUDES ====================
#include <memory>
#include <map>
#include "Import/ModelImporter.h"
#include "Import/ImageImporter.h"
#include "Import/FontImporter.h"
#include "../Core/ISubsystem.h"
#include "../Rendering/Model.h"
#include "../RHI/RHI_Texture.h"
//===============================

namespace Spartan
{
	#define VALIDATE_RESOURCE_TYPE(T) static_assert(std::is_base_of<IResource, T>::value, "Provided type does not implement IResource")

	enum Asset_Type
	{
		Asset_Cubemaps,
		Asset_Fonts,
		Asset_Icons,
		Asset_Scripts,
		Asset_ShaderCompiler,
		Asset_Shaders,
		Asset_Textures
	};

	class SPARTAN_CLASS ResourceCache : public ISubsystem
	{
	public:
		ResourceCache(Context* context);
		~ResourceCache();

		//= Subsystem =============
		bool Initialize() override;
		//=========================

		//= GET BY ==============================================================================
		// NAME
		std::shared_ptr<IResource>& GetByName(const std::string& name, Resource_Type type);
		template <class T> 
		constexpr std::shared_ptr<T> GetByName(const std::string& name) 
		{ 
			VALIDATE_RESOURCE_TYPE(T);
			return std::static_pointer_cast<T>(GetByName(name, IResource::TypeToEnum<T>()));
		}

		// TYPE
		std::vector<std::shared_ptr<IResource>> GetByType(Resource_Type type = Resource_Unknown);
		// PATH
		template <class T>
		std::shared_ptr<IResource>& GetByPath(const std::string& path)
		{
			VALIDATE_RESOURCE_TYPE(T);

			for (auto& resource : m_resource_groups[IResource::TypeToEnum<T>()])
			{
				if (path == resource->GetResourceFilePath())
					return resource;
			}

			return m_empty_resource;
		}
		//=======================================================================================
	
		//= LOADING/CACHING =============================================================================================
		// Caches resource, or replaces with existing cached resource
		template <class T>
		void Cache(std::shared_ptr<T>& resource)
		{
			VALIDATE_RESOURCE_TYPE(T);

			if (!resource)
				return;

			// If the resource is already loaded, replace it with the existing one, then early exit
			if (IsCached(resource->GetResourceName(), resource->GetResourceType()))
			{
				resource = GetByName<T>(FileSystem::GetFileNameNoExtensionFromFilePath(resource->GetResourceFilePath()));
				return;
			}

			// Cache the resource
			std::lock_guard<mutex> guard(m_mutex);
			m_resource_groups[resource->GetResourceType()].emplace_back(resource);
		}
		bool IsCached(const std::string& resource_name, Resource_Type resource_type);

		// Loads a resource and adds it to the resource cache
		template <class T>
		std::shared_ptr<T> Load(const std::string& file_path)
		{
			VALIDATE_RESOURCE_TYPE(T);

			if (!FileSystem::FileExists(file_path))
			{
				LOGF_ERROR("Path \"%s\" is invalid.", file_path.c_str());
				return false;
			}

			// Try to make the path relative to the engine (in case it isn't)
			auto file_path_relative	= FileSystem::GetRelativeFilePath(file_path);
			auto name				= FileSystem::GetFileNameNoExtensionFromFilePath(file_path_relative);

			// Check if the resource is already loaded
			if (IsCached(name, IResource::TypeToEnum<T>()))
			{
				return GetByName<T>(name);
			}

			// Create new resource
			auto typed = std::make_shared<T>(m_context);
			// Set a default name and a default filepath in case it's not overridden by LoadFromFile()
			typed->SetResourceName(name);
			typed->SetResourceFilePath(file_path_relative);

			// Cache it now so LoadFromFile() can safely pass around a reference to the resource from the ResourceManager
			Cache<T>(typed);

			// Load
			if (!typed->LoadFromFile(file_path_relative))
			{
				LOGF_ERROR("Failed to load \"%s\".", file_path_relative.c_str());
				return nullptr;
			}

			// Cache it and cast it
			return typed;
		}
		//===============================================================================================================

		//= I/O ======================
		void SaveResourcesToFiles();
		void LoadResourcesFromFiles();
		//============================

		//= MISC ============================================================
		// Memory
		unsigned int GetMemoryUsage(Resource_Type type = Resource_Unknown);
		// Unloads all resources
		void Clear() { m_resource_groups.clear(); }
		// Returns all resources of a given type
		unsigned int GetResourceCount(Resource_Type type = Resource_Unknown);
		//===================================================================

		//= DIRECTORIES ===============================================================
		void AddDataDirectory(Asset_Type type, const std::string& directory);
		const std::string& GetDataDirectory(Asset_Type type);
		void SetProjectDirectory(const std::string& directory);
		std::string GetProjectDirectoryAbsolute() const;
		const std::string& GetProjectDirectory() const	{ return m_project_directory; }
		std::string GetDataDirectory() const			{ return "Data//"; }
		//=============================================================================

		// Importers
		ModelImporter* GetModelImporter() const { return m_importer_model.get(); }
		ImageImporter* GetImageImporter() const { return m_importer_image.get(); }
		FontImporter* GetFontImporter() const	{ return m_importer_font.get(); }

	private:
		// Cache
		std::map<Resource_Type, std::vector<std::shared_ptr<IResource>>> m_resource_groups;
		std::mutex m_mutex;

		// Directories
		std::map<Asset_Type, std::string> m_standard_resource_directories;
		std::string m_project_directory;

		// Importers
		std::shared_ptr<ModelImporter> m_importer_model;
		std::shared_ptr<ImageImporter> m_importer_image;
		std::shared_ptr<FontImporter> m_importer_font;

		std::shared_ptr<IResource> m_empty_resource = nullptr;
	};
}