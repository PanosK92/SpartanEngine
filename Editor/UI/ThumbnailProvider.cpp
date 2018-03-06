/*
Copyright(c) 2016-2018 Panos Karabelas

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

//= INCLUDES ===================
#include "ThumbnailProvider.h"
#include "Graphics/Texture.h"
#include "../ImGui/imgui.h"
#include "EditorHelper.h"
#include "Threading/Threading.h"
//==============================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

static Thumbnail g_noThumbnail;

ThumbnailProvider::ThumbnailProvider()
{
	m_context = nullptr;
}

ThumbnailProvider::~ThumbnailProvider()
{
	m_thumbnails.clear();
}

void ThumbnailProvider::Initialize(Context* context)
{
	m_context = context;

	// Load standard some standard icons
	Thumbnail_Load("Standard Assets\\Editor\\component_ComponentOptions.png",	Icon_Component_Options);	
	Thumbnail_Load("Standard Assets\\Editor\\component_AudioListener.png",		Icon_Component_AudioListener);
	Thumbnail_Load("Standard Assets\\Editor\\component_AudioSource.png",		Icon_Component_AudioSource);	
	Thumbnail_Load("Standard Assets\\Editor\\component_Camera.png",				Icon_Component_Camera);	
	Thumbnail_Load("Standard Assets\\Editor\\component_Collider.png",			Icon_Component_Collider);	
	Thumbnail_Load("Standard Assets\\Editor\\component_Light.png",				Icon_Component_Light);
	Thumbnail_Load("Standard Assets\\Editor\\component_Material.png",			Icon_Component_Material);	
	Thumbnail_Load("Standard Assets\\Editor\\component_MeshCollider.png",		Icon_Component_MeshCollider);	
	Thumbnail_Load("Standard Assets\\Editor\\component_MeshFilter.png",			Icon_Component_MeshFilter);	
	Thumbnail_Load("Standard Assets\\Editor\\component_MeshRenderer.png",		Icon_Component_MeshRenderer);	
	Thumbnail_Load("Standard Assets\\Editor\\component_RigidBody.png",			Icon_Component_RigidBody);
	Thumbnail_Load("Standard Assets\\Editor\\component_Script.png",				Icon_Component_Script);	
	Thumbnail_Load("Standard Assets\\Editor\\component_Transform.png",			Icon_Component_Transform);
	Thumbnail_Load("Standard Assets\\Editor\\console_info.png",					Icon_Console_Info);	
	Thumbnail_Load("Standard Assets\\Editor\\console_warning.png",				Icon_Console_Warning);
	Thumbnail_Load("Standard Assets\\Editor\\console_error.png",				Icon_Console_Error);	
	Thumbnail_Load("Standard Assets\\Editor\\file.png",							Thumbnail_File_Default);	
	Thumbnail_Load("Standard Assets\\Editor\\folder.png",						Thumbnail_Folder);	
	Thumbnail_Load("Standard Assets\\Editor\\audio.png",						Thumbnail_File_Audio);	
	Thumbnail_Load("Standard Assets\\Editor\\model.png",						Thumbnail_File_Model);	
	Thumbnail_Load("Standard Assets\\Editor\\scene.png",						Thumbnail_File_Scene);	
	Thumbnail_Load("Standard Assets\\Editor\\button_play.png",					Icon_Button_Play);
}

void* ThumbnailProvider::GetShaderResourceByType(Thumbnail_Type type)
{
	return Thumbnail_Load(NOT_ASSIGNED, type).texture->GetShaderResource();
}

void* ThumbnailProvider::GetShaderResourceByFilePath(const std::string& filePath)
{
	return Thumbnail_Load(filePath).texture->GetShaderResource();
}

void* ThumbnailProvider::GetShaderResourceByThumbnail(const Thumbnail& thumbnail)
{
	for (const auto& thumbnailTemp : m_thumbnails)
	{
		if (thumbnailTemp.texture->GetLoadState() != LoadState_Completed)
			continue;

		if (thumbnailTemp.texture->GetResourceID() == thumbnail.texture->GetResourceID())
		{
			return thumbnailTemp.texture->GetShaderResource();
		}
	}

	return nullptr;
}

bool ThumbnailProvider::ImageButton_enum_id(const char* id, Thumbnail_Type iconEnum, float size)
{
	ImGui::PushID(id);
	bool pressed = ImGui::ImageButton(GetShaderResourceByType(iconEnum), ImVec2(size, size));
	ImGui::PopID();

	return pressed;
}

bool ThumbnailProvider::ImageButton_filepath(const std::string& filepath, float size)
{
	bool pressed = ImGui::ImageButton(GetShaderResourceByFilePath(filepath), ImVec2(size, size));

	return pressed;
}

const Thumbnail& ThumbnailProvider::Thumbnail_Load(const string& filePath, Thumbnail_Type type /*Icon_Custom*/, int size /*100*/)
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
	if (FileSystem::IsDirectory(filePath))
		return GetThumbnailByType(Thumbnail_Folder);

	// Model
	if (FileSystem::IsSupportedModelFile(filePath))
	{
		return GetThumbnailByType(Thumbnail_File_Model);
	}

	// Audio
	if (FileSystem::IsSupportedAudioFile(filePath))
	{
		return GetThumbnailByType(Thumbnail_File_Audio);
	}

	// Scene
	if (FileSystem::IsEngineSceneFile(filePath))
	{
		return GetThumbnailByType(Thumbnail_File_Scene);
	}

	// Texture
	if (FileSystem::IsSupportedImageFile(filePath) || FileSystem::IsEngineTextureFile(filePath))
	{
		// Make a cheap texture
		auto texture = std::make_shared<Texture>(m_context);
		texture->EnableMimaps(false);
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

const Thumbnail& ThumbnailProvider::GetThumbnailByType(Thumbnail_Type type)
{
	for (auto& thumbnail : m_thumbnails)
	{
		if (thumbnail.type == type)
			return thumbnail;
	}

	return g_noThumbnail;
}
