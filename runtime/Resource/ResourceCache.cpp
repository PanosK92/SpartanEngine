/*
Copyright(c) 2016-2025 Panos Karabelas

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

//= INCLUDES =======================
#include "pch.h"
#include "ResourceCache.h"
#include "../World/World.h"
#include "../IO/FileStream.h"
#include "../RHI/RHI_Texture.h"
#include "../Audio/AudioClip.h"
#include "../Rendering/Mesh.h"
#include "../Core/ProgressTracker.h"
//==================================

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

    bool ResourceCache::IsCached(const string& file_path, const ResourceType resource_type)
    {
        SP_ASSERT(!file_path.empty());

        lock_guard<mutex> guard(m_mutex);

        for (shared_ptr<IResource>& resource : m_resources)
        {
            if (resource->GetResourceType() != resource_type)
                continue;

            if (file_path == resource->GetResourceFilePath())
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
        // todo: since we won't be using custom file formats, we just need to save the resource paths, simple and reliable
    }

    void ResourceCache::Deserialize()
    {
        // todo: we just need to load the resource paths, simple and reliable
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
