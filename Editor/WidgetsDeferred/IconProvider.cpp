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

//= INCLUDES =================
#include "IconProvider.h"
#include "Rendering/Model.h"
#include "../ImGuiExtension.h"
//============================

//= NAMESPACES ==========
using namespace std;
using namespace Spartan;
//=======================

static Thumbnail g_no_thumbnail;

IconProvider::IconProvider()
{
    m_context = nullptr;
}

IconProvider::~IconProvider()
{
    m_thumbnails.clear();
}

void IconProvider::Initialize(Context* context)
{
    m_context = context;
    const string data_dir = m_context->GetSubsystem<ResourceCache>()->GetResourceDirectory() + "/";

    // Load standard icons
    LoadFromFile(data_dir + "Icons/component_componentOptions.png",       IconType::Component_Options);
    LoadFromFile(data_dir + "Icons/component_audioListener.png",          IconType::Component_AudioListener);
    LoadFromFile(data_dir + "Icons/component_audioSource.png",            IconType::Component_AudioSource);
    LoadFromFile(data_dir + "Icons/component_reflectionProbe.png",        IconType::Component_ReflectionProbe);
    LoadFromFile(data_dir + "Icons/component_camera.png",                 IconType::Component_Camera); 
    LoadFromFile(data_dir + "Icons/component_collider.png",               IconType::Component_Collider);
    LoadFromFile(data_dir + "Icons/component_light.png",                  IconType::Component_Light);
    LoadFromFile(data_dir + "Icons/component_material.png",               IconType::Component_Material);
    LoadFromFile(data_dir + "Icons/component_material_removeTexture.png", IconType::Component_Material_RemoveTexture);
    LoadFromFile(data_dir + "Icons/component_meshCollider.png",           IconType::Component_MeshCollider);
    LoadFromFile(data_dir + "Icons/component_renderable.png",             IconType::Component_Renderable);
    LoadFromFile(data_dir + "Icons/component_rigidBody.png",              IconType::Component_RigidBody);
    LoadFromFile(data_dir + "Icons/component_softBody.png",               IconType::Component_SoftBody);
    LoadFromFile(data_dir + "Icons/component_script.png",                 IconType::Component_Script);
    LoadFromFile(data_dir + "Icons/component_transform.png",              IconType::Component_Transform);
    LoadFromFile(data_dir + "Icons/component_terrain.png",                IconType::Component_Terrain);
    LoadFromFile(data_dir + "Icons/component_environment.png",            IconType::Component_Environment);
    LoadFromFile(data_dir + "Icons/console_info.png",                     IconType::Console_Info);
    LoadFromFile(data_dir + "Icons/console_warning.png",                  IconType::Console_Warning);
    LoadFromFile(data_dir + "Icons/console_error.png",                    IconType::Console_Error);
    LoadFromFile(data_dir + "Icons/button_play.png",                      IconType::Button_Play);
    LoadFromFile(data_dir + "Icons/profiler.png",                         IconType::Button_Profiler);
    LoadFromFile(data_dir + "Icons/resource_cache.png",                   IconType::Button_ResourceCache);
    LoadFromFile(data_dir + "Icons/bookmark.png",                         IconType::Button_BookMarkViewer);
    LoadFromFile(data_dir + "Icons/file.png",                             IconType::Directory_File_Default);
    LoadFromFile(data_dir + "Icons/folder.png",                           IconType::Directory_Folder);
    LoadFromFile(data_dir + "Icons/audio.png",                            IconType::Directory_File_Audio);
    LoadFromFile(data_dir + "Icons/model.png",                            IconType::Directory_File_Model);
    LoadFromFile(data_dir + "Icons/world.png",                            IconType::Directory_File_World);
    LoadFromFile(data_dir + "Icons/material.png",                         IconType::Directory_File_Material);
    LoadFromFile(data_dir + "Icons/shader.png",                           IconType::Directory_File_Shader);
    LoadFromFile(data_dir + "Icons/xml.png",                              IconType::Directory_File_Xml);
    LoadFromFile(data_dir + "Icons/dll.png",                              IconType::Directory_File_Dll);
    LoadFromFile(data_dir + "Icons/txt.png",                              IconType::Directory_File_Txt);
    LoadFromFile(data_dir + "Icons/ini.png",                              IconType::Directory_File_Ini);
    LoadFromFile(data_dir + "Icons/exe.png",                              IconType::Directory_File_Exe);
    LoadFromFile(data_dir + "Icons/script.png",                           IconType::Directory_File_Script);
    LoadFromFile(data_dir + "Icons/font.png",                             IconType::Directory_File_Font);
    LoadFromFile(data_dir + "Icons/texture.png",                          IconType::Directory_File_Texture);
}

