/*
Copyright(c) 2016-2021 Panos Karabelas

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

//= INCLUDES ======================
#include "Spartan.h"
#include "ResourceCache.h"
#include "ProgressTracker.h"
#include "Import/ImageImporter.h"
#include "Import/ModelImporter.h"
#include "Import/FontImporter.h"
#include "../World/World.h"
#include "../World/Entity.h"
#include "../IO/FileStream.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_TextureCube.h"
#include "../Audio/AudioClip.h"
#include "../Rendering/Model.h"
//=================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    ResourceCache::ResourceCache(Context* context) : ISubsystem(context)
    {
        const string data_dir = "Data\\";

        // Add engine standard resource directories
        AddResourceDirectory(ResourceDirectory::Cubemaps,        data_dir + "environment");
        AddResourceDirectory(ResourceDirectory::Fonts,           data_dir + "fonts");
        AddResourceDirectory(ResourceDirectory::Icons,           data_dir + "icons");
        AddResourceDirectory(ResourceDirectory::Scripts,         data_dir + "scripts");
        AddResourceDirectory(ResourceDirectory::ShaderCompiler,  data_dir + "shader_compiler");
        AddResourceDirectory(ResourceDirectory::Shaders,         data_dir + "shaders");
        AddResourceDirectory(ResourceDirectory::Textures,        data_dir + "textures");

        // Create project directory
        SetProjectDirectory("Project/");

        // Subscribe to events
        SUBSCRIBE_TO_EVENT(EventType::WorldSave,    EVENT_HANDLER(SaveResourcesToFiles));
        SUBSCRIBE_TO_EVENT(EventType::WorldLoad,    EVENT_HANDLER(LoadResourcesFromFiles));
    }

    ResourceCache::~ResourceCache()
    {
        // Unsubscribe from events
        UNSUBSCRIBE_FROM_EVENT(EventType::WorldSave,    EVENT_HANDLER(SaveResourcesToFiles));
        UNSUBSCRIBE_FROM_EVENT(EventType::WorldLoad,    EVENT_HANDLER(LoadResourcesFromFiles));
    }

    bool ResourceCache::OnInitialise()
    {
        // Importers
        m_importer_image    = make_shared<ImageImporter>(m_context);
        m_importer_model    = make_shared<ModelImporter>(m_context);
        m_importer_font     = make_shared<FontImporter>(m_context);

        return true;
    }

    bool ResourceCache::IsCached(const string& resource_name, const ResourceType resource_type /*= Resource_Unknown*/)
    {
        if (resource_name.empty())
        {
            LOG_ERROR_INVALID_PARAMETER();
            return false;
        }

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource_name == resource->GetResourceName())
                return true;
        }

        return false;
    }

    shared_ptr<IResource>& ResourceCache::GetByName(const string& name, const ResourceType type)
    {
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (name == resource->GetResourceName())
                return resource;
        }

        static shared_ptr<IResource> empty;
        return empty;
    }

    vector<shared_ptr<IResource>> ResourceCache::GetByType(const ResourceType type /*= ResourceType::Unknown*/)
    {
        vector<shared_ptr<IResource>> resources;

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Unknown)
            {
                resources.emplace_back(resource);
            }
        }

        return resources;
    }

    uint64_t ResourceCache::GetMemoryUsageCpu(ResourceType type /*= Resource_Unknown*/)
    {
        uint64_t size = 0;

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Unknown)
            {
                if (SpartanObject* object = dynamic_cast<SpartanObject*>(resource.get()))
                {
                    size += object->GetSizeCpu();
                }
            }
        }

        return size;
    }

    uint64_t ResourceCache::GetMemoryUsageGpu(ResourceType type /*= Resource_Unknown*/)
    {
        uint64_t size = 0;

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Unknown)
            {
                if (SpartanObject* object = dynamic_cast<SpartanObject*>(resource.get()))
                {
                    size += object->GetSizeGpu();
                }
            }
        }

        return size;
    }

    void ResourceCache::SaveResourcesToFiles()
    {
        // Start progress report
        ProgressTracker::Get().Reset(ProgressType::ResourceCache);
        ProgressTracker::Get().SetIsLoading(ProgressType::ResourceCache, true);
        ProgressTracker::Get().SetStatus(ProgressType::ResourceCache, "Loading resources...");

        // Create resource list file
        string file_path = GetProjectDirectoryAbsolute() + m_context->GetSubsystem<World>()->GetName() + "_resources.dat";
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
        {
            LOG_ERROR_GENERIC_FAILURE();
            return;
        }

        const auto resource_count = GetResourceCount();
        ProgressTracker::Get().SetJobCount(ProgressType::ResourceCache, resource_count);

        // Save resource count
        file->Write(resource_count);

        // Save all the currently used resources to disk
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (!resource->HasFilePathNative())
                continue;

            // Save file path
            file->Write(resource->GetResourceFilePathNative());
            // Save type
            file->Write(static_cast<uint32_t>(resource->GetResourceType()));
            // Save resource (to a dedicated file)
            resource->SaveToFile(resource->GetResourceFilePathNative());

            // Update progress
            ProgressTracker::Get().IncrementJobsDone(ProgressType::ResourceCache);
        }

        // Finish with progress report
        ProgressTracker::Get().SetIsLoading(ProgressType::ResourceCache, false);
    }

    void ResourceCache::LoadResourcesFromFiles()
    {
        // Open resource list file
        auto file_path = GetProjectDirectoryAbsolute() + m_context->GetSubsystem<World>()->GetName() + "_resources.dat";
        auto file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
            return;

        // Load resource count
        const auto resource_count = file->ReadAs<uint32_t>();

        for (uint32_t i = 0; i < resource_count; i++)
        {
            // Load resource file path
            auto file_path = file->ReadAs<string>();

            // Load resource type
            const auto type = static_cast<ResourceType>(file->ReadAs<uint32_t>());

            switch (type)
            {
            case ResourceType::Model:
                Load<Model>(file_path);
                break;
            case ResourceType::Material:
                Load<Material>(file_path);
                break;
            case ResourceType::Texture:
                Load<RHI_Texture>(file_path);
                break;
            case ResourceType::Texture2d:
                Load<RHI_Texture2D>(file_path);
                break;
            case ResourceType::TextureCube:
                Load<RHI_TextureCube>(file_path);
                break;
            case ResourceType::Audio:
                Load<AudioClip>(file_path);
                break;
            }
        }

    }

    void ResourceCache::Clear()
    {
        uint32_t resource_count = static_cast<uint32_t>(m_resources.size());

        m_resources.clear();

        LOG_INFO("%d resources have been cleared", resource_count);
    }

    uint32_t ResourceCache::GetResourceCount(const ResourceType type)
    {
        return static_cast<uint32_t>(GetByType(type).size());
    }

    void ResourceCache::AddResourceDirectory(const ResourceDirectory type, const string& directory)
    {
        m_standard_resource_directories[type] = directory;
    }

    string ResourceCache::GetResourceDirectory(const ResourceDirectory type)
    {
        for (auto& directory : m_standard_resource_directories)
        {
            if (directory.first == type)
                return directory.second;
        }

        return "";
    }

    void ResourceCache::SetProjectDirectory(const string& directory)
    {
        if (!FileSystem::Exists(directory))
        {
            FileSystem::CreateDirectory_(directory);
        }

        m_project_directory = directory;
    }

    string ResourceCache::GetProjectDirectoryAbsolute() const
    {
        return FileSystem::GetWorkingDirectory() + "/" + m_project_directory;
    }
}
