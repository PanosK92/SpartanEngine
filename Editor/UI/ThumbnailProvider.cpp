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

//= INCLUDES ================
#include "ThumbnailProvider.h"
#include "Graphics/Texture.h"
#include "ImGui/imgui.h"
#include "EditorHelper.h"
#include "Threading/Threading.h"
//===========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

vector<Thumbnail> ThumbnailProvider::m_thumbnails;
static Context* g_context;
static int g_id = -1;

void ThumbnailProvider::Initialize(Context* context)
{
	g_context = context;

	Thumbnail_Load(Icon_Component_Options,		"Standard Assets\\Editor\\component_ComponentOptions.png");
	Thumbnail_Load(Icon_Component_AudioListener,	"Standard Assets\\Editor\\component_AudioListener.png");
	Thumbnail_Load(Icon_Component_AudioSource,	"Standard Assets\\Editor\\component_AudioSource.png");
	Thumbnail_Load(Icon_Component_Camera,		"Standard Assets\\Editor\\component_Camera.png");
	Thumbnail_Load(Icon_Component_Collider,		"Standard Assets\\Editor\\component_Collider.png");
	Thumbnail_Load(Icon_Component_Light,			"Standard Assets\\Editor\\component_Light.png");
	Thumbnail_Load(Icon_Component_Material,		"Standard Assets\\Editor\\component_Material.png");
	Thumbnail_Load(Icon_Component_MeshCollider,	"Standard Assets\\Editor\\component_MeshCollider.png");
	Thumbnail_Load(Icon_Component_MeshFilter,	"Standard Assets\\Editor\\component_MeshFilter.png");
	Thumbnail_Load(Icon_Component_MeshRenderer,	"Standard Assets\\Editor\\component_MeshRenderer.png");
	Thumbnail_Load(Icon_Component_RigidBody,		"Standard Assets\\Editor\\component_RigidBody.png");
	Thumbnail_Load(Icon_Component_Script,		"Standard Assets\\Editor\\component_Script.png");
	Thumbnail_Load(Icon_Component_Transform,		"Standard Assets\\Editor\\component_Transform.png");
	Thumbnail_Load(Icon_Console_Info,			"Standard Assets\\Editor\\console_info.png");
	Thumbnail_Load(Icon_Console_Warning,			"Standard Assets\\Editor\\console_warning.png");
	Thumbnail_Load(Icon_Console_Error,			"Standard Assets\\Editor\\console_error.png");
	Thumbnail_Load(Icon_File_Default,			"Standard Assets\\Editor\\file.png");
	Thumbnail_Load(Icon_Folder,					"Standard Assets\\Editor\\folder.png");
	Thumbnail_Load(Icon_File_Audio,				"Standard Assets\\Editor\\audio.png");
	Thumbnail_Load(Icon_File_Model,				"Standard Assets\\Editor\\model.png");
	Thumbnail_Load(Icon_File_Scene,				"Standard Assets\\Editor\\scene.png");
	Thumbnail_Load(Icon_Button_Play,				"Standard Assets\\Editor\\button_play.png");
}

void* ThumbnailProvider::GetShaderResourceByEnum(Thumbnail_Type iconEnum)
{
	for (const auto& icon : m_thumbnails)
	{
		if (icon.texture->GetAsyncState() != Async_Completed)
			continue;

		if (icon.type == iconEnum)
		{
			return icon.texture->GetShaderResource();
		}
	}

	return nullptr;
}

void* ThumbnailProvider::GetShaderResourceByFilePath(const std::string& filePath)
{
	// Validate file path
	if (FileSystem::IsDirectory(filePath))
		return GetShaderResourceByEnum(Icon_Folder);

	// Texture
	if (FileSystem::IsSupportedImageFile(filePath) || FileSystem::IsEngineTextureFile(filePath))
	{
		if (!Thumbnail_Exists(filePath))
		{
			Thumbnail_Load(Icon_Custom, filePath);
			return nullptr;
		}

		for (const auto& icon : m_thumbnails)
		{
			if (icon.filePath == filePath)
			{
				if (icon.texture->GetAsyncState() == Async_Completed)
				{
					return icon.texture->GetShaderResource();
				}
			}	
		}
	}

	// Model
	if (FileSystem::IsSupportedModelFile(filePath))
	{
		return GetShaderResourceByEnum(Icon_File_Model);
	}

	// Audio
	if (FileSystem::IsSupportedAudioFile(filePath))
	{
		return GetShaderResourceByEnum(Icon_File_Audio);
	}

	// Scene
	if (FileSystem::IsEngineSceneFile(filePath))
	{
		return GetShaderResourceByEnum(Icon_File_Scene);
	}

	// Default file
	return GetShaderResourceByEnum(Icon_File_Default);
}

bool ThumbnailProvider::ImageButton_enum_id(const char* id, Thumbnail_Type iconEnum, float size)
{
	ImGui::PushID(id);
	bool pressed = ImGui::ImageButton(GetShaderResourceByEnum(iconEnum), ImVec2(size, size));
	ImGui::PopID();

	return pressed;
}

bool ThumbnailProvider::ImageButton_filepath(const std::string& filepath, float size)
{
	bool pressed = ImGui::ImageButton(GetShaderResourceByFilePath(filepath), ImVec2(size, size));

	return pressed;
}

void ThumbnailProvider::Thumbnail_Load(Thumbnail_Type iconEnum, const string& filePath)
{
	if (auto texture = LoadThumbnail(filePath, g_context))
	{
		m_thumbnails.emplace_back(iconEnum, texture, filePath);
	}
}

bool ThumbnailProvider::Thumbnail_Exists(const std::string& filePath)
{
	for (const auto& thumbnail : m_thumbnails)
	{
		if (thumbnail.filePath == filePath)
			return true;
	}

	return false;
}

shared_ptr<Texture> ThumbnailProvider::LoadThumbnail(const std::string& filePath, Context* context)
{
	// Validate file path
	if (FileSystem::IsDirectory(filePath))
		return nullptr;
	if (!FileSystem::IsSupportedImageFile(filePath) && !FileSystem::IsEngineTextureFile(filePath))
		return nullptr;

	// Make a cheap texture
	auto texture = std::make_shared<Texture>(context);
	texture->EnableMimaps(false);
	texture->SetWidth(100);
	texture->SetHeight(100);

	// Load it asynchronously
	context->GetSubsystem<Threading>()->AddTask([texture, filePath]()
	{
		texture->LoadFromFile(filePath);
	});

	return texture;
}