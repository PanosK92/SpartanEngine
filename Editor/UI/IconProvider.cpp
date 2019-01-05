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

//= INCLUDES ============
#include "IconProvider.h"
#include "EditorHelper.h"
//=======================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static Thumbnail g_noThumbnail;

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

	// Load standard some standard icons
	Thumbnail_Load("Standard Assets\\Icons\\component_ComponentOptions.png",	Icon_Component_Options);	
	Thumbnail_Load("Standard Assets\\Icons\\component_AudioListener.png",		Icon_Component_AudioListener);
	Thumbnail_Load("Standard Assets\\Icons\\component_AudioSource.png",			Icon_Component_AudioSource);	
	Thumbnail_Load("Standard Assets\\Icons\\component_Camera.png",				Icon_Component_Camera);	
	Thumbnail_Load("Standard Assets\\Icons\\component_Collider.png",			Icon_Component_Collider);	
	Thumbnail_Load("Standard Assets\\Icons\\component_Light.png",				Icon_Component_Light);
	Thumbnail_Load("Standard Assets\\Icons\\component_Material.png",			Icon_Component_Material);	
	Thumbnail_Load("Standard Assets\\Icons\\component_MeshCollider.png",		Icon_Component_MeshCollider);	
	Thumbnail_Load("Standard Assets\\Icons\\component_renderable.png",			Icon_Component_Renderable);	
	Thumbnail_Load("Standard Assets\\Icons\\component_RigidBody.png",			Icon_Component_RigidBody);
	Thumbnail_Load("Standard Assets\\Icons\\component_Script.png",				Icon_Component_Script);	
	Thumbnail_Load("Standard Assets\\Icons\\component_Transform.png",			Icon_Component_Transform);
	Thumbnail_Load("Standard Assets\\Icons\\console_info.png",					Icon_Console_Info);	
	Thumbnail_Load("Standard Assets\\Icons\\console_warning.png",				Icon_Console_Warning);
	Thumbnail_Load("Standard Assets\\Icons\\console_error.png",					Icon_Console_Error);	
	Thumbnail_Load("Standard Assets\\Icons\\button_play.png",					Icon_Button_Play);
	Thumbnail_Load("Standard Assets\\Icons\\profiler.png",						Icon_Profiler);
	Thumbnail_Load("Standard Assets\\Icons\\file.png",							Thumbnail_File_Default);	
	Thumbnail_Load("Standard Assets\\Icons\\folder.png",						Thumbnail_Folder);	
	Thumbnail_Load("Standard Assets\\Icons\\audio.png",							Thumbnail_File_Audio);	
	Thumbnail_Load("Standard Assets\\Icons\\model.png",							Thumbnail_File_Model);	
	Thumbnail_Load("Standard Assets\\Icons\\scene.png",							Thumbnail_File_Scene);	
	Thumbnail_Load("Standard Assets\\Icons\\material.png",						Thumbnail_File_Material);
	Thumbnail_Load("Standard Assets\\Icons\\shader.png",						Thumbnail_File_Shader);
	Thumbnail_Load("Standard Assets\\Icons\\xml.png",							Thumbnail_File_Xml);
	Thumbnail_Load("Standard Assets\\Icons\\dll.png",							Thumbnail_File_Dll);
	Thumbnail_Load("Standard Assets\\Icons\\txt.png",							Thumbnail_File_Txt);
	Thumbnail_Load("Standard Assets\\Icons\\ini.png",							Thumbnail_File_Ini);
	Thumbnail_Load("Standard Assets\\Icons\\exe.png",							Thumbnail_File_Exe);
	Thumbnail_Load("Standard Assets\\Icons\\script.png",						Thumbnail_File_Script);
	Thumbnail_Load("Standard Assets\\Icons\\font.png",							Thumbnail_File_Font);
	
}

void* IconProvider::GetShaderResourceByType(Icon_Type type)
{
	return Thumbnail_Load(NOT_ASSIGNED, type).texture->GetShaderResource();
}

void* IconProvider::GetShaderResourceByFilePath(const std::string& filePath)
{
	return Thumbnail_Load(filePath).texture->GetShaderResource();
}

