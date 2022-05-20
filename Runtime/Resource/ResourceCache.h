/*
Copyright(c) 2016-2022 Panos Karabelas

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
#include <unordered_map>
#include "IResource.h"
#include "../Core/Subsystem.h"
#include "../Rendering/Model.h"
//=============================

namespace Spartan
{
    // Forward declarations
    class FontImporter;
    class ImageImporter;
    class ModelImporter;

    enum class ResourceDirectory
    {
        Cubemaps,
        Fonts,
        Icons,
        Scripts,
        ShaderCompiler,
        Shaders,
        Textures
    };

    class SPARTAN_CLASS ResourceCache : public Subsystem
    {
    public:
        ResourceCache(Context* context);
        ~ResourceCache();

        //= ISubsystem ==============
        bool OnInitialize() override;
        //===========================

        // Get by name
        std::shared_ptr<IResource>& GetByName(const std::string& name, ResourceType type);
        template <class T> 
        constexpr std::shared_ptr<T> GetByName(const std::string& name) 
        { 
            return std::static_pointer_cast<T>(GetByName(name, IResource::TypeToEnum<T>()));
        }

        // Get by type
        std::vector<std::shared_ptr<IResource>> GetByType(ResourceType type = ResourceType::Unknown);

        // Get by path
        template <class T>
        std::shared_ptr<T> GetByPath(const std::string& path)
        {
            for (std::shared_ptr<IResource>& resource : m_resources)
            {
                if (path == resource->GetResourceFilePathNative())
                    return std::static_pointer_cast<T>(resource);
            }

            return nullptr;
        }

        // Caches resource, or replaces with existing cached resource
        template <class T>
        [[nodiscard]] std::shared_ptr<T> Cache(const std::shared_ptr<T>& resource)
        {
            // Validate resource
            if (!resource)
                return nullptr;

            // Validate resource file path
            if (!resource->HasFilePathNative() && !FileSystem::IsDirectory(resource->GetResourceFilePathNative()))
            {
                LOG_ERROR("A resource must have a valid file path in order to be cached");
                return nullptr;
            }

            // Validate resource file path
            if (!FileSystem::IsEngineFile(resource->GetResourceFilePathNative()))
            {
                LOG_ERROR("A resource must have a native file format in order to be cached, provide format was %s", FileSystem::GetExtensionFromFilePath(resource->GetResourceFilePathNative()).c_str());
                return nullptr;
            }

            // Ensure that this resource is not already cached
            if (IsCached(resource->GetResourceName(), resource->GetResourceType()))
                return GetByName<T>(resource->GetResourceName());

            // Prevent threads from colliding in critical section
            std::lock_guard<std::mutex> guard(m_mutex);

            // In order to guarantee deserialization, we save it now
            resource->SaveToFile(resource->GetResourceFilePathNative());

            // Cache it
            return std::static_pointer_cast<T>(m_resources.emplace_back(resource));
        }

        // Loads a resource and adds it to the resource cache
        template <class T>
        std::shared_ptr<T> Load(const std::string& file_path)
        {
            if (!FileSystem::Exists(file_path))
            {
                LOG_ERROR("\"%s\" doesn't exist.", file_path.c_str());
                return nullptr;
            }

            // Check if the resource is already loaded
            const std::string name = FileSystem::GetFileNameWithoutExtensionFromFilePath(file_path);
            if (IsCached(name, IResource::TypeToEnum<T>()))
                return GetByName<T>(name);

            // Create new resource
            auto typed = std::make_shared<T>(m_context);

            // Set a default file path in case it's not overridden by LoadFromFile()
            typed->SetResourceFilePath(file_path);

            // Load
            if (!typed || !typed->LoadFromFile(file_path))
            {
                LOG_ERROR("Failed to load \"%s\".", file_path.c_str());
                return nullptr;
            }

            // Returned cached reference which is guaranteed to be around after deserialization
            return Cache<T>(typed);
        }

        template <class T>
        void Remove(std::shared_ptr<T>& resource)
        {
            if (!resource)
                return;

            if (!IsCached(resource->GetResourceName(), resource->GetResourceType()))
                return;

            m_resources.erase
            (
                std::remove_if
                (
                    m_resources.begin(),
                    m_resources.end(),
                    [](std::shared_ptr<IResource> resource) { return dynamic_cast<SpartanObject*>(resource.get())->GetObjectId() == resource->GetObjectId(); }
                ),
                m_resources.end()
            );
        }

        // Memory
        uint64_t GetMemoryUsageCpu(ResourceType type = ResourceType::Unknown);
        uint64_t GetMemoryUsageGpu(ResourceType type = ResourceType::Unknown);
        uint32_t GetResourceCount(ResourceType type = ResourceType::Unknown);
        void Clear();

        // Directories
        void AddResourceDirectory(ResourceDirectory type, const std::string& directory);
        std::string GetResourceDirectory(ResourceDirectory type);
        void SetProjectDirectory(const std::string& directory);
        std::string GetProjectDirectoryAbsolute() const;
        const auto& GetProjectDirectory()  const { return m_project_directory; }
        std::string GetResourceDirectory() const { return "Data"; }

        // Importers
        ModelImporter* GetModelImporter() const { return m_importer_model.get(); }
        ImageImporter* GetImageImporter() const { return m_importer_image.get(); }
        FontImporter* GetFontImporter()   const { return m_importer_font.get(); }

    private:
        bool IsCached(const uint64_t resource_id);
        bool IsCached(const std::string& resource_name, const ResourceType resource_type);

        // Event handlers
        void SaveResourcesToFiles();
        void LoadResourcesFromFiles();

        // Cache
        std::vector<std::shared_ptr<IResource>> m_resources;
        std::mutex m_mutex;

        // Directories
        std::unordered_map<ResourceDirectory, std::string> m_standard_resource_directories;
        std::string m_project_directory;

        // Importers
        std::shared_ptr<ModelImporter> m_importer_model;
        std::shared_ptr<ImageImporter> m_importer_image;
        std::shared_ptr<FontImporter> m_importer_font;
    };
}
