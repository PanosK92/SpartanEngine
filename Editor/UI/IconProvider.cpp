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
using namespace Spartan;
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
	string data_dir = m_context->GetSubsystem<ResourceCache>()->GetDataDirectory();

	// Load standard icons
	Thumbnail_Load(data_dir + "Icons\\component_componentOptions.png",		Icon_Component_Options);	
	Thumbnail_Load(data_dir + "Icons\\component_audioListener.png",			Icon_Component_AudioListener);
	Thumbnail_Load(data_dir + "Icons\\component_audioSource.png",			Icon_Component_AudioSource);	
	Thumbnail_Load(data_dir + "Icons\\component_camera.png",				Icon_Component_Camera);	
	Thumbnail_Load(data_dir + "Icons\\component_collider.png",				Icon_Component_Collider);	
	Thumbnail_Load(data_dir + "Icons\\component_light.png",					Icon_Component_Light);
	Thumbnail_Load(data_dir + "Icons\\component_material.png",				Icon_Component_Material);
	Thumbnail_Load(data_dir + "Icons\\component_material_removeTexture.png",Icon_Component_Material_RemoveTexture);
	Thumbnail_Load(data_dir + "Icons\\component_meshCollider.png",			Icon_Component_MeshCollider);	
	Thumbnail_Load(data_dir + "Icons\\component_renderable.png",			Icon_Component_Renderable);	
	Thumbnail_Load(data_dir + "Icons\\component_rigidBody.png",				Icon_Component_RigidBody);
	Thumbnail_Load(data_dir + "Icons\\component_script.png",				Icon_Component_Script);	
	Thumbnail_Load(data_dir + "Icons\\component_transform.png",				Icon_Component_Transform);
	Thumbnail_Load(data_dir + "Icons\\console_info.png",					Icon_Console_Info);	
	Thumbnail_Load(data_dir + "Icons\\console_warning.png",					Icon_Console_Warning);
	Thumbnail_Load(data_dir + "Icons\\console_error.png",					Icon_Console_Error);	
	Thumbnail_Load(data_dir + "Icons\\button_play.png",						Icon_Button_Play);
	Thumbnail_Load(data_dir + "Icons\\profiler.png",						Icon_Profiler);
	Thumbnail_Load(data_dir + "Icons\\resource_cache.png",					Icon_ResourceCache);
	Thumbnail_Load(data_dir + "Icons\\file.png",							Thumbnail_File_Default);	
	Thumbnail_Load(data_dir + "Icons\\folder.png",							Thumbnail_Folder);	
	Thumbnail_Load(data_dir + "Icons\\audio.png",							Thumbnail_File_Audio);	
	Thumbnail_Load(data_dir + "Icons\\model.png",							Thumbnail_File_Model);	
	Thumbnail_Load(data_dir + "Icons\\scene.png",							Thumbnail_File_Scene);	
	Thumbnail_Load(data_dir + "Icons\\material.png",						Thumbnail_File_Material);
	Thumbnail_Load(data_dir + "Icons\\shader.png",							Thumbnail_File_Shader);
	Thumbnail_Load(data_dir + "Icons\\xml.png",								Thumbnail_File_Xml);
	Thumbnail_Load(data_dir + "Icons\\dll.png",								Thumbnail_File_Dll);
	Thumbnail_Load(data_dir + "Icons\\txt.png",								Thumbnail_File_Txt);
	Thumbnail_Load(data_dir + "Icons\\ini.png",								Thumbnail_File_Ini);
	Thumbnail_Load(data_dir + "Icons\\exe.png",								Thumbnail_File_Exe);
	Thumbnail_Load(data_dir + "Icons\\script.png",							Thumbnail_File_Script);
	Thumbnail_Load(data_dir + "Icons\\font.png",							Thumbnail_File_Font);
}

void* IconProvider::GetShaderResourceByType(Icon_Type type)
{
	auto texture = Thumbnail_Load(NOT_ASSIGNED, type).texture;
	return texture ? texture->GetBufferView() : nullptr;
}

void* IconProvider::GetShaderResourceByFilePath(const std::string& filePath)
{
	auto texture = Thumbnail_Load(filePath).texture;
	return texture ? texture->GetBufferView() : nullptr;
}

void* IconProvider::GetShaderResourceByThumbnail(const Thumbnail& thumbnail)
{
	for (const auto& thumbnailTemp : m_thumbnails)
	{
		if (thumbnailTemp.texture->GetLoadState() != LoadState_Completed)
			continue;

		if (thumbnailTemp.texture->GetResourceId() == thumbnail.texture->GetResourceId())
		{
			return thumbnailTemp.texture->GetBufferView();
		}
	}

	return nullptr;
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
		auto texture = std::make_shared<RHI_Texture>(m_context, false);
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
