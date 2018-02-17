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
#include "IconProvider.h"
#include "Graphics/Texture.h"
#include "ImGui/imgui.h"
#include "EditorHelper.h"
#include "Threading/Threading.h"
//===========================

//= NAMESPACES ==========
using namespace std;
using namespace Directus;
//=======================

vector<IconProviderImage> IconProvider::m_icons;
static Context* g_context;
static int g_id = -1;

void IconProvider::Initialize(Context* context)
{
	g_context = context;

	LoadAsync(Icon_Component_Options,		"Standard Assets\\Editor\\component_ComponentOptions.png");
	LoadAsync(Icon_Component_AudioListener,	"Standard Assets\\Editor\\component_AudioListener.png");
	LoadAsync(Icon_Component_AudioSource,	"Standard Assets\\Editor\\component_AudioSource.png");
	LoadAsync(Icon_Component_Camera,		"Standard Assets\\Editor\\component_Camera.png");
	LoadAsync(Icon_Component_Collider,		"Standard Assets\\Editor\\component_Collider.png");
	LoadAsync(Icon_Component_Light,			"Standard Assets\\Editor\\component_Light.png");
	LoadAsync(Icon_Component_Material,		"Standard Assets\\Editor\\component_Material.png");
	LoadAsync(Icon_Component_MeshCollider,	"Standard Assets\\Editor\\component_MeshCollider.png");
	LoadAsync(Icon_Component_MeshFilter,	"Standard Assets\\Editor\\component_MeshFilter.png");
	LoadAsync(Icon_Component_MeshRenderer,	"Standard Assets\\Editor\\component_MeshRenderer.png");
	LoadAsync(Icon_Component_RigidBody,		"Standard Assets\\Editor\\component_RigidBody.png");
	LoadAsync(Icon_Component_Script,		"Standard Assets\\Editor\\component_Script.png");
	LoadAsync(Icon_Component_Transform,		"Standard Assets\\Editor\\component_Transform.png");
	LoadAsync(Icon_Console_Info,			"Standard Assets\\Editor\\console_info.png");
	LoadAsync(Icon_Console_Warning,			"Standard Assets\\Editor\\console_warning.png");
	LoadAsync(Icon_Console_Error,			"Standard Assets\\Editor\\console_error.png");
	LoadAsync(Icon_File_Default,			"Standard Assets\\Editor\\file.png");
	LoadAsync(Icon_Folder,					"Standard Assets\\Editor\\folder.png");
	LoadAsync(Icon_File_Audio,				"Standard Assets\\Editor\\audio.png");
	LoadAsync(Icon_File_Model,				"Standard Assets\\Editor\\model.png");
	LoadAsync(Icon_File_Scene,				"Standard Assets\\Editor\\scene.png");
	LoadAsync(Icon_Button_Play,				"Standard Assets\\Editor\\button_play.png");
}

void* IconProvider::GetShaderResourceByEnum(IconProvider_Icon iconEnum)
{
	for (const auto& icon : m_icons)
	{
		if (icon.texture->GetAsyncState() != Async_Completed)
			continue;

		if (icon.iconEnum == iconEnum)
		{
			return icon.texture->GetShaderResource();
		}
	}

	return nullptr;
}

void* IconProvider::GetShaderResourceByFilePath(const std::string& filePath)
{
	// Validate file path
	if (FileSystem::IsDirectory(filePath))
		return GetShaderResourceByEnum(Icon_Folder);

	// Texture
	if (FileSystem::IsSupportedImageFile(filePath) || FileSystem::IsEngineTextureFile(filePath))
	{
		if (!IconExistsByFilePath(filePath))
		{
			LoadAsync(Icon_Custom, filePath);
			return nullptr;
		}

		for (const auto& icon : m_icons)
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

bool IconProvider::ImageButton_enum_id(const char* id, IconProvider_Icon iconEnum, float size)
{
	ImGui::PushID(id);
	bool pressed = ImGui::ImageButton(GetShaderResourceByEnum(iconEnum), ImVec2(size, size));
	ImGui::PopID();

	return pressed;
}

bool IconProvider::ImageButton_filepath(const std::string& filepath, float size)
{
	bool pressed = ImGui::ImageButton(GetShaderResourceByFilePath(filepath), ImVec2(size, size));

	return pressed;
}

void IconProvider::LoadAsync(IconProvider_Icon iconEnum, const string& filePath)
{
	if (auto texture = LoadThumbnail(filePath, g_context))
	{
		m_icons.emplace_back(iconEnum, texture, filePath);
	}
}

bool IconProvider::IconExistsByFilePath(const std::string& filePath)
{
	for (const auto& icon : m_icons)
	{
		if (icon.filePath == filePath)
			return true;
	}

	return false;
}

shared_ptr<Texture> IconProvider::LoadThumbnail(const std::string& filePath, Context* context)
{
	// Validate file path
	if (FileSystem::IsDirectory(filePath))
		return nullptr;
	if (!FileSystem::IsSupportedImageFile(filePath) && !FileSystem::IsEngineTextureFile(filePath))
		return nullptr;

	// Compute some useful information
	auto path = FileSystem::GetRelativeFilePath(filePath);
	auto name = FileSystem::GetFileNameNoExtensionFromFilePath(path);

	// Check if this texture is already cached, if so return the cached one
	auto resourceManager = context->GetSubsystem<ResourceManager>();	
	if (auto cached = resourceManager->GetResourceByName<Texture>(name).lock())
	{			
		return cached;
	}

	// Since the texture is not cached, load it and returned a cached ref
	auto texture = std::make_shared<Texture>(context);
	texture->EnableMimaps(false);
	texture->SetWidth(100);
	texture->SetHeight(100);
	texture->SetResourceName(name);
	texture->SetResourceFilePath(path);
	context->GetSubsystem<Threading>()->AddTask([texture, filePath]()
	{
		texture->LoadFromFile(filePath);
	});

	return texture;
}