void* IconProvider::GetShaderResourceByThumbnail(const Thumbnail& thumbnail)
{
	for (const auto& thumbnailTemp : m_thumbnails)
	{
		if (thumbnailTemp.texture->GetLoadState() != LoadState_Completed)
			continue;

		if (thumbnailTemp.texture->Resource_GetID() == thumbnail.texture->Resource_GetID())
		{
			return thumbnailTemp.texture->GetShaderResource();
		}
	}

	return nullptr;
}

bool IconProvider::ImageButton_enum_id(const char* id, Icon_Type iconEnum, float size)
{
	ImGui::PushID(id);
	bool pressed = ImGui::ImageButton(GetShaderResourceByType(iconEnum), ImVec2(size, size));
	ImGui::PopID();

	return pressed;
}

bool IconProvider::ImageButton_filepath(const std::string& filepath, float size)
{
	bool pressed = ImGui::ImageButton(GetShaderResourceByFilePath(filepath), ImVec2(size, size));

	return pressed;
}

const Thumbnail& IconProvider::Thumbnail_Load(const string& filePath, Icon_Type type /*Icon_Custom*/, int size /*100*/)
{
	// Check if we already have this thumbnail (by type)
	if (type != Thumbnail_Custom)
	{
		for (auto& thumbnail : m_thumbnails)
		{
			if (thumbnail.type == type)
				return thumbnail;
		}
	}
	else // Check if we already have this thumbnail (by path)
	{		
		for (auto& thumbnail : m_thumbnails)
		{
			if (thumbnail.filePath == filePath)
				return thumbnail;
		}
	}

	// Deduce file path type

	// Directory
	if (FileSystem::IsDirectory(filePath))							return GetThumbnailByType(Thumbnail_Folder);
	// Model
	if (FileSystem::IsSupportedModelFile(filePath))					return GetThumbnailByType(Thumbnail_File_Model);
	// Audio
	if (FileSystem::IsSupportedAudioFile(filePath))					return GetThumbnailByType(Thumbnail_File_Audio);
	// Material
	if (FileSystem::IsEngineMaterialFile(filePath))					return GetThumbnailByType(Thumbnail_File_Material);
	// Shader
	if (FileSystem::IsSupportedShaderFile(filePath))				return GetThumbnailByType(Thumbnail_File_Shader);
	// Scene
	if (FileSystem::IsEngineSceneFile(filePath))					return GetThumbnailByType(Thumbnail_File_Scene);
	// Script
	if (FileSystem::IsEngineScriptFile(filePath))					return GetThumbnailByType(Thumbnail_File_Script);
	// Font
	if (FileSystem::IsSupportedFontFile(filePath))					return GetThumbnailByType(Thumbnail_File_Font);

	// Xml
	if (FileSystem::GetExtensionFromFilePath(filePath) == ".xml")	return GetThumbnailByType(Thumbnail_File_Xml);
	// Dll
	if (FileSystem::GetExtensionFromFilePath(filePath) == ".dll")	return GetThumbnailByType(Thumbnail_File_Dll);
	// Txt
	if (FileSystem::GetExtensionFromFilePath(filePath) == ".txt")	return GetThumbnailByType(Thumbnail_File_Txt);
	// Ini
	if (FileSystem::GetExtensionFromFilePath(filePath) == ".ini")	return GetThumbnailByType(Thumbnail_File_Ini);
	// Exe
	if (FileSystem::GetExtensionFromFilePath(filePath) == ".exe")	return GetThumbnailByType(Thumbnail_File_Exe);

	// Texture
	if (FileSystem::IsSupportedImageFile(filePath) || FileSystem::IsEngineTextureFile(filePath))
	{
		// Make a cheap texture
		auto texture = std::make_shared<RHI_Texture>(m_context);
		texture->SetNeedsMipChain(false);
		texture->SetWidth(size);
		texture->SetHeight(size);

		// Load it asynchronously
		m_context->GetSubsystem<Threading>()->AddTask([texture, filePath]()
		{
			texture->LoadFromFile(filePath);
		});

		m_thumbnails.emplace_back(type, texture, filePath);
		return m_thumbnails.back();
	}

	return GetThumbnailByType(Thumbnail_File_Default);
}

const Thumbnail& IconProvider::GetThumbnailByType(Icon_Type type)
{
	for (auto& thumbnail : m_thumbnails)
	{
		if (thumbnail.type == type)
			return thumbnail;
	}

	return g_noThumbnail;
}
