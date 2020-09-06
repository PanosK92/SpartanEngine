/*
Copyright(c) 2016-2020 Panos Karabelas

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
#include <string>
#include <utility>
#include <vector>
#include <memory>
#include "RHI/RHI_Definition.h"
//=============================

enum Icon_Type
{
    Icon_NotAssigned,
    Icon_Component_Options,
    Icon_Component_AudioListener,
    Icon_Component_AudioSource,
    Icon_Component_Camera,
    Icon_Component_Collider,
    Icon_Component_Light,
    Icon_Component_Material,
    Icon_Component_Material_RemoveTexture,
    Icon_Component_MeshCollider,
    Icon_Component_Renderable,
    Icon_Component_RigidBody,
    Icon_Component_SoftBody,
    Icon_Component_Script,
    Icon_Component_Terrain,
    Icon_Component_Environment,
    Icon_Component_Transform,
    Icon_Console_Info,
    Icon_Console_Warning,
    Icon_Console_Error,    
    Icon_Button_Play,
    Icon_Profiler,
    Icon_ResourceCache,
    Thumbnail_Custom,
    Thumbnail_Folder,
    Thumbnail_File_Audio,
    Thumbnail_File_Scene,
    Thumbnail_File_Model,
    Thumbnail_File_Default,
    Thumbnail_File_Material,
    Thumbnail_File_Shader,
    Thumbnail_File_Xml,
    Thumbnail_File_Dll,
    Thumbnail_File_Txt,
    Thumbnail_File_Ini,
    Thumbnail_File_Exe,
    Thumbnail_File_Script,
    Thumbnail_File_Font
};

namespace Spartan { class Context; }

struct Thumbnail
{
    Thumbnail() = default;
    Thumbnail(Icon_Type type, std::shared_ptr<Spartan::RHI_Texture> texture, const std::string& filePath)
    {
        this->type      = type;
        this->texture   = std::move(texture);
        this->filePath  = filePath;
    }

    Icon_Type type = Icon_NotAssigned;
    std::shared_ptr<Spartan::RHI_Texture> texture;
    std::string filePath;
};

class IconProvider
{
public:
    static IconProvider& Get()
    {
        static IconProvider instance;
        return instance;
    }

    IconProvider();
    ~IconProvider();

    void Initialize(Spartan::Context* context);

    Spartan::RHI_Texture* GetTextureByType(Icon_Type type);
    Spartan::RHI_Texture* GetTextureByFilePath(const std::string& filePath);
    Spartan::RHI_Texture* GetTextureByThumbnail(const Thumbnail& thumbnail);
    const Thumbnail& Thumbnail_Load(const std::string& filePath, Icon_Type type = Thumbnail_Custom, int size = 100);

private:
    const Thumbnail& GetThumbnailByType(Icon_Type type);
    std::vector<Thumbnail> m_thumbnails;
    Spartan::Context* m_context;
};