RHI_Texture* IconProvider::GetTextureByType(IconType type)
{
    return LoadFromFile("", type).texture.get();
}

RHI_Texture* IconProvider::GetTextureByFilePath(const string& filePath)
{
    return LoadFromFile(filePath).texture.get();
}

RHI_Texture* IconProvider::GetTextureByThumbnail(const Thumbnail& thumbnail)
{
    for (const auto& thumbnailTemp : m_thumbnails)
    {
        if (thumbnailTemp.texture->IsLoading())
            continue;

        if (thumbnailTemp.texture->GetObjectId() == thumbnail.texture->GetObjectId())
        {
            return thumbnailTemp.texture.get();
        }
    }

    return nullptr;
}

const Thumbnail& IconProvider::LoadFromFile(const string& file_path, IconType type /*NotAssigned*/, const uint32_t size /*100*/)
{
    // Check if we already have this thumbnail
    bool search_by_type = type != IconType::NotAssigned;
    for (Thumbnail& thumbnail : m_thumbnails)
    {
        if (search_by_type)
        {
            if (thumbnail.type == type)
                return thumbnail;
        }
        else if (thumbnail.file_path == file_path)
        {
            return thumbnail;
        }
    }

    // Deduce file path type

    // Directory
    if (FileSystem::IsDirectory(file_path))                        return GetThumbnailByType(IconType::Directory_Folder);
    // Model                                                       
    if (FileSystem::IsSupportedModelFile(file_path))               return GetThumbnailByType(IconType::Directory_File_Model);
    // Audio                                                       
    if (FileSystem::IsSupportedAudioFile(file_path))               return GetThumbnailByType(IconType::Directory_File_Audio);
    // Material                                                    
    if (FileSystem::IsEngineMaterialFile(file_path))               return GetThumbnailByType(IconType::Directory_File_Material);
    // Shader                                                      
    if (FileSystem::IsSupportedShaderFile(file_path))              return GetThumbnailByType(IconType::Directory_File_Shader);
    // Scene                                                       
    if (FileSystem::IsEngineSceneFile(file_path))                  return GetThumbnailByType(IconType::Directory_File_World);
    // Script                                                      
    if (FileSystem::IsEngineScriptFile(file_path))                 return GetThumbnailByType(IconType::Directory_File_Script);
    // Font                                                        
    if (FileSystem::IsSupportedFontFile(file_path))                return GetThumbnailByType(IconType::Directory_File_Font);
                                                                   
    // Xml                                                         
    if (FileSystem::GetExtensionFromFilePath(file_path) == ".xml") return GetThumbnailByType(IconType::Directory_File_Xml);
    // Dll                                                         
    if (FileSystem::GetExtensionFromFilePath(file_path) == ".dll") return GetThumbnailByType(IconType::Directory_File_Dll);
    // Txt                                                         
    if (FileSystem::GetExtensionFromFilePath(file_path) == ".txt") return GetThumbnailByType(IconType::Directory_File_Txt);
    // Ini                                                         
    if (FileSystem::GetExtensionFromFilePath(file_path) == ".ini") return GetThumbnailByType(IconType::Directory_File_Ini);
    // Exe                                                         
    if (FileSystem::GetExtensionFromFilePath(file_path) == ".exe") return GetThumbnailByType(IconType::Directory_File_Exe);

    // Texture
    if (FileSystem::IsSupportedImageFile(file_path) || FileSystem::IsEngineTextureFile(file_path))
    {
        // Created a texture
        auto texture = make_shared<RHI_Texture2D>(m_context, RHI_Texture_Flags::RHI_Texture_Srv);
        texture->SetWidth(size);
        texture->SetHeight(size);

        // Load it asynchronously
        m_context->GetSubsystem<Threading>()->AddTask([texture, file_path]()
        {
            texture->LoadFromFile(file_path);
        });

        m_thumbnails.emplace_back(type, texture, file_path);
        return m_thumbnails.back();
    }

    return GetThumbnailByType(IconType::Directory_File_Default);
}

const Thumbnail& IconProvider::GetThumbnailByType(IconType type)
{
    for (Thumbnail& thumbnail : m_thumbnails)
    {
        if (thumbnail.type == type)
            return thumbnail;
    }

    return g_no_thumbnail;
}
