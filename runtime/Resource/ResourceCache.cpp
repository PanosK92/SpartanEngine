/*
Copyright(c) 2016-2024 Panos Karabelas

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

//= INCLUDES =========================
#include "pch.h"
#include "ResourceCache.h"
#include "../World/World.h"
#include "../IO/FileStream.h"
#include "../RHI/RHI_Texture2D.h"
#include "../RHI/RHI_Texture2DArray.h"
#include "../RHI/RHI_TextureCube.h"
#include "../Audio/AudioClip.h"
#include "../Rendering/Mesh.h"
//====================================

//= NAMESPACES ================
using namespace std;
using namespace Spartan::Math;
//=============================

namespace Spartan
{
    namespace
    {
        array<string, 6> m_standard_resource_directories;
        string m_project_directory;
        vector<shared_ptr<IResource>> m_resources;
        mutex m_mutex;
        bool use_root_shader_directory = false;
    }

    void ResourceCache::Initialize()
    {
        // create project directory
        SetProjectDirectory("project\\");

        // add engine standard resource directories
        const string data_dir = "data\\";
        AddResourceDirectory(ResourceDirectory::Environment,    m_project_directory + "environment");
        AddResourceDirectory(ResourceDirectory::Fonts,          data_dir + "fonts");
        AddResourceDirectory(ResourceDirectory::Icons,          data_dir + "icons");
        AddResourceDirectory(ResourceDirectory::ShaderCompiler, data_dir + "shader_compiler");
        AddResourceDirectory(ResourceDirectory::Shaders,        data_dir + "shaders");
        AddResourceDirectory(ResourceDirectory::Textures,       data_dir + "textures");

        // subscribe to events
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldSaveStart, SP_EVENT_HANDLER_STATIC(Serialize));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldLoadStart, SP_EVENT_HANDLER_STATIC(Deserialize));
        SP_SUBSCRIBE_TO_EVENT(EventType::WorldClear,     SP_EVENT_HANDLER_STATIC(Shutdown));
    }

    bool ResourceCache::IsCached(const string& resource_file_path_native, const ResourceType resource_type)
    {
        SP_ASSERT(!resource_file_path_native.empty());

        lock_guard<mutex> guard(m_mutex);

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() != resource_type)
                continue;

            if (resource_file_path_native == resource->GetResourceFilePathNative())
                return true;
        }

        return false;
    }

    bool ResourceCache::IsCached(const uint64_t resource_id)
    {
        lock_guard<mutex> guard(m_mutex);

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource_id == resource->GetObjectId())
                return true;
        }

        return false;
    }
    
	shared_ptr<IResource>& ResourceCache::GetByName(const string& name, const ResourceType type)
    {
        lock_guard<mutex> guard(m_mutex);

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (name == resource->GetObjectName())
                return resource;
        }

        static shared_ptr<IResource> empty;
        return empty;
    }

    vector<shared_ptr<IResource>> ResourceCache::GetByType(const ResourceType type /*= ResourceType::Unknown*/)
    {
        lock_guard<mutex> guard(m_mutex);

        vector<shared_ptr<IResource>> resources;
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Max)
            {
                resources.emplace_back(resource);
            }
        }

        return resources;
    }

    uint64_t ResourceCache::GetMemoryUsage(ResourceType type /*= Resource_Unknown*/)
    {
        lock_guard<mutex> guard(m_mutex);

        uint64_t size = 0;
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() == type || type == ResourceType::Max)
            {
                if (SpartanObject* object = dynamic_cast<SpartanObject*>(resource.get()))
                {
                    size += object->GetObjectSize();
                }
            }
        }

        return size;
    }

    void ResourceCache::Serialize()
    {
        // create resource list file
        string file_path = GetProjectDirectoryAbsolute() + World::GetName() + ".resource";
        auto file = make_unique<FileStream>(file_path, FileStream_Write);
        if (!file->IsOpen())
        {
            SP_LOG_ERROR("Failed to open file.");
            return;
        }

        const uint32_t resource_count = GetResourceCount();

        // start progress report
        ProgressTracker::GetProgress(ProgressType::Resource).Start(resource_count, "Loading resources...");

        // save resource count
        file->Write(resource_count);

        // save all the currently used resources to disk
        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->HasFilePathNative())
            {
                SP_ASSERT_MSG(!resource->GetResourceFilePathNative().empty(), "Resources must have a native file path");
                SP_ASSERT_MSG(resource->GetResourceType() != ResourceType::Max, "Resources must have a type");

                file->Write(resource->GetResourceFilePathNative());              // file path
                file->Write(static_cast<uint32_t>(resource->GetResourceType())); // type
                resource->SaveToFile(resource->GetResourceFilePathNative());     // save
            }

            // update progress
            ProgressTracker::GetProgress(ProgressType::Resource).JobDone();
        }
    }

    void ResourceCache::Deserialize()
    {
        // open file
        string file_path = GetProjectDirectoryAbsolute() + World::GetName() + ".resource";
        unique_ptr<FileStream> file = make_unique<FileStream>(file_path, FileStream_Read);
        if (!file->IsOpen())
            return;

        // go through each resource and load it
        const uint32_t resource_count = file->ReadAs<uint32_t>();
        for (uint32_t i = 0; i < resource_count; i++)
        {
            string file_path = file->ReadAs<string>();
            const ResourceType type = static_cast<ResourceType>(file->ReadAs<uint32_t>());

            switch (type)
            {
            case ResourceType::Mesh:
                Load<Mesh>(file_path);
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
            case ResourceType::Texture2dArray:
                Load<RHI_Texture2DArray>(file_path);
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

    void ResourceCache::Shutdown()
    {
        uint32_t resource_count = static_cast<uint32_t>(m_resources.size());
        m_resources.clear();
        SP_LOG_INFO("%d resources have been cleared", resource_count);
    }

    uint32_t ResourceCache::GetResourceCount(const ResourceType type)
    {
        return static_cast<uint32_t>(GetByType(type).size());
    }

    void ResourceCache::AddResourceDirectory(const ResourceDirectory type, const string& directory)
    {
        m_standard_resource_directories[static_cast<uint32_t>(type)] = directory;
    }

    string ResourceCache::GetResourceDirectory(const ResourceDirectory resource_directory_type)
    {
        string directory = m_standard_resource_directories[static_cast<uint32_t>(resource_directory_type)];

        if (use_root_shader_directory)
        {
            if (resource_directory_type == ResourceDirectory::Shaders)
            {
                directory = "..\\" + directory;
            }
        }

        return directory;
    }

    void ResourceCache::SetProjectDirectory(const string& directory)
    {
        if (!FileSystem::Exists(directory))
        {
            FileSystem::CreateDirectory(directory);
        }

        m_project_directory = directory;
    }

    string ResourceCache::GetProjectDirectoryAbsolute()
    {
        return FileSystem::GetWorkingDirectory() + "/" + m_project_directory;
    }

    const string& ResourceCache::GetProjectDirectory()
    {
        return m_project_directory;
    }

    string ResourceCache::GetDataDirectory()
    {
        return "Data";
    }

    vector<shared_ptr<IResource>>& ResourceCache::GetResources()
    {
        return m_resources;
    }

    mutex& ResourceCache::GetMutex()
    {
        return m_mutex;
    }

    bool ResourceCache::GetUseRootShaderDirectory()
    {
        return use_root_shader_directory;
    }

    void ResourceCache::SetUseRootShaderDirectory(const bool _use_root_shader_directory)
    {
        use_root_shader_directory = _use_root_shader_directory;
    }
}
