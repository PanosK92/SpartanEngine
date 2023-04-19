/*
Copyright(c) 2016-2023 Panos Karabelas

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
#include "IconLoader.h"
#include "Resource/ResourceCache.h"
#include "RHI/RHI_Texture2D.h"
#include "Core/ThreadPool.h"
#include "Event.h"
//=================================

//= NAMESPACES =========
using namespace std;
using namespace Spartan;
//======================

namespace
{
    static vector<Icon> icons;
    static Icon no_icon;
    static std::mutex icon_mutex;
}

static void destroy_rhi_resources()
{
    icons.clear();
}

static const Icon& get_icon_by_type(IconType type)
{
    for (Icon& icon : icons)
    {
        if (icon.GetType() == type)
            return icon;
    }

    return no_icon;
}

Icon::Icon(IconType type, const std::string& file_path)
{
    this->m_type = type;

    // Create texture
    string name     = FileSystem::GetFileNameFromFilePath(file_path);
    this->m_texture = make_shared<RHI_Texture2D>(RHI_Texture_Srv, name.c_str());

    // Load texture
    ThreadPool::AddTask([this, file_path]()
    {
        m_texture->LoadFromFile(file_path);
    });
}

RHI_Texture* Icon::GetTexture() const
{
    if (m_texture && m_texture->IsReadyForUse())
    {
        return m_texture.get();
    }

    return nullptr;
}

void Icon::SetTexture(shared_ptr<RHI_Texture> texture)
{
    m_texture = texture;
}

string Icon::GetFilePath() const
{
    return m_texture->GetResourceFilePath();
}

void IconLoader::Initialize()
{
    // Load all standard editor icons
    ThreadPool::AddTask([]()
    {
        const string data_dir = ResourceCache::GetDataDirectory() + "\\";

        LoadFromFile(data_dir + "Icons\\component_componentOptions.png",       IconType::Component_Options);
        LoadFromFile(data_dir + "Icons\\component_audioListener.png",          IconType::Component_AudioListener);
        LoadFromFile(data_dir + "Icons\\component_audioSource.png",            IconType::Component_AudioSource);
        LoadFromFile(data_dir + "Icons\\component_reflectionProbe.png",        IconType::Component_ReflectionProbe);
        LoadFromFile(data_dir + "Icons\\component_camera.png",                 IconType::Component_Camera); 
        LoadFromFile(data_dir + "Icons\\component_collider.png",               IconType::Component_Collider);
        LoadFromFile(data_dir + "Icons\\component_light.png",                  IconType::Component_Light);
        LoadFromFile(data_dir + "Icons\\component_material.png",               IconType::Component_Material);
        LoadFromFile(data_dir + "Icons\\component_material_removeTexture.png", IconType::Component_Material_RemoveTexture);
        LoadFromFile(data_dir + "Icons\\component_meshCollider.png",           IconType::Component_MeshCollider);
        LoadFromFile(data_dir + "Icons\\component_renderable.png",             IconType::Component_Renderable);
        LoadFromFile(data_dir + "Icons\\component_rigidBody.png",              IconType::Component_RigidBody);
        LoadFromFile(data_dir + "Icons\\component_softBody.png",               IconType::Component_SoftBody);
        LoadFromFile(data_dir + "Icons\\component_script.png",                 IconType::Component_Script);
        LoadFromFile(data_dir + "Icons\\component_transform.png",              IconType::Component_Transform);
        LoadFromFile(data_dir + "Icons\\component_terrain.png",                IconType::Component_Terrain);
        LoadFromFile(data_dir + "Icons\\component_environment.png",            IconType::Component_Environment);
        LoadFromFile(data_dir + "Icons\\console_info.png",                     IconType::Console_Info);
        LoadFromFile(data_dir + "Icons\\console_warning.png",                  IconType::Console_Warning);
        LoadFromFile(data_dir + "Icons\\console_error.png",                    IconType::Console_Error);
        LoadFromFile(data_dir + "Icons\\button_play.png",                      IconType::Button_Play);
        LoadFromFile(data_dir + "Icons\\profiler.png",                         IconType::Button_Profiler);
        LoadFromFile(data_dir + "Icons\\resource_cache.png",                   IconType::Button_ResourceCache);
        LoadFromFile(data_dir + "Icons\\renderdoc.png",                        IconType::Button_RenderDoc);
        LoadFromFile(data_dir + "Icons\\file.png",                             IconType::Directory_File_Default);
        LoadFromFile(data_dir + "Icons\\folder.png",                           IconType::Directory_Folder);
        LoadFromFile(data_dir + "Icons\\audio.png",                            IconType::Directory_File_Audio);
        LoadFromFile(data_dir + "Icons\\model.png",                            IconType::Directory_File_Model);
        LoadFromFile(data_dir + "Icons\\world.png",                            IconType::Directory_File_World);
        LoadFromFile(data_dir + "Icons\\material.png",                         IconType::Directory_File_Material);
        LoadFromFile(data_dir + "Icons\\shader.png",                           IconType::Directory_File_Shader);
        LoadFromFile(data_dir + "Icons\\xml.png",                              IconType::Directory_File_Xml);
        LoadFromFile(data_dir + "Icons\\dll.png",                              IconType::Directory_File_Dll);
        LoadFromFile(data_dir + "Icons\\txt.png",                              IconType::Directory_File_Txt);
        LoadFromFile(data_dir + "Icons\\ini.png",                              IconType::Directory_File_Ini);
        LoadFromFile(data_dir + "Icons\\exe.png",                              IconType::Directory_File_Exe);
        LoadFromFile(data_dir + "Icons\\font.png",                             IconType::Directory_File_Font);
        LoadFromFile(data_dir + "Icons\\texture.png",                          IconType::Directory_File_Texture);
    });

    // Subscribe to renderer event
    SP_SUBSCRIBE_TO_EVENT(EventType::RendererOnShutdown, SP_EVENT_HANDLER_STATIC(destroy_rhi_resources));
}

RHI_Texture* IconLoader::GetTextureByType(IconType type)
{
    return LoadFromFile("", type).GetTexture();
}

const Icon& IconLoader::LoadFromFile(const string& file_path, IconType type /*Undefined*/)
{
    // Check if the texture is already loaded, and return that
    bool search_by_type = type != IconType::Undefined;
    for (Icon& icon : icons)
    {
        if (search_by_type)
        {
            if (icon.GetType() == type)
                return icon;
        }
        else if (icon.GetFilePath() == file_path)
        {
            return icon;
        }
    }

    // The texture is new so load it
    if (FileSystem::IsSupportedImageFile(file_path) || FileSystem::IsEngineTextureFile(file_path))
    {
        // Add a new icon
        lock_guard<mutex> guard(icon_mutex);
        icons.emplace_back(type, file_path);

        // Return it
        return icons.back();
    }

    return get_icon_by_type(IconType::Directory_File_Default);
}
