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

//= INCLUDES ========================
#include <string>
#include <vector>
#include <memory>
#include "RHI/RHI_Definition.h"
//===================================

enum Icon_Type
{
	Icon_Component_Options,
	Icon_Component_AudioListener,
	Icon_Component_AudioSource,
	Icon_Component_Camera,
	Icon_Component_Collider,
	Icon_Component_Light,
	Icon_Component_Material,
	Icon_Component_MeshCollider,
	Icon_Component_Renderable,
	Icon_Component_RigidBody,
	Icon_Component_Script,
	Icon_Component_Transform,
	Icon_Console_Info,
	Icon_Console_Warning,
	Icon_Console_Error,	
	Icon_Button_Play,
	Icon_Profiler,
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

namespace Directus {class Context;}

struct Thumbnail
{
	Thumbnail(){}
	Thumbnail(Icon_Type type, std::shared_ptr<Directus::RHI_Texture> texture, const std::string& filePath)
	{
		this->type = type;
		this->texture = texture;
		this->filePath = filePath;
	}

	Icon_Type type;
	std::shared_ptr<Directus::RHI_Texture> texture;
	std::string filePath;
};

class IconProvider
{
public:
	IconProvider();
	~IconProvider();

	void Initialize(Directus::Context* context);

	//= SHADER RESOURCE ===========================================
	void* GetShaderResourceByType(Icon_Type type);
	void* GetShaderResourceByFilePath(const std::string& filePath);
	void* GetShaderResourceByThumbnail(const Thumbnail& thumbnail);
	//=============================================================

	//= ImGui::ImageButton =======================================================
	bool ImageButton_enum_id(const char* id, Icon_Type iconEnum, float size);
	bool ImageButton_filepath(const std::string& filepath, float size);
	//============================================================================

	//= THUMBNAIL ==================================================================================================
	const Thumbnail& Thumbnail_Load(const std::string& filePath, Icon_Type type = Thumbnail_Custom, int size = 100);
	//==============================================================================================================

	 static IconProvider& Get()
     {
         static IconProvider instance;
         return instance;
     }

private:
	const Thumbnail& GetThumbnailByType(Icon_Type type);
	std::vector<Thumbnail> m_thumbnails;
	Directus::Context* m_context;
